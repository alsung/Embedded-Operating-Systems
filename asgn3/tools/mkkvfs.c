#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kvfs.h"

void
usage()
{
	printf("mkkvfs [-f device]\n");
	printf("-f device\t\tThe disk device to format\n");
}

void
printsblock(struct kvfs_superblock *sblock)
{
	printf(
	    "magicnum: 0x%.4x, superblock_size: 0x%.4x, freelist_off: 0x%.16jx, inode_off: 0x%.16jx, data_off: 0x%.16jx, block_count: 0x%.8x, flags: 0x%.16lx, fs_size:0x%.16lx\n",
	    sblock->magicnum, sblock->superblock_size, sblock->freelist_off,
	    sblock->inode_off, sblock->data_off, sblock->block_count,
	    sblock->flags, sblock->fs_size);
}

/*
 * Initialize the superblock, and calculate total size and offset
 * of each section in the kvfs partition, based on the disk size.
 * */
void
init_superblock(off_t disksize, struct kvfs_superblock *sb)
{
	assert(sb != NULL);
	/* we always PAD the superblock to fit in exactly one block. */
	off_t superblock_size = BLOCKSIZE;
	/* first guess: assume 1/2 of disk can be used for free blocks */
	off_t blocks = (disksize - superblock_size) / BLOCKSIZE / 2;
	off_t delta = disksize / 16;

	off_t inode_count, free_bitmap, sum;
	while (1) {
		/* TODO: locate free bitmap inside superblock */
		inode_count = blocks;
		free_bitmap = CEIL(blocks, 8);
		sum = superblock_size + blocks * BLOCKSIZE +
		    PAD(inode_count * sizeof(struct kvfs_inode)) +
		    PAD(free_bitmap);

		/* check if we found the solution */
		if (disksize - sum == 0) {
			break;
		} else {
			/* improve guess */
			if (sum > disksize) {
				blocks -= delta;
				/* reduce delta each time we overshoot target */
				delta /= 2;
			} else if (sum < disksize) {
				blocks += delta;
			}
			/* error if we take too many iterations */
			if (delta < 1) {
				warnx("Warning: could not converge solution");
				break;
			}
		}
	}

	/* init superblock */
	sb->magicnum = KVFS_SUPERBLOCK_MAGIC;
	sb->superblock_size = sizeof(struct kvfs_superblock);
	sb->block_count = blocks;
	sb->fs_size = sum;
	sb->flags = 0;

	/* set offsets for each section */
	/* free list always starts at block 1, since sizeof(superblock) < 1
	 * block*/
	sb->freelist_off = BLOCKSIZE;
	sb->inode_off = PAD(free_bitmap) + sb->freelist_off;
	sb->data_off = PAD(inode_count * sizeof(struct kvfs_inode)) +
	    sb->inode_off;
}

/* write a buffer to fd. assumes fd is open */
void
writebuf(int fd, void *buf, size_t size)
{
	ssize_t write_size = write(fd, buf, size);
	if (write_size < 0) {
		perror("write");
		exit(1);
	}
	/* make sure write gets everything */
	while (write_size < size) {
		write_size = write(fd, buf + write_size, size - write_size);
		if (write_size < 0) {
			perror("write");
			exit(1);
		}
	}
}

int
main(int argc, char **argv)
{
	int ch;
	uint8_t buf[BLOCKSIZE];
	char *device = NULL;
	int error;

	while ((ch = getopt(argc, argv, "hf:")) != -1) {
		switch (ch) {
		/* get device name */
		case 'f': {
			char *cp = strrchr(optarg, '/');
			if (cp == NULL) {
				warnx("Device name '%s' is invalid", optarg);
				exit(1);
			}
			device = optarg;
			break;
		}
		default:
			usage();
			exit(1);
		}
	}
	argv += optind;
	argc -= optind;

	/* check if we actually got device name */
	if (device == NULL) {
		usage();
		exit(1);
	}

	int fd = open(device, O_RDWR);
	if (fd < 0) {
		perror("open");
		exit(1);
	}

	/* check if device is already formatted with kvfs */
	uint8_t readbuf[PAGE_SIZE];
	ssize_t bytes_read = read(fd, readbuf, PAGE_SIZE);
	if (bytes_read < 0) {
		perror("read");
		exit(1);
	}
	struct kvfs_superblock check;
	memcpy(&check, readbuf, sizeof(struct kvfs_superblock));
	if (check.magicnum == KVFS_SUPERBLOCK_MAGIC) {
		printf("Device '%s' is already formatted with kvfs.\n", device);
		printf("Do you wish to re-format?\n");
		printf("WARNING: re-formatting will erase all data! [Y|n] ");
		int c = fgetc(stdin);
		if (!(c == 'Y' || c == 'y')) {
			exit(0);
		}
	}
	/* reset read/write offset */
	lseek(fd, 0, SEEK_SET);

	/* get the sector size in bytes */
	u_int sector_size;
	error = ioctl(fd, DIOCGSECTORSIZE, &sector_size);
	if (error != 0) {
		perror("ioctl: DIOCGSECTORSIZE");
		exit(1);
	}
	/* get the size of the disk in bytes */
	off_t media_size;
	error = ioctl(fd, DIOCGMEDIASIZE, &media_size);
	if (error != 0) {
		perror("ioctl: DIOCGMEDIASIZE");
		exit(1);
	}

	printf("Formatting '%s' with kvfs. Sector Size: %d, Media Size: %zu\n",
	    device, sector_size, media_size);

	/* calculate size of each section */
	struct kvfs_superblock sblock;
	init_superblock(media_size, &sblock);

	printf("Writing superblock...\n");
	/* write superblock to disk */
	memcpy(buf, &sblock, sizeof(struct kvfs_superblock));
	bzero(buf + sizeof(struct kvfs_superblock),
	    BLOCKSIZE - sizeof(struct kvfs_superblock));
	writebuf(fd, buf, BLOCKSIZE);

	printf("Writing free list bitmap...\n");
	/* write free list to disk. currently, free list is all 0s */
	bzero(buf, BLOCKSIZE);
	for (off_t ptr = sblock.freelist_off; ptr < sblock.inode_off;
	     ptr += BLOCKSIZE) {
		writebuf(fd, buf, BLOCKSIZE);
	}

	printf("Writing inodes...\n");
	/* write inodes to disk. */
	struct kvfs_inode inode = { 0 };
	inode.flags |= KVFS_INODE_FREE;
	/* do some buffering so we write a full block size at once */
	for (int i = 0; i < BLOCKSIZE; i += sizeof(struct kvfs_inode)) {
		memcpy(buf + i, &inode, sizeof(struct kvfs_inode));
	}
	for (off_t ptr = sblock.inode_off; ptr < sblock.data_off;
	     ptr += sizeof(struct kvfs_inode)) {
		if (ptr % BLOCKSIZE == 0) {
			writebuf(fd, buf, BLOCKSIZE);
		}
	}

	printf("Writing data blocks...\n");
	/* write free space zeroes to disk */
	bzero(buf, BLOCKSIZE);
	for (off_t ptr = sblock.data_off; ptr < sblock.fs_size;
	     ptr += BLOCKSIZE) {
		writebuf(fd, buf, BLOCKSIZE);
	}

	close(fd);

	return 0;
}
