/*
 * vnode operations for kvfs
 * */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/dirent.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>

#include "kvfs.h"

/* prototypes for kvfs vnode ops */
static vop_lookup_t kvfs_lookup;
static vop_create_t kvfs_create;
static vop_open_t kvfs_open;
static vop_close_t kvfs_close;
static vop_access_t kvfs_access;
static vop_getattr_t kvfs_getattr;
static vop_setattr_t kvfs_setattr;
static vop_read_t kvfs_read;
static vop_write_t kvfs_write;
static vop_fsync_t kvfs_fsync;
static vop_remove_t kvfs_remove;
static vop_rename_t kvfs_rename;
static vop_readdir_t kvfs_readdir;
static vop_inactive_t kvfs_inactive;
static vop_reclaim_t kvfs_reclaim;
static vop_strategy_t kvfs_strategy;

/* update an inode on-disk.
 * @param inode should be the same as the inode stored in @param knode,
 * but it can be different if you want to write out an 'empty' inode. */
static int
memnode_update(struct kvfs_memnode *knode, struct kvfs_inode *inode)
{
	struct kvfs_mount *mp = knode->mp;
	ino_t ino = knode->ino;

	struct buf *bp;
	daddr_t loc = mp->inode_off + ino;
	int error = bread(mp->devvp, btodb(loc), DEV_BSIZE, NOCRED, &bp);
	if (error != 0) {
		return (error);
	}

	caddr_t ptr = bp->b_data + (ino % DEV_BSIZE);
	memcpy(ptr, inode, sizeof(struct kvfs_inode));
	return (bwrite(bp));
}

static int
kvfs_lookup(struct vop_lookup_args *ap)
{
	/* the locked vnode of the directory to search */
	struct vnode *vdp = ap->a_dvp;
	/* the vnode of the lookup, if successful */
	struct vnode **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;

	struct kvfs_memnode *knode = vdp->v_data;
	struct kvfs_mount *kvfsmp = knode->mp;
	int error = 0;

	printf("kvfs_lookup -- passed cn_nameiop: %d\n", cnp->cn_nameiop);

	if ((vdp->v_vflag & VV_ROOT) == 0) {
		/* no other directories should exist in kvfs */
		return (ENOENT);
	}

	/* are they going after `.` ? */
	if (cnp->cn_nameptr[0] == '.' && cnp->cn_namelen == 1) {
		/* caller wanted `.`, increment ref count on directory
		 * vnode */
		vref(vdp);
		*vpp = vdp;
		return (0);
	}
	/* XXX Don't need to implement lookup on `..`, as the layers above us will
	 * handle cross-filesystem lookup when `vdp` is the filesystem root. */

	/* check that key is valid */
	if (cnp->cn_namelen != KVFS_KEY_STRLEN) {
		return (EINVAL);
	}
	uint8_t key[20];
	error = str_to_key(cnp->cn_nameptr, key);
	if (error != 0) {
		return (EINVAL);
	}

	printf("  bread\n");
	/* bread() every single inode from disk */
	struct buf *bp;
	error = bread(kvfsmp->devvp, btodb(kvfsmp->inode_off),
	    PAD(kvfsmp->block_count * sizeof(struct kvfs_inode)), NOCRED, &bp);
	if (error != 0) {
		return error;
	}

	/* go through each inode on disk */
	printf("  look through inodes\n");
	struct kvfs_inode inode;
	for (int i = 0; i < kvfsmp->block_count; i++) {
		ino_t idx = i * sizeof(struct kvfs_inode);
		memcpy(&inode, bp->b_data + idx, sizeof(struct kvfs_inode));
		/* if this inode is free we just skip it */
		if (inode.flags & KVFS_INODE_FREE) {
			continue;
		}
		if (memcmp(key, inode.key, 20) == 0) {
			/* found the inode for our file, get the locked vnode
			   associated with it. */
			brelse(bp);
			int error = VFS_VGET(vdp->v_mount, idx, cnp->cn_lkflags,
			    vpp);
			if (error != 0) {
				printf("  got vget error: %d\n", error);
				return (error);
			}
			goto found;
		}
	}

	/* not found */
	brelse(bp);
	/* special case: as per VOP_LOOKUP(9),
	 * if operation is CREATE or RENAME, we return EJUSTRETURN */
	if ((cnp->cn_flags & ISLASTCN) &&
	    (cnp->cn_nameiop == CREATE || cnp->cn_nameiop == RENAME)) {
		return (EJUSTRETURN);
	}
	return (ENOENT);

found:
	printf("  found inode\n");
	return (0);
}

