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

#include "ddfs.h"
#include "ddfs_fs.h"

void
usage()
{
	printf("mkkvfs [-f device]\n");
	printf("-f device\t\tThe file to read\n");
}

int
main(int argc, char** argv) {
	int ch;
	//uint8_t buf[DDFS_BLOCKSIZE];
	char *device = NULL;
	int error;
	int fd;
	char readbuffer[SBLOCKSIZE + 1];
   int read_size;
	int num_used_dedup_entries;
   int tot_ref_count;
   
	while ((ch = getopt(argc, argv, "hf:")) != -1) {
		switch (ch) {
		/* get device name */
		case 'f': {
			device = optarg;
			break;
		}
		/* print usage menu */
		case 'h': {
			usage();
			exit(1);
		}
		default:
			usage();
			exit(1);
		}
	}
	argv += optind;
	argc -= optind;

	if (device != NULL)
		printf("%s\n", device);
	else
		printf("Got no filename\n");
	fd = open(device, O_RDONLY);
	if (fd <= 0) {
		printf("Could not open file\n");
		exit(1);
	}
   lseek(fd, SBLOCK_UFS2, SEEK_SET);
	read_size = read(fd, readbuffer, SBLOCKSIZE);
	struct fs check;
   int32_t dedup_offset;
   int32_t n_dedup_blocks;
   int32_t n_dedup_entries;
   int32_t curr_block_no;
   int32_t n_dedup_entries_per_block;
   int64_t space_saved;
	if (strlen(readbuffer) == 0) {
		perror("Empty file");
      close(fd);
		exit(1);
	}
/*
 * superblock is located at SBLOCK_UFS2 byte offset
 * read in the superblock (struct fs in ddfs_fs.h)
 * get the offset for the dedup table (fs->fs_ddblkno)
 * get the number of 4k blocks in the dedup table (fs->fs_dedupfrags)
 * read the dedup table and count all the active entries
 */

	memcpy(&check, readbuffer, sizeof(struct fs));
	if (check.fs_magic != FS_DDFS_MAGIC) {
		printf("Error: incorrectly formatted file. Please try a different file\n");
		close(fd);
		exit(1);
	} else {
		printf("Got correctly formatted file\n");
	}
   dedup_offset = check.fs_ddblkno;
	//lseek(fd, dedup_offset, SEEK_SET);
	num_used_dedup_entries = 0;
   tot_ref_count = 0;
   
   /* get the number of blocks containing dedup entries and the total number of dedup entries */
   n_dedup_blocks = check.fs_dedupfrags;
   n_dedup_entries_per_block = DDFS_BLOCKSIZE / sizeof(struct ddfs_dedup);
   n_dedup_entries = n_dedup_blocks * n_dedup_entries_per_block;
   printf("superblock offset = %d\n", SBLOCK_UFS2);
   printf("n_dedup_blocks = %d\n", n_dedup_blocks);
   printf("n_dedup_entries = %d\n", n_dedup_entries);
   printf("n_dedup_entries_per_block = %d\n", n_dedup_entries_per_block);
	for (curr_block_no = 1; curr_block_no <= n_dedup_blocks; curr_block_no++) {
      int i;
      struct ddfs_dedup check;
      
      memset(readbuffer, 0, DDFS_BLOCKSIZE + 1);
      read_size = read(fd, readbuffer, DDFS_BLOCKSIZE);
      readbuffer[DDFS_BLOCKSIZE] = 0;
      if (read_size < 0) {
         perror("dedup read error");
         close(fd);
         exit(1);
      }
      for (i = 0; i < n_dedup_entries_per_block; i++) {
         memcpy(&check, readbuffer + (i * sizeof(check)), sizeof(check));
/*printf("got check with key = '%s', flags = %d, ref_count = %d\n", check.key, check.flags, check.ref_count);*/
         if (check.flags & DDFS_DEDUP_ACTIVE) {
            num_used_dedup_entries++;
            tot_ref_count += check.ref_count;
         }
      }
   }
	close(fd);
   printf("You have used %d data block(s) with %d references to the data\n\n", num_used_dedup_entries, tot_ref_count);
   space_saved = (tot_ref_count - num_used_dedup_entries) * DDFS_BLOCKSIZE;
   printf("You have saved %jd space!\n", space_saved);
	return 0;
}
