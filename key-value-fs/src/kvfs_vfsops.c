/*
 * VFS operations for kvfs
 * */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/vnode.h>

#include <geom/geom.h>
#include <geom/geom_vfs.h>

#include "kvfs.h"

MALLOC_DEFINE(M_KVFSMOUNT, "kvfs_mount", "kvfs mount structure");
MALLOC_DEFINE(M_KVFSFREE, "kvfs_freelist", "kvfs free list entries");

uma_zone_t kvfs_zone_node = NULL;

static vfs_init_t kvfs_init;
static vfs_uninit_t kvfs_uninit;
static vfs_mount_t kvfs_mount;
static vfs_unmount_t kvfs_unmount;
static vfs_root_t kvfs_root;
static vfs_statfs_t kvfs_statfs;
static vfs_sync_t kvfs_sync;
static vfs_vget_t kvfs_vget;

static int
kvfs_init(struct vfsconf __unused *confp)
{
	// create uma zone for our in-memory inode representation
	kvfs_zone_node = uma_zcreate("KVFS Node zone",
	    sizeof(struct kvfs_memnode), NULL, NULL, NULL, NULL, 0, 0);
	if (kvfs_zone_node == NULL) {
		printf("Cannot create allocation zones.\n");
		return (ENOMEM);
	}

	return (0);
}

static int
kvfs_uninit(struct vfsconf __unused *confp)
{
	if (kvfs_zone_node != NULL) {
		uma_zdestroy(kvfs_zone_node);
		kvfs_zone_node = NULL;
	}
	return (0);
}