/* Creating a file.
 * Done in "soft update" order:
 *	1. pop entry from free list
 *	2. update free bitmap
 *	3. allocate vnode and inode
 *	4. write inode to disk
 *	*/
static int
kvfs_create(struct vop_create_args *ap)
{
	printf("kvfs_create\n");
	struct vnode *vdp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	struct kvfs_memnode *knode = vdp->v_data;
	struct kvfs_mount *mp = knode->mp;
	struct componentname *cnp = ap->a_cnp;

	int error;

	struct kvfs_freelist_entry *entry = LIST_FIRST(&mp->freelist_head);
	if (entry == NULL) {
		*vpp = NULL;
		return (ENOSPC);
	}
	ino_t ino = entry->ino;
	off_t free_byte = INO_TO_FREE_BYTE(ino, mp);
	uint8_t mask = INO_TO_FREE_BIT_MASK(ino);

	printf("found inode in free list: %zu, at freelist byte %zu/lbn %zu, with mask %d\n",
	    ino, free_byte, btodb(free_byte), mask);
	/* pop entry from free list */
	LIST_REMOVE(entry, entries);
	free(entry, M_KVFSFREE);
	mp->freelist_count--;

	/* update free bitmap to mark that we now own this inode */
	struct buf *bp;
	error = bread(mp->devvp, btodb(free_byte), DEV_BSIZE, NOCRED, &bp);
	if (error != 0) {
		return 0;
	}
	/* set the bit corresponding to the newly allocated block in the on-disk
	 * free list */
	bp->b_data[free_byte % DEV_BSIZE] |= mask;
	bwrite(bp);

	/* allocate inode and vnode. This routine also writes the inode to a
	 * buf, which should be flushed to filesystem. */
	error = kvfs_vget_internal(vdp->v_mount, ino, LK_EXCLUSIVE, vpp,
	    cnp->cn_nameptr);
	if (error) {
		return (error);
	}

	return (0);
}

static int
kvfs_open(struct vop_open_args *ap)
{
	printf("kvfs_open\n");
	return (0);
}

static int
kvfs_close(struct vop_close_args *ap)
{
	printf("kvfs_close\n");
	return (0);
}

static int
kvfs_access(struct vop_access_args *ap)
{
	printf("kvfs_access\n");
	/* ACCESS always returns that we are successful, kvfs has no permissions
	 */
	return (0);
}

static int
kvfs_getattr(struct vop_getattr_args *ap)
{
	printf("kvfs_getattr\n");

	// Get pointers
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;
	struct kvfs_memnode *mnp = VTOM(vp);

	VATTR_NULL(vap);

	if (vp->v_vflag & VV_ROOT) {
		printf("  attr: root\n");
		vap->va_type = VDIR;
		vap->va_fileid = KVFS_ROOT_INO;
		vap->va_size = 0;
		vap->va_bytes = 0;
	} else {
		printf(" attr: file\n");
		vap->va_type = VREG;
		vap->va_fileid = mnp->ino;
		vap->va_size = (u_quad_t)BLOCKSIZE;
		vap->va_bytes = (u_quad_t)BLOCKSIZE; /* 4KiB held on disk */
		/* set mtime back from packed to timespec */
		uint64_to_timespec(mnp->inode.timestamp, &vap->va_mtime);
	}

	vap->va_blocksize = BLOCKSIZE;	     // 4KiB size hardcoded
	vap->va_fsid = dev2udev(mnp->mp->cdev);
	vap->va_uid = 0;
	vap->va_gid = 0;
	vap->va_mode = 0777; /* no permissions */
	vap->va_flags = 0;
	vap->va_gen = 1;
	vap->va_nlink = 1; /* each kv pair has only one reference; itself */
	vap->va_filerev = 1;

	return (0);
}

