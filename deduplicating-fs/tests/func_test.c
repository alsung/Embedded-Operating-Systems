// Functional tests for ddfs.
// Includes things like standard writes and reads, opening and closing multiple files, etc.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "func_test.h"

#define MAX_NAME 20

// Checks if open read throws any errors.
// Returns a valid file descriptor, -1 otherwise.
int test_open(char *path, int flags)
{
    int ret = open(path, flags);
    if (ret < 0) {
        printf("test_open(): exiting with error number %d\n", errno);
        return -1;
    }
    return ret; // returns valid file descriptor
}

// Checks if close read throws any errors.
// Returns 0 on success, -1 otherwise.
int test_close(int fd)
{
    int ret = close(fd);
    if (ret < 0) {
        printf("test_close(): exiting with error number %d\n", errno);
        return -1;
    }
    return 0; // returns 0 on success
}

// Checks if write read throws any errors.
// Returns number of bytes written, -1 if an error occurs.
int test_write(int fd, void *buf, size_t num)
{
    int ret = write(fd, buf, num);
    if (ret < 0) {
        printf("test_write(): exiting with error number %d\n", errno);
        return -1;
    }
    return ret; // returns number of bytes written
}

// Checks if read throws any errors.
// Returns number of bytes read (0 if EOF is reached), -1 for an error.
int test_read(int fd, void *buf, size_t num)
{
    int ret = read(fd, buf, num);
    if (ret < 0) {
        printf("test_read(): exiting with error number %d\n", errno);
        return -1;
    }
    return ret; // returns number of bytes read (0 if EOF is reached)
}

// Checks if multiple files can be open at the same time.
// Return 0 on success, -1 otherwise.
int test_open_multiple(int filenum, int *fdvector, int flags)
{
    for (int i = 0; i < filenum; i++) {
        char str[11];
        snprintf(str, 11, "/mnt/file%d", i);
        int fd = open(str, flags);
        if (fd == -1) {
            printf("Iteration: %d\n", i);
            printf("test_open_multiple(): exiting with error number %d\n", errno);
            return fd;
        }
        fdvector[i] = fd;
    }
    return 0;
}

// Checks if multiple files can be closed at the same time.
// Return 0 on success, -1 otherwise.
int test_close_multiple(int filenum, int *fdvector)
{
    for (int i = 0; i < filenum; i++) {
        int ret = close(fdvector[i]);
        if (ret < 0) {
            printf("test_close_multiple(): exiting with error code %d\n", errno);
            return ret;
        }
    }
    return 0;
}

// Checks if a single symlink can be created correctly.
// Return 0 on success, -1 otherwise.
int test_link(char *path, char *name)
{
    // open file
    int flags = O_CREAT | O_EXCL | O_RDWR;
    int fd = open(path, flags);
    if (fd < 0) {
        printf("test_link() -> open: exiting with error code %d\n", errno);
        return fd;
    }
    // Write some data to the file
    int err = write(fd, "test", strlen("test") + 1);
    if (err < 0) {
        printf("test_link() -> write: exiting with error code %d\n", errno);
        return err;
    }
    // Close file
    int ret = close(fd);
    if (ret != 0) {
        printf("test_close(): exiting with error number %d\n", errno);
        return -1;
    }
    // Create symlink to file
    err = symlink(path, name);
    if (err < 0) {
        printf("test_link() -> symlink: exiting with error number %d\n", errno);
        return -1;
    }
    // Try and read link
    char tmp[256];
    err = readlink(name, tmp, 256);
    tmp[255] = '\0'; // readlink() does not null terminate, see readlink(2)
    char *result = strstr(tmp, path); // check for path in returned string
    if (result != NULL) {
        remove(path); // remove file
        return 0; // test passed
    } else {
        remove(path); // remove file
        return -1; // test failed
    }
}