static int
kvfs_mount(struct mount *mp)
{
	char *from;
	struct nameidata ndp;
	int error = 0;
	struct cdev *cdev;

	/* these resources must be released if we fail */
	struct g_consumer *cp = NULL;
	struct kvfs_mount *kvfsmp = NULL;
	struct buf *bp = NULL;

	/* XXX magic global variables, oh boy */
	struct thread *td = curthread;

	printf("mounting ...\n");

	/* check operation requested an update,
	 * like a name change or permission change */
	if (mp->mnt_flag & MNT_UPDATE) {
		/* XXX do we need update support? */
		return (EOPNOTSUPP);
	}

	/* look up the name and verify that it refers to a sensible disk device
	 */
	if (vfs_getopt(mp->mnt_optnew, "from", (void **)&from, NULL)) {
		return (EINVAL);
	}
	NDINIT(&ndp, LOOKUP, FOLLOW | LOCKLEAF, UIO_SYSSPACE, from, td);
	if ((error = namei(&ndp))) {
		return (error);
	}

	/* save vnode for blk device to mount */
	struct vnode *devvp = ndp.ni_vp;
	NDFREE(&ndp, NDF_ONLY_PNBUF);

	if (!vn_isdisk_error(devvp, &error)) {
		vput(devvp);
		return (error);
	}

	cdev = devvp->v_rdev;
	/* check if device is already mounted. if not, mount it. */
	if (atomic_cmpset_acq_ptr((uintptr_t *)&cdev->si_mountpt, 0,
		(uintptr_t)mp) == 0) {
		VOP_UNLOCK(devvp);
		return (EBUSY);
	}
	/* open new cdev for our mounted device */
	g_topology_lock();
	error = g_vfs_open(devvp, &cp, "kvfs",
	    (mp->mnt_flag & MNT_RDONLY) ? 0 : 1);
	g_topology_unlock();
	if (error != 0) {
		atomic_store_rel_ptr((uintptr_t *)&cdev->si_mountpt, 0);
		VOP_UNLOCK(devvp);
		return (error);
	}
	/* increment ref count on cdev associated with mount point */
	dev_ref(cdev);
	VOP_UNLOCK(devvp);

	/* allocate a struct to hold mount info for later calls */
	kvfsmp = malloc(sizeof(struct kvfs_mount), M_KVFSMOUNT,
	    M_WAITOK | M_ZERO);
	kvfsmp->vfs = devvp->v_mount;
	kvfsmp->devvp = devvp;
	kvfsmp->cdev = cdev;
	kvfsmp->cp = cp;
	mp->mnt_data = kvfsmp;
	MNT_ILOCK(mp);
	mp->mnt_flag |= MNT_LOCAL;
	/* tell kern we are using buffer cache */
	mp->mnt_kern_flag |= MNTK_USES_BCACHE;
	MNT_IUNLOCK(mp);

	/* get new unique fsid for kvfs */
	vfs_getnewfsid(mp);

	/* read superblock. we have padded superblock to BLOCKSIZE, so we just
	 * read a whole block */
	error = bread(devvp, 0, BLOCKSIZE, NOCRED, &bp);
	if (error != 0) {
		goto error_exit;
	}

	struct kvfs_superblock sb;
	memcpy(&sb, bp->b_data, sizeof(struct kvfs_superblock));
	if (sb.magicnum != KVFS_SUPERBLOCK_MAGIC) {
		printf(
		    "Error: not mounting kvfs filesystem. Expected 0x%.4x, got 0x%.4x\n",
		    KVFS_SUPERBLOCK_MAGIC, sb.magicnum);
		error = EINVAL;
		goto error_exit;
	}
	brelse(bp);

	kvfsmp->flags = sb.flags;
	kvfsmp->inode_off = sb.inode_off;
	kvfsmp->freelist_off = sb.freelist_off;
	kvfsmp->data_off = sb.data_off;
	kvfsmp->block_count = sb.block_count;

	/* init free list */
	LIST_INIT(&kvfsmp->freelist_head);
	/* read free list from location found in superblock */
	uint32_t freelist_bytes = CEIL(kvfsmp->block_count, 8);
	error = bread(devvp, btodb(kvfsmp->freelist_off), PAD(freelist_bytes),
	    NOCRED, &bp);
	if (error != 0) {
		goto error_exit;
	}

	/* read freelist byte by byte */
	for (int i = 0; i < freelist_bytes; i++) {
		/* each byte represents 8 indoe or block entries */
		ino_t curr_inode = (i * 8 * sizeof(struct kvfs_inode));
		uint8_t byte = bp->b_data[i];
		/* read msb to lsb */
		for (uint8_t mask = 1 << 7, j = 0; mask != 0; mask >>= 1, j++) {
			/* if bit not set in the free bitmap, create
			 * entry in the free list */
			if ((byte & mask) == 0) {
				/* the number of bytes in the free list bitmap
				 * is padded to CEIL(blocks/8). if the number of
				 * blocks is not divisible by 8, make sure not
				 * to read "aux" bits at the end of the padded
				 * byte */
				if (i * 8 + j >= kvfsmp->block_count) {
					break;
				}
				struct kvfs_freelist_entry *e =
				    malloc(sizeof(struct kvfs_freelist_entry),
					M_KVFSFREE, M_WAITOK);
				e->ino = curr_inode +
				    (j * sizeof(struct kvfs_inode));
				LIST_INSERT_HEAD(&kvfsmp->freelist_head, e,
				    entries);
				kvfsmp->freelist_count++;
			}
		}
	}
	printf("Found %d free blocks\n", kvfsmp->freelist_count);

	brelse(bp);
	/* mount fs */
	vfs_mountedfrom(mp, from);

	return (0);

error_exit:
	if (kvfsmp != NULL)
		free(kvfsmp, M_KVFSMOUNT);
	if (bp != NULL)
		brelse(bp);
	if (cp != NULL) {
		g_topology_lock();
		g_vfs_close(cp);
		g_topology_unlock();
	}
	dev_rel(cdev);
	atomic_store_rel_ptr((uintptr_t *)&cdev->si_mountpt, 0);
	return (error);
}

static int
kvfs_unmount(struct mount *mp, int mntflags)
{
	int error = 0;
	int flags = 0;
	printf("unmounting...\n");
	struct kvfs_mount *kvfsmp = mp->mnt_data;

	if (mntflags & MNT_FORCE) {
		flags |= FORCECLOSE;
	}

	error = vflush(mp, 0, flags, curthread);
	if (error != 0) {
		return (error);
	}

	g_topology_lock();
	g_vfs_close(kvfsmp->cp);
	g_topology_unlock();
	atomic_store_rel_ptr((uintptr_t *)&kvfsmp->cdev->si_mountpt, 0);
	vrele(kvfsmp->devvp);
	dev_rel(kvfsmp->cdev);

	/* free up freelist */
	struct kvfs_freelist_entry *curr, *next;
	curr = LIST_FIRST(&kvfsmp->freelist_head);
	while (curr != NULL) {
		next = LIST_NEXT(curr, entries);
		free(curr, M_KVFSFREE);
		curr = next;
	}
	free(kvfsmp, M_KVFSMOUNT);
	mp->mnt_data = NULL;
	MNT_ILOCK(mp);
	mp->mnt_flag &= ~MNT_LOCAL;
	MNT_IUNLOCK(mp);

	return (error);
}