static int
kvfs_setattr(struct vop_setattr_args *ap)
{
	printf("kvfs_setattr\n");

	struct vattr *vap = ap->a_vap;
	struct vnode *vp = ap->a_vp;
	struct kvfs_memnode *knode = VTOM(vp);

	/* root vnode has no metadata we can update */
	if (vp->v_vflag & VV_ROOT) {
		return (0);
	}

	// Update modification timestamp
	if (vap->va_mtime.tv_sec != VNOVAL) {
		knode->inode.timestamp = timespec_to_uint64(&vap->va_mtime);
		memnode_update(knode, &knode->inode);
	}

	return (0);
}

/* based on:
 * https://netbsd.org/docs/internals/en/chap-file-system.html: 2.11.5.4
 */
static int
kvfs_read(struct vop_read_args *ap)
{
	printf("kvfs_read\n");
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct kvfs_memnode *knode = vp->v_data;
	struct kvfs_mount *mp = knode->mp;

	if (vp->v_type != VREG) {
		return (EISDIR);
	}

	if (uio->uio_offset < 0 ) {
		return (EINVAL);
	}

	/* If they requested no data, or if they have gotten everything, we are done */
	if (uio->uio_resid == 0 || uio->uio_offset >= BLOCKSIZE) {
		return (0);
	}

	/* read block associated with this file */
	struct buf *bp;
	int error = bread(mp->devvp, knode->lbn, BLOCKSIZE, NOCRED, &bp);

	/* copy as much as we can to the user */
	int amt = uio->uio_resid;
	while (uio->uio_resid > 0) {
		amt = min(amt, BLOCKSIZE - bp->b_resid);
		error = uiomove(bp->b_data, amt, uio);
		if (error != 0) {
			break;
		}
	}
	brelse(bp);
	return (error);
}

static int
kvfs_write(struct vop_write_args *ap)
{
	printf("kvfs_write\n");
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct kvfs_memnode *knode = vp->v_data;
	struct kvfs_mount *mp = knode->mp;

	int error;
	int ioflag = ap->a_ioflag;

	if (vp->v_type != VREG) {
		return (EISDIR);
	}

	/* if they wrote no data, we are done */
	if (uio->uio_resid == 0) {
		return (0);
	}

	/* don't support append, since file size is fixed
	 * at BLOCKSIZE and cannot grow */
	if (ioflag & IO_APPEND) {
		return (EINVAL);
	}

	/* first read the block associated with this file */
	struct buf *bp;
	error = bread(mp->devvp, knode->lbn, BLOCKSIZE, NOCRED, &bp);
	if (error != 0) {
		return (error);
	}

	/* copy as much as we can from the user */
	int amt = uio->uio_resid;
	while (uio->uio_resid > 0) {
		amt = min(amt, BLOCKSIZE - bp->b_resid);
		error = uiomove(bp->b_data + uio->uio_offset, amt, uio);
		if (error != 0) {
			brelse(bp);
			printf("Error: uiomove failed with code %d\n", error);
			return (error);
		}
	}
	/* write back the block */
	bwrite(bp);
	return (0);
}

static int
kvfs_fsync(struct vop_fsync_args *ap)
{
	printf("kvfs_fsync\n");
	/* TODO: sync and fsync are really broken */
#if 0
	// Doing it how Ext2 does it
	vop_stdfsync(ap);
	struct kvfs_memnode *knp = ap->a_vp->v_data;

	int error;
	if (ap->a_waitfor != MNT_NOWAIT) {
		struct vnode *devvp = knp->mp->devvp;
		vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
		error = VOP_FSYNC(devvp, MNT_WAIT, ap->a_td);
		VOP_UNLOCK(devvp);
	} else {
		error = 0;
	}
	/* TODO try to sync inode? */
#endif
	return (0);
}

