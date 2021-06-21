#ifndef KVFS_H
#define KVFS_H

#ifdef _KERNEL
#include <sys/types.h>
#else /* ! _KERNEL */
#include <stdint.h>
#endif /* _KERNEL */

/* Blocks in KVFS are always 4096KiB in size */
#define KVFS_BLOCKSIZE 4096

/* Keys in kvfs are always 40 characters long */
#define KVFS_KEY_STRLEN 40

/* KVFS inode flags */
#define KVFS_INODE_ACTIVE 0x0001
#define KVFS_INODE_FREE 0x0002

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

/* ==================
 * Kernel Helper Functions
 * ================== */

/* Convert 40-digit string to 160-bit key */
int str_to_key(const char *str, uint8_t *out_key);

/* Convert 160-bit key to 40-digit string */
int key_to_str(const uint8_t *key, char *out_str);

/* unpack a packed uint64_t nanosecond epoch into timespec */
void uint64_to_timespec(uint64_t packed, struct timespec *ts);

/* pack timespec into uint64_t nanosecond epoch */
uint64_t timespec_to_uint64(struct timespec *ts);

/* hash a block with sha1 */
int hash_block(uint8_t result[20], void *buf, size_t size);

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
