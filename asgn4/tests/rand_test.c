/* Random tests for ddfs.
 * This is really only one "test", but what it does is fairly comprehensive:
 *  - Creates a file with a random name, up to some maximum.
 *  - Opens that file, with read-write status.
 *    - Not with random flags (as originally intended)
 *  - Writes a random number of bytes to the file.
 *  - Reads a random number of bytes from the file.
 *  - Close the file.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "rand_test.h"

#define MAX_NAME_LEN 255       // max generated filename length
#define MAX_NUM_CHAR 1024      // maximum number of chars to write

int test_random(void) {

    // Generate random name, up to MAX_NAME_LEN characters long
    // Derived from:
    // https://stackoverflow.com/questions/42570700/generating-10-random-characters
    // printf("Generating name...\n");
    int name_len = rand() % MAX_NAME_LEN; // generate name of random length up to define value
    char filename[strlen("/mnt/") + name_len + 1]; // buffer for filename
    strncpy(filename, "/mnt/", strlen("/mnt/"));
    for (int i = 5; i < name_len + 5; i++) {
        filename[i] = abc[rand() % 26];
    }
    filename[name_len] = '\0'; // null termiate string

    // Create and open a file with that random name, read-write
    // printf("Creating and opening file...\n");
    int flags = O_CREAT | O_EXCL | O_RDWR;
    int fd = open(filename, flags);
    if (fd < 0) {
        printf("test_random() -> open: exiting with error code %d\n", errno);
        return -1;
    }

    // Write a random amount of random bytes to that file.
    // Derived from: https://stackoverflow.com/questions/19724346/generate-random-characters-in-c
    int nbytes = rand() % MAX_NUM_CHAR;
    char *write_tmp = calloc(nbytes + 1, sizeof(char));
    // printf("Write: ");
    for (int i = 0; i < nbytes; i++) {
        char letter = abc[rand() % 26];
        write_tmp[i] = letter;
        int err = write(fd, &letter, 1);
        if (err < 0) {
            printf("test_random() -> write: exiting with error code %d", errno);
            return -1;
        }
        // putc(letter, stdout);
    }
    // putc('\n', stdout);

    // Read those bytes just written to the file
    char *read_tmp = calloc(nbytes + 1, sizeof(char));
    lseek(fd, 0, SEEK_SET);
    int err = read(fd, read_tmp, nbytes);
    // printf("Bytes read: %d\n", err);
    if (err < 0) {
        printf("test_random() -> read: exiting with error code %d\n", errno);
        return err;
    }
    // printf("Read: %s\n", read_tmp);

    // Check if both written string and read string are the same
    int ret = strcmp(write_tmp, read_tmp);
    // printf("Write_tmp: %s\n", write_tmp);
    // printf("Read_tmp: %s\n", read_tmp);
    // printf("Strcmp return value: %d\n", ret);
    if (ret == 0) {
        free(write_tmp);
        free(read_tmp);
        err = close(fd);
        if (err < 0) {
            printf("test_random() -> close: exiting with error code %d\n", errno);
        }
        printf("test_random passed\n");
        return 0;
    } else {
        free(write_tmp);
        free(read_tmp);
        err = close(fd);
        if (err < 0) {
            printf("test_random() -> close: exiting with error code %d\n", errno);
        }
        printf("test_random failed\n");
        return -1;
    }
}
