#ifndef DDFS_H
#define DDFS_H

#ifdef _KERNEL
#include <sys/types.h>
#else /* ! _KERNEL */
#include <stdint.h>
#endif /* _KERNEL */

/* Each deduplicated block is always 4k */
#define DDFS_BLOCKSIZE 4096

/* Keys in kvfs are always 40 characters long */
#define DDFS_KEY_STRLEN 40

/* KVFS inode flags */
#define DDFS_DEDUP_FREE 0x0001
#define DDFS_DEDUP_ACTIVE 0x0010

/* 
 * On-disk representation of a ddfs dedup table entry.
 * Contains a key, ref count, and block pointer.
 * The reference count is incremented when a 4k fragment hashes to the key in this entry,
 * and decremented when the old hash does not match the new hash.
 * When the reference count reaches 0, this entry is freed.
 */
struct __attribute__((packed)) ddfs_dedup {
	uint8_t key[20];    /* 160 bit key */
	uint16_t flags;	    /* flags. one of FREE | ACTIVE */
	uint16_t ref_count; /* reference count*/
	daddr_t blockptr;	/* block pointer for this key-value pair */
};

#ifdef _KERNEL

/* ==================
 * Kernel Helper Functions
 * ================== */

/* Convert 40-digit string to 160-bit key */
int str_to_key(const char *str, uint8_t *out_key);

/* Convert 160-bit key to 40-digit string */
int key_to_str(const uint8_t *key, char *out_str);

/* hash a block with sha1 */
int hash_block(uint8_t result[20], void *buf, size_t size);

/* ==================
 * Kernel Dedup Functions
 * ================== */
struct ufsmount;

/* allocate a free space in the ddtable, or increment an existing key if found */
int ddtable_alloc(struct ufsmount *mnt, uint8_t key[20], daddr_t in_block, daddr_t *out_block);

/* decrement a key-value pair in the ddtable. removes the key-value pair if refcount == 0 */
int ddtable_unref(struct ufsmount *mnt, daddr_t blocknum);

#endif /* _KERNEL */

# endif /* ! DDFS_H */