/* Remove a file.
 * Synchronously zeroes out the data block and inode.
 * Performed in a "soft update" manner:
 *	1. zero out inode
 *	2. zero out data blocks
 *	3. add block to free list
 */
static int
kvfs_remove(struct vop_remove_args *ap)
{
	printf("kvfs_remove\n");
	struct vnode *vp = ap->a_vp;
	struct kvfs_memnode *knode = vp->v_data;
	struct kvfs_mount *mp = knode->mp;
	ino_t ino = knode->ino;
	int error = 0;

	/* write an empty inode to this file */
	struct kvfs_inode empty = { 0 };
	empty.flags |= KVFS_INODE_FREE;
	memnode_update(knode, &empty);

	/* zero out data block */
	struct buf *bp;
	bp = getblk(mp->devvp, knode->lbn, BLOCKSIZE, 0, 0, 0);
	bzero(bp->b_data, BLOCKSIZE);
	bwrite(bp);

	/* add (inode, block) to filesystem free list */
	uint32_t free_byte = INO_TO_FREE_BYTE(ino, mp);
	uint32_t mask = INO_TO_FREE_BIT_MASK(ino);
	struct kvfs_freelist_entry *e =
		malloc(sizeof(struct kvfs_freelist_entry),
		M_KVFSFREE, M_WAITOK);
	e->ino = ino;
	LIST_INSERT_HEAD(&mp->freelist_head, e, entries);
	mp->freelist_count++;

	/* update free bitmap to mark that inode is free now own this inode */
	error = bread(mp->devvp, btodb(free_byte), DEV_BSIZE, NOCRED, &bp);
	if (error != 0) {
		return 0;
	}
	/* free the bit corresponding to the freed block in the on-disk
	 * free list */
	bp->b_data[free_byte % DEV_BSIZE] &= ~mask;
	bwrite(bp);

	/* XXX remove vnode from hash, so if the file is created again,
	 * it will be re-allocated. */
	vfs_hash_remove(vp);

	return (0);
}

/* rename a key, by moving the key to a different name.
 * this may possibly delete the old name.
 *
 * according to VOP_RENAME(9), before returning we must:
 *	- vrele(9) the source dir and file
 *	- vput(9) the target dir and file
 * */
static int
kvfs_rename(struct vop_rename_args *ap)
{
	struct vnode *tvp = ap->a_tvp;
	struct vnode *tdvp = ap->a_tdvp;
	struct vnode *fvp = ap->a_fvp;
	struct vnode *fdvp = ap->a_fdvp;
	struct componentname *tcnp = ap->a_tcnp;
	struct componentname *fcnp = ap->a_fcnp;

	struct kvfs_memnode *from = fvp->v_data;

	printf("kvfs_rename: %s -> %s\n", fcnp->cn_nameptr, tcnp->cn_nameptr);
	int error;

	/*
	 * Check for cross-device rename.
	 * Copied from /usr/from/sys/fs/ext2fs/ext2_vnops.c::ext2_rename
	 */
	if ((fvp->v_mount != tdvp->v_mount) ||
	    (tvp && (fvp->v_mount != tvp->v_mount))) {
		error = EXDEV;
	}

	/* Check that the requested name is valid. */
	uint8_t testkey[20];
	error = str_to_key(tcnp->cn_nameptr, testkey);
	if (error != 0) {
		error = EINVAL;
		goto out;
	}

	/* If dest file exists, remove it. */
	if (tvp != NULL && tvp->v_data != NULL) {
		VOP_REMOVE(tdvp, tvp, tcnp);
	}

	/* Overwrite name entry in "from" file with new name */
	if ((error = vn_lock(fvp, LK_EXCLUSIVE)) != 0) {
		goto out;
	}
	memcpy(from->inode.key, testkey, 20 * sizeof(uint8_t));

	/* Update inode on disk */
	memnode_update(from, &from->inode);

	/* Unlock source file */
	VOP_UNLOCK(fvp);

out:
	if (tdvp == tvp)
		vrele(tdvp);
	else
		vput(tdvp);
	if (tvp)
		vput(tvp);
	vrele(fdvp);
	vrele(fvp);
	return (error);
}