static int
kvfs_root(struct mount *mp, int flags, struct vnode **vpp)
{
	printf("root\n");
	return kvfs_vget_internal(mp, KVFS_ROOT_INO, flags, vpp, NULL);
}

static int
kvfs_statfs(struct mount *mp, struct statfs *sbp)
{
	printf("stat\n");
	struct kvfs_mount *kvfsmp = mp->mnt_data;

	/* block size */
	sbp->f_bsize = BLOCKSIZE;
	/* optimal transfer size */
	sbp->f_iosize = BLOCKSIZE;
	/* total number of blocks */
	sbp->f_blocks = kvfsmp->block_count;
	/* number of free blocks */
	sbp->f_bfree = kvfsmp->freelist_count;
	/* blocks avail to regular user */
	sbp->f_bavail = kvfsmp->freelist_count;
	/* number of files in system */
	sbp->f_files = kvfsmp->block_count - kvfsmp->freelist_count;
	/* number of free file nodes. in KVFS, same as free block count */
	sbp->f_ffree = kvfsmp->freelist_count;
	/* max filename length. 40 hex characters = 160 bits */
	sbp->f_namemax = KVFS_KEY_STRLEN;

	return (0);
}

static int
kvfs_sync(struct mount *mp, int waitfor)
{
	printf("sync\n");
	/* TODO: sync and fsync are really broken */
#if 0
	// loop over all vnodes and fsync() them (based on ext2 and FAT)
	int error = 0;
	struct vnode *mvp, *vp;
loop:
	MNT_VNODE_FOREACH_ALL(vp, mp, mvp) {
		error = vget(vp, LK_EXCLUSIVE | LK_NOWAIT | LK_INTERLOCK);
		if (error) {
			// abort if vnode does not exist
			if (error == ENOENT) {
				MNT_VNODE_FOREACH_ALL_ABORT(mp, mvp);
				goto loop;
			}
			continue;
		}
		error = VOP_FSYNC(vp, waitfor, curthread);
		VOP_UNLOCK(vp);
		vrele(vp);
	}
	// TODO write back superblock here?
#endif
	return (0);
}
static int
kvfs_vget(struct mount *mp, ino_t ino, int flags, struct vnode **vpp)
{
	return (kvfs_vget_internal(mp, ino, flags, vpp, NULL));
}

/* get a vnode from cache, or allocate one.
 * */
