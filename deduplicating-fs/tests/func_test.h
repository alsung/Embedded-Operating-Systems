// Header file for func_test.c

#define MAX_NAME 20 // max length of file name

// Function declarations
int test_open(char *path, int flags);
int test_close(int fd);
int test_write(int fd, void *buf, size_t num);
int test_read(int fd, void *buf, size_t num);
int test_open_multiple(int filenum, int* fdvector, int flags);
int test_close_multiple(int filenum, int* fdvector);
int test_link(char *path, char *name);
