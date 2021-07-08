#ifndef KVFS_H
#define KVFS_H

#ifdef _KERNEL
#include <sys/types.h>
#include <sys/malloc.h>

#include <vm/uma.h>
#else /* ! _KERNEL */
#include <stdint.h>
#endif /* _KERNEL */

/* Blocks in KVFS are always 4096KiB in size */
#define BLOCKSIZE 4096

/* Keys in kvfs are always 40 characters long */
#define KVFS_KEY_STRLEN 40

/* Magic number for kvfs superblock.
 * Used to tell if a mounted filesystem is actually kvfs */
#define KVFS_SUPERBLOCK_MAGIC 0x666F

/* Because kvfs is completely flat, the root inode is a "virtual" inode,
 * it does not exist on disk at all.
 * We support a maximum of 2**30 blocks, 
 * so any inode number >= 0x800000000 is invalid. */
#define KVFS_ROOT_INO 0x800000008

/* KVFS inode flags */
#define KVFS_INODE_ACTIVE 0x0001
#define KVFS_INODE_FREE 0x0002

/* kvfs superblock */
struct __attribute__((packed)) kvfs_superblock {
	uint16_t magicnum;	  /* kvfs magic number */
	uint16_t superblock_size; /* size of superblock on disk */
	off_t freelist_off;	  /* block offset of free list */
	off_t inode_off;	  /* block offset of inode allocation table */
	off_t data_off;		  /* block offset of data blocks */
	/* supports a maximum of 2^30 blocks */
	uint32_t block_count; /* number of data blocks in this filesystem. */

	uint64_t flags;	  /* filesystem flags */
	uint64_t fs_size; /* actual filesystem size */
};

/* kvfs inode. On-disk representation of a file.  */
struct __attribute__((packed)) kvfs_inode {
	uint8_t key[20];    /* 160 bit key */
	uint16_t flags;	    /* inode flags */
	uint16_t ref_count; /* reference count. currently always 1 */
	uint64_t timestamp; /* modification time in nanoseconds */
};

/* ==================
 * Kernel-only structures
 * ================== */
#ifdef _KERNEL

extern struct vop_vector kvfs_vnodeops;
extern uma_zone_t kvfs_zone_node;

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_KVFSFREE);
#endif

extern uma_zone_t kvfs_zone_node;

/* kvfs inode in-memory representation */
struct kvfs_memnode {
	struct kvfs_mount *mp;
	struct vnode *vp; /* pointer to associated vnode */
	ino_t ino;	  /* index of kvfs_inode on disk */
	daddr_t lbn;  /* logical block number of "data" value on disk. */

	struct kvfs_inode inode; /* fields in kvfs inode */
};

/* represents a free key-value (inode, block) pair */
struct kvfs_freelist_entry {
	ino_t ino;	       /* index of this inode on disk */
	LIST_ENTRY(kvfs_freelist_entry) entries;
};

/* Convert between vnode and memnode pointers*/
#define VTOM(vp) ((struct kvfs_memnode *)(vp)->v_data)
#define MTOV(ip) ((ip)->vp)

/* kvfs mount structure, holds data about the mounted filesystem */
struct kvfs_mount {
	struct mount *vfs;     /* vfs mount struct for this fs */
	struct g_consumer *cp; /* GEOM layer characver device consumer */
	struct vnode *devvp;   /* vnode for character device mounted */
	struct cdev *cdev;     /* character device mounted */

	off_t freelist_off;   /* data offset of freelist bitmap */
	off_t inode_off;      /* data offset of inode allocation table */
	off_t data_off;	      /* data offset of data blocks */
	uint32_t block_count; /* number of data blocks in this filesystem. */
	uint64_t flags;

	LIST_HEAD(freelist_head, kvfs_freelist_entry) freelist_head;
	uint32_t freelist_count; /* number of free blocks */
};

/* ==================
 * Kernel Helper Functions
 * ================== */

/* Convert 40-digit string to 160-bit key */
int str_to_key(const char *str, uint8_t *out_key);

/* Convert 160-bit key to 40-digit string */
int key_to_str(const uint8_t *key, char *out_str);

/* get a locked vnode associated with inode ino. If inode does not exist
 * already, we allocate it */
int kvfs_vget_internal(struct mount *mp, ino_t ino, int flags,
    struct vnode **vpp, const char *key);

/* unpack a packed uint64_t nanosecond epoch into timespec */
void uint64_to_timespec(uint64_t packed, struct timespec *ts);

/* pack timespec into uint64_t nanosecond epoch */
uint64_t timespec_to_uint64(struct timespec *ts);

/* convert ino to block index */
#define INO_TO_BLOCKNUM(ino) (((ino) / sizeof(struct kvfs_inode)) * BLOCKSIZE)

/* convert ino to free block byte offset */
#define INO_TO_FREE_BYTE(ino, mp) \
	(((ino) / sizeof(struct kvfs_inode) / 8) + mp->freelist_off)

/* convert ino to mask containing free bit sit*/
#define INO_TO_FREE_BIT_MASK(ino) (1 << ((ino) / sizeof(struct kvfs_inode) % 8))

#endif /* _KERNEL */

/* ==================
 * Global Helper Macros
 * ================== */

/* ceiling of a/b without using floats */
#define CEIL(a, b) (((a) + (b)-1) / (b))

/* pad byte_count to the next multiple of BLOCKSIZE,
 * returning the number of bytes in the next multiple of BLOCKSIZE */
#define PAD(bc) (CEIL((bc), BLOCKSIZE) * BLOCKSIZE)

#endif /* ! KVFS_H */