/*
 * Write out a single 'struct dirent', based on 'name' and 'fileno' arguments.
 * Copied from /usr/sys/fs/autofs/autofs_vnops.c::autofs_readdir_one
 */
static int
kvfs_readdir_one(struct uio *uio, const char *name, ino_t fileno,
    size_t *reclenp, uint8_t type)
{
	printf("  readdir_one: %s\n", name);
	struct dirent dirent;
	int error;

	size_t len = strlen(name);
	size_t reclen = _GENERIC_DIRLEN(len);
	if (reclenp != NULL)
		*reclenp = reclen;

	if (uio == NULL)
		return (0);

	if (uio->uio_resid < reclen)
		return (EINVAL);

	dirent.d_fileno = fileno;
	dirent.d_off = uio->uio_offset + reclen;
	dirent.d_reclen = reclen;
	dirent.d_type = type;
	dirent.d_namlen = len;
	memcpy(dirent.d_name, name, len);
	dirent_terminate(&dirent);
	error = uiomove(&dirent, reclen, uio);

	return (error);
}

/* Find the size of a single direntry, to check if it has already
 * been written to dirbuf.
 * Copied from /usr/src/sys/fs/autofs/autofs_vnops.c::autofs_readdir_reclen
 */
static size_t
kvfs_dirent_reclen(const char *name, uint8_t type)
{
	size_t reclen;

	(void)kvfs_readdir_one(NULL, name, -1, &reclen, type);

	return (reclen);
}

/* Read out the contents of nodes on disk to a struct dirbuf, and uiomove
 * that.
 * Heavily inspired by /usr/src/sys/fs/autofs/autofs_vnops.c::autofs_readdir
 */
static int
kvfs_readdir(struct vop_readdir_args *ap)
{
	printf("kvfs_readdir\n");

	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct kvfs_memnode *knode = vp->v_data;
	struct kvfs_mount *kvfsmp = knode->mp;
	size_t initial_resid = uio->uio_resid;
	size_t reclen, reclens;
	int error = 0;
	struct buf *bp = NULL;

	/* ignore cookies and eofflag, we don't support NFS */
	ap->a_cookies = NULL;
	ap->a_ncookies = NULL;
	ap->a_eofflag = NULL;

	/* readdir not supported on anything other than root directory vnode */
	if ((vp->v_vflag & VV_ROOT) == 0 || (vp->v_type != VDIR)) {
		return (ENOTDIR);
	}

	if (uio->uio_offset < 0) {
		return (EINVAL);
	}

	/*
	 * Simulate . and .. entries.
	 * Write out the directory entry for ".".
	 */
	if (uio->uio_offset == 0) {
		/* entry has not been written yet */
		error = kvfs_readdir_one(uio, ".", KVFS_ROOT_INO, &reclen,
		    DT_DIR);
		if (error != 0)
			goto out;
	}
	reclens = kvfs_dirent_reclen(".", DT_DIR);

	/*
	 * Write out the directory entry for "..".
	 */
	if (uio->uio_offset <= reclens) {
		if (uio->uio_offset != reclens)
			return (EINVAL);
		/* we have only one directory, the root */
		error = kvfs_readdir_one(uio, "..", KVFS_ROOT_INO, &reclen,
		    DT_DIR);
		if (error != 0)
			goto out;
	}
	reclens += kvfs_dirent_reclen("..", DT_DIR);

	/* Now, we write the rest of the entries.
	 * bread() every single inode from disk */
	error = bread(kvfsmp->devvp, btodb(kvfsmp->inode_off),
	    PAD(kvfsmp->block_count * sizeof(struct kvfs_inode)), NOCRED, &bp);
	if (error != 0) {
		return error;
	}

	/* go through each inode */
	struct kvfs_inode inode;
	for (int i = 0; i < kvfsmp->block_count; i++) {
		ino_t ino = i * sizeof(struct kvfs_inode);
		memcpy(&inode, bp->b_data + ino, sizeof(struct kvfs_inode));
		/* if this inode is free we just skip it */
		if (inode.flags & KVFS_INODE_FREE) {
			continue;
		}
		/* add inode to dirbuf */
		char name[KVFS_KEY_STRLEN + 1];
		key_to_str(inode.key, name);
		/* Check the offset to skip entries returned by previous
		 * calls to getdents(2).  */
		if (uio->uio_offset > reclens) {
			reclens += kvfs_dirent_reclen(name, DT_REG);
			continue;
		}

		/* Prevent seeking into the middle of dirent.  */
		if (uio->uio_offset != reclens) {
			brelse(bp);
			return (EINVAL);
		}
		error = kvfs_readdir_one(uio, name, ino, &reclen, DT_REG);
		reclens += reclen;
		if (error != 0) {
			goto out;
		}
	}
	brelse(bp);
	return (0);

out:
	if (bp != NULL)
		brelse(bp);

	/* Return error if the initial buffer was too small to do anything.  */
	if (uio->uio_resid == initial_resid)
		return (error);

	/* Don't return an error if we managed to copy out some entries.  */
	if (uio->uio_resid < reclen)
		return (0);

	return (error);
}

