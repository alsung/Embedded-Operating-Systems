#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/mount.h>
#include <sys/vnode.h>

#include <ufs/ufs/quota.h>
#include "ddfs_inode.h"
#include <ufs/ufs/ufs_extern.h>
#include <ufs/ufs/extattr.h>
#include <ufs/ufs/ufsmount.h>
#include "ddfs_fs.h"
#include <ufs/ffs/ffs_extern.h>

#include <crypto/sha1.h>

#include "ddfs.h"

/* Copied from /usr/src/sys/fs/nfsserver/nfs_nfsdsubs.c
 * Translate an ASCII hex digit to it's binary value (between 0x0 and 0xf).
 * Return -1 if the char isn't a hex digit.
 */
static int8_t
hexdigit(char c)
{
	if (c >= '0' && c <= '9')
		return (c - '0');
	if (c >= 'a' && c <= 'f')
		return (c - 'a' + ((char)10));
	if (c >= 'A' && c <= 'F')
		return (c - 'A' + ((char)10));

	return (-1);
}

/* Convert a hex digit (4 bit nibble) into an ASCII character.
 * https://stackoverflow.com/a/45233496/714416 */
static char
digithex(int8_t digit)
{
	if (digit <= 9) {
		return ('0' + digit);
	} else {
		return ('a' + digit - ((char)10));
	}
}

/* Convert 40-digit string to 160-bit key.
 * Can fail if the passed string is not hexidecimal, in which case
 * it will return 1. Otherwise, return 0*/
int
str_to_key(const char *str, uint8_t *out_key)
{
	/* first have to zero out the key */
	bzero(out_key, 20);
	for (int i = 0; i < 40; i++) {
		int8_t d = hexdigit(str[i]);
		if (d == -1) {
			return 1;
		}
		uint8_t shift = (i % 2 == 0) ? 4 : 0;
		out_key[i / 2] |= (d << shift);
	}
	return (0);
}

/* Convert 160-bit key to 40-digit hex string.
 * **Assumes out_str is at least 41 characters long**,
 * so the string can be null terminated
 * https://stackoverflow.com/a/45233496/714416 */
int
key_to_str(const uint8_t *key, char *out_str)
{
	int len = 20; /* 160 bits == 20 8-bit bytes */
	while (len--) {
		*(out_str++) = digithex(*key >> 4);
		*(out_str++) = digithex(*key & 0x0f);
		key++; /* move to next byte */
	}
	// null terminate string
	*out_str = '\0';
	return (0);
}

int
hash_block(uint8_t hash_result[SHA1_RESULTLEN], void *buf, size_t size)
{
	SHA1_CTX ctx = { 0 };
	sha1_init(&ctx);
	sha1_loop(&ctx, buf, size);
	sha1_result(&ctx, hash_result);
	return 0;
}

/*
 * Helper function for ddtable_alloc and ddtable_unref.
 * Finds a dedup table entry with matching `key` or `targetblock`, whichever is non-null.
 *
 * Sets the `out_idx` to the location of the entry in the buffer pointed to by `bp`.
 * If `first_free` is non-null, will set to the location of the first free space in the buffer
 * pointed to by `freebp`.
 * Note that this is *not necesscarily* the same buffer as `bp`.
 * The caller must be careful to avoid a double free when releasing these buffers.
 *
 * Returns 0 on successful find, and 1 if not found.
 * The caller is responsible for freeing the buffer `bp` with `bwrite()` or `brelse()`.
 */
