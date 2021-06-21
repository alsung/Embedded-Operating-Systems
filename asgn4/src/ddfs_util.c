#include <sys/param.h>
#include <sys/systm.h>
#include <crypto/sha1.h>

#include "ddfs.h"

#define NS_PER_SEC 1000000000ULL

/* Copied from /usr/src/sys/fs/nfsserver/nfs_nfsdsubs.c
 * Translate an ASCII hex digit to it's binary value (between 0x0 and 0xf).
 * Return -1 if the char isn't a hex digit.
 */
static int8_t
hexdigit(char c)
{
	if (c >= '0' && c <= '9')
		return (c - '0');
	if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
		return (c - 'a' + ((char)10));

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

/* unpack a packed uint64_t nanosecond epoch into timespec */
void
uint64_to_timespec(uint64_t packed, struct timespec *ts)
{
	ts->tv_sec = packed / NS_PER_SEC;
	ts->tv_nsec = packed - (ts->tv_sec * NS_PER_SEC);
}

/* pack timespec into uint64_t nanosecond epoch */
uint64_t
timespec_to_uint64(struct timespec *ts)
{
	return (ts->tv_sec * NS_PER_SEC + ts->tv_nsec);
}

int
hash_block(uint8_t hash_result[SHA1_RESULTLEN], void *buf, size_t size) 
{
	SHA1_CTX ctx = {0};
	sha1_init(&ctx);
	sha1_loop(&ctx, buf, size);
	sha1_result(&ctx, hash_result);
	return 0;
}