int
kvfs_vget_internal(struct mount *mp, ino_t ino, int flags, struct vnode **vpp,
    const char *keystr)
{
	printf("kvfs_vget_internal\n");
	struct kvfs_mount *kvfsmp = mp->mnt_data;
	struct vnode *node = NULL;
	struct buf *bp = NULL;
	struct kvfs_memnode *knp = NULL;
	int error = 0;

	error = vfs_hash_get(mp, ino, flags, curthread, vpp, NULL, NULL);
	if (*vpp != NULL) {
		printf("  returning cached vnode\n");
		return (0);
	} else if (error != 0) {
		return (error);
	}

	/*
	 * We must promote to an exclusive lock for vnode creation.
	 * This can happen if vget is passed LOCKSHARED.
	 */
	if ((flags & LK_TYPE_MASK) == LK_SHARED) {
		flags &= ~LK_TYPE_MASK;
		flags |= LK_EXCLUSIVE;
	}

	error = getnewvnode("kvfs", mp, &kvfs_vnodeops, &node);
	if (error != 0) {
		*vpp = NULL;
		return (error);
	}

	/* lock the new root vnode */
	error = vn_lock(node, LK_EXCLUSIVE | LK_RETRY);
	if (error != 0) {
		*vpp = NULL;
		return (error);
	}

	/* associate our new vnode with the mount */
	error = insmntque(node, mp);
	if (error != 0) {
		return (error);
	}

	error = vfs_hash_insert(node, ino, flags, curthread, vpp, NULL, NULL);
	if (error != 0 || *vpp != NULL) {
		return (error);
	}

	/* allocate a new node in memory */
	knp = uma_zalloc(kvfs_zone_node, M_WAITOK | M_ZERO);
	knp->mp = kvfsmp;
	knp->vp = node;
	node->v_data = knp;

	/* read or create an inode on disk, but only if the inode requested is
	 * not the root -- the root does not exist on disk. */
	if (ino != KVFS_ROOT_INO) {
		knp->ino = ino;
		/* in kvfs, blocks and inodes are 1:1, so any block index is the
		 * same as any inode index.
		 * we store here the logical block number of the data on disk */
		knp->lbn = btodb(kvfsmp->data_off + INO_TO_BLOCKNUM(ino));
		printf("stored logical block number %zd\n", knp->lbn);

		/* read a block of inodes on disk */
		/* read the entire sector that has our inode contained in it */
		daddr_t loc = kvfsmp->inode_off + ino;
		bread(kvfsmp->devvp, btodb(loc), DEV_BSIZE, NOCRED, &bp);
		printf("  reading inode\n");
		/* read inode contents from the block */
		caddr_t buf_ptr = bp->b_data + (ino % DEV_BSIZE);
		memcpy(&knp->inode, buf_ptr, sizeof(struct kvfs_inode));

		/* if inode was previously free, we can allocate it */
		if (knp->inode.flags & KVFS_INODE_FREE) {
			/* vfs_vget will call us with NULL keystr if just trying
			 * to access a vnode, not necessary create one. the
			 * contract is that we don't try to VGET an inode that
			 * is free, since it doesn't make sense to try and
			 * allocate with a NULL keystr. */
			if (keystr == NULL) {
				printf(
				    "Error: tried to create vnode with NULL key\n");
				error = ENOENT;
				goto error_exit;
			}

			printf("  allocating new inode\n");
			/* update inode fields */
			knp->inode.ref_count = 1;
			knp->inode.flags &= ~KVFS_INODE_FREE;
			knp->inode.flags |= KVFS_INODE_ACTIVE;

			if (str_to_key(keystr, knp->inode.key) != 0) {
				/* str_to_key will fail if name is invalid */
				error = EINVAL;
				goto error_exit;
			}
			struct timespec ts;
			vfs_timestamp(&ts);
			knp->inode.timestamp = timespec_to_uint64(&ts);

			/* copy allocated inode back into buffer */
			memcpy(buf_ptr, &knp->inode, sizeof(struct kvfs_inode));
			bwrite(bp);
		} else {
			/* inode exists on disk, so we don't need to allocate it
			 */
			brelse(bp);
		}

	} else {
		/* root inode is virtual only, does not exist on disk */
		knp->ino = -1;
		knp->lbn = -1;
	}

	/* allow sharing of the lock */
	VN_LOCK_ASHARE(node);

	/* set flags */
	if (ino == KVFS_ROOT_INO) {
		node->v_vflag |= VV_ROOT;
		node->v_type = VDIR;
	} else {
		node->v_type = VREG;
	}

	*vpp = node;
	return (0);

error_exit:
	if (bp != NULL)
		brelse(bp);
	if (knp != NULL)
		uma_zfree(kvfs_zone_node, knp);
	node->v_data = NULL;
	*vpp = NULL;
	vfs_hash_remove(node);
	vput(node);
	return (error);
}

static struct vfsops kvfs_vfsops = {
	.vfs_init = kvfs_init,
	.vfs_uninit = kvfs_uninit,
	.vfs_mount = kvfs_mount,
	.vfs_unmount = kvfs_unmount,
	.vfs_root = kvfs_root,
	.vfs_statfs = kvfs_statfs,
	.vfs_sync = kvfs_sync,
	.vfs_vget = kvfs_vget,
};

VFS_SET(kvfs_vfsops, kvfs, 0);