static int
ddtable_locate(struct ufsmount *mnt, uint8_t key[20], struct buf **bp, daddr_t *out_idx, daddr_t *first_free, struct buf **freebp, daddr_t *targetblock)
{
	struct fs *fs = mnt->um_fs;
	int error = 0;
	daddr_t freespot = -1;

	const uint64_t num_blocks = fs->fs_dedupfrags / fs->fs_frag;
	const uint64_t entries_per_block = fs->fs_bsize / sizeof(struct ddfs_dedup);
	/* helper variable to avoid freeing the buffer for the free block */
	bool found;
	for (int i = 0; i < num_blocks; i++) {
		found = false;
		daddr_t dd_lbn = fsbtodb(fs, fs->fs_ddblkno + i * fs->fs_frag);
		error = bread(mnt->um_devvp, dd_lbn, fs->fs_bsize, NOCRED, bp);
		if (error != 0) {
			printf("  bread error %d\n", error);
			return (error);
		}
		/* read through each entry in this block */
		struct ddfs_dedup entry;
		for (int k = 0; k < entries_per_block; k++) {
			daddr_t idx = k * sizeof(struct ddfs_dedup);
			memcpy(&entry, (*bp)->b_data + idx, sizeof(struct ddfs_dedup));
			if (entry.flags & DDFS_DEDUP_FREE) {
				if (freespot == -1 && freebp != NULL) {
					/* mark the first free spot we find,
					 * in case we need to allocate a new entry */
					freespot = idx;
					*freebp = *bp;
					found = true;
				}
				continue;
			}

			if (targetblock != NULL) {
				if (entry.blockptr == *targetblock) {
					*out_idx = idx;
					goto out;
				}

			} else if (key != NULL) {
				if (memcmp(key, entry.key, 20) == 0) {
					/* found matching entry on disk. */
					*out_idx = idx;
					error = 0;
					goto out;
				}
			}
		}
		/*
		 * If we didn't find anything AND we didn't
		 * find a free spot, release the buffer.
		 */
		if (!found) {
			brelse(*bp);
		}
	}
	/* didn't find the entry */
	error = 1;
out:
	/* if caller requested free spot, give it to them */
	if (first_free != NULL) {
		*first_free = freespot;
	}
	return (error);
}

/*
 * Allocate a free space in the ddtable, or increment an existing key if found
 */
int ddtable_alloc(struct ufsmount *mnt, uint8_t key[20], daddr_t in_block,
		daddr_t *out_block)
{
	daddr_t block_idx, freespot;
	struct buf *bp, *freebp;
	struct ddfs_dedup entry;

	int notfound = ddtable_locate(mnt, key, &bp, &block_idx, &freespot, &freebp, NULL);
	if (notfound) {
		printf("ddtable_alloc: allocating a new entry with block pointer %zu\n", in_block);
		/* didn't find a match in the table, so allocate a new entry at the spot we saved */
		entry.flags |= DDFS_DEDUP_ACTIVE;
		entry.ref_count = 1;
		entry.blockptr = in_block;
		memcpy(entry.key, key, 20);
		/* write the new entry to disk */
		memcpy(freebp->b_data + freespot, &entry, sizeof(struct ddfs_dedup));
		bwrite(freebp);
		/* allocated a new entry, so out bptr == in bptr */
		*out_block = entry.blockptr;
	} else {
		/* found a match. update refcount */
		memcpy(&entry, bp->b_data + block_idx, sizeof(struct ddfs_dedup));
		printf("ddtable_alloc: incrementing ref count on bno %zu\n", entry.blockptr);
		entry.ref_count++;
		*out_block = entry.blockptr;
		/* update entry on disk */
		memcpy(bp->b_data + block_idx, &entry, sizeof(struct ddfs_dedup));
		bwrite(bp);
	}
	return (0);
}

/*
 * Decrement a key-value pair in the ddtable.
 * Removes the entry from the table if refcount == 0.
 * Returns the updated refcount of the block, or -1 if not found.
 * If the refcount is 0, the caller is responsible for removing the block.
 */
int ddtable_unref(struct ufsmount *mnt, daddr_t blocknum)
{
	daddr_t block_idx;
	struct buf *bp;
	struct ddfs_dedup entry;

	int notfound = ddtable_locate(mnt, NULL, &bp, &block_idx, NULL, NULL, &blocknum);
	if (notfound)
		return (-1);

	/* found a match. update refcount */
	memcpy(&entry, bp->b_data + block_idx, sizeof(struct ddfs_dedup));
	if (--entry.ref_count == 0) {
		/* ref count is now 0, so we delete this entry */
		bzero(&entry, sizeof(struct ddfs_dedup));
		entry.flags = DDFS_DEDUP_FREE;
	}
	/* update entry on disk */
	memcpy(bp->b_data + block_idx, &entry, sizeof(struct ddfs_dedup));
	bwrite(bp);

	return (entry.ref_count);
}