/* called when vnode reference count reaches zero */
static int
kvfs_inactive(struct vop_inactive_args *ap)
{
	printf("kvfs_inactive\n");
	struct vnode *vp = ap->a_vp;
	struct kvfs_memnode *knp = vp->v_data;

	/* if file was deleted, we can recycle vnode */
	if (knp == NULL || knp->inode.flags & KVFS_INODE_FREE) {
		printf("  recycling vnode\n");
		vrecycle(vp);
	}
	return (0);
}

static int
kvfs_reclaim(struct vop_reclaim_args *ap)
{
	printf("kvfs_reclaim\n");
	struct vnode *vp = ap->a_vp;
	struct kvfs_memnode *knode = vp->v_data;

	if (knode != NULL) {
		vfs_hash_remove(vp);
		uma_zfree(kvfs_zone_node, knode);
		vp->v_data = NULL;
	}
	return (0);
}

/* called when an I/O operation is done on a vnode (like read() or write()) */
static int
kvfs_strategy(struct vop_strategy_args *ap)
{
	printf("!!! kvfs_strategy\n");
	struct buf *bp = ap->a_bp;
	struct kvfs_memnode *knode = ap->a_vp->v_data;
	struct bufobj *bo = &knode->mp->devvp->v_bufobj;

	/* find the filesystem relative block number for the inode */
	bp->b_blkno = dbtob(knode->lbn);
	BO_STRATEGY(bo, bp);
	return (0);
}

/* global vfs data structures for kvfs */
struct vop_vector kvfs_vnodeops = {
	.vop_default = &default_vnodeops,

	.vop_lookup = kvfs_lookup,
	.vop_create = kvfs_create,
	.vop_open = kvfs_open,
	.vop_close = kvfs_close,
	.vop_access = kvfs_access,
	.vop_getattr = kvfs_getattr,
	.vop_setattr = kvfs_setattr,
	.vop_read = kvfs_read,
	.vop_write = kvfs_write,
	.vop_fsync = kvfs_fsync,
	.vop_remove = kvfs_remove,
	.vop_rename = kvfs_rename,
	.vop_readdir = kvfs_readdir,
	.vop_inactive = kvfs_inactive,
	.vop_reclaim = kvfs_reclaim,
	.vop_strategy = kvfs_strategy,

	// not supported operations
	// can't have directories, hard links, symlinks, or fifos
	.vop_mkdir = VOP_EOPNOTSUPP,
	.vop_rmdir = VOP_EOPNOTSUPP,
	.vop_link = VOP_EOPNOTSUPP,
	.vop_symlink = VOP_EOPNOTSUPP,
	.vop_readlink = VOP_EOPNOTSUPP,
	.vop_mknod = VOP_EOPNOTSUPP,
};

VFS_VOP_VECTOR_REGISTER(kvfs_vnodeops);
