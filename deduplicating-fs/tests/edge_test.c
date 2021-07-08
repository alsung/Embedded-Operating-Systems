// Edge case tests for ddfs
// Includes things like writing more than the max file size, opening files with
// wrong permissions, etc.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "edge_test.h"

// According to the assignment spec, the max file size is "slightly over 32 GiB"
// Doing the math, this works out to exactly 32.545 GiB as a max file size.
// This test simply check to see if this max file size is enforced.
int test_max(char *path, int flags) {
    // open a file
    int ret = open(path, flags);
    if (ret < 0) {
        printf("test_max() -> open: exiting with error number %d\n", errno);
        return ret;
    }
    // Try to write 33 Gib of data to a single file
    for (int i = 0; i < 33; i++) {
        char *massive = calloc(SIZE_GIGA, sizeof(char));
        memset(massive, 'a', SIZE_GIGA);
        int err = write(ret, massive, SIZE_GIGA);
        if (err) {
            printf("Iteration: %d\n", i);
            printf("test_max() -> write: exiting with error number %d\n", errno);
            return err;
        }
        free(massive);
    }
    return 0;
}

// Removing a file that doesn't exist should return an error.
// (Specifically, ENOENT, but as long as it errors for this assignment,
// it should be fine)
int test_enoent(void) {
    int flags = O_CREAT | O_EXCL | O_RDWR;
    int fd = open("/mnt/test_enoent.txt", flags);
    close(fd);
    int ret1 = remove("/mnt/test_enoent.txt");
    if (ret1 < 0) {
        printf("test_enoent() -> remove: error %d, failed removing file\n", errno);
        return -1;
    }
    int ret2 = remove("/mnt/test_enoent.txt");
    if (ret2 < 0) {
        return 0; // test passed
    } else {
        return -1; // test failed
    }
}

// Using ftruncate/truncate to extend a file past its maximum size
// should also error.
int test_max_trunc(void){
    int flags = O_CREAT | O_EXCL | O_RDWR;
    int fd = open("test_max_trunc.txt", flags);
    int ret = ftruncate(fd, 33 * SIZE_GIGA);
    if (ret != -1) {
        return 0; // test passed
    } else {
        return -1; // test failed
    }
}
