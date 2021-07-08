// Main function for ddfs test harness

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "func_test.h"
#include "rand_test.h"
#include "edge_test.h"

// change this number to modify how many random tests are run
#define NUM_RAND_TEST 10

static void print_result(int retval, char *testname, int *pcount);

int main(int argc, char const *argv[]) {

    int count = 0;
    int ret, fd;
    int flags = O_CREAT | O_EXCL | O_RDWR;

    // Initiate functional tests.
    fd = test_open("/mnt/open_test.txt", flags); // open single test
    print_result(fd, "test_open", &count);

    char *test_str = "test";
    int test_str_len = strlen(test_str) + 1;
    ret = test_write(fd, test_str, test_str_len); // write single test
    test_str = "test_write";
    print_result(ret, test_str, &count);

    char test_buf[test_str_len];
    ret = test_read(fd, test_buf, test_str_len); // read single test
    test_str = "test_read";
    print_result(ret, test_str, &count);

    ret = test_close(fd);
    test_str = "test_close";
    print_result(ret, test_str, &count);  // close single test

    int filevector[3];
    ret = test_open_multiple(3, filevector, flags); // open multiple test
    test_str = "test_open_multiple";
    print_result(ret, test_str, &count);

    ret = test_close_multiple(3, filevector);       // close multiple test
    test_str = "test_close_multiple";
    print_result(ret, test_str, &count);

    ret = test_link("/mnt/test_link", "/mnt/test_link_new"); // link test
    test_str = "test_link";
    print_result(ret, test_str, &count);

    // Intiate edge case tests.
    // ret = test_max("test_max", flags); // max file size test
    // test_str = "test_max";
    // print_result(ret, test_str, &count);

    ret = test_enoent();  // removing a file twice test
    test_str = "test_enoent";
    print_result(ret, test_str, &count);

    // Initiate random tests.
    // srand(time(NULL)); // seed srand with current time
    // for (int i = 0; i < NUM_RAND_TEST; i++) {
    //     ret = test_random();
    //     if (ret == 0) {
    //         count++;
    //     }
    // }

    // Display results.
    printf("Total number of tests passed: %d/%d\n", count, 8);

    return 0;
}

// helper function for error checking tests
static void print_result(int retval, char *testname, int *pcount) {
    if (retval < 0) {
        printf("Error %s: %d failed\n", testname, errno);
    } else {
        printf("%s passed\n", testname);
        (*pcount)++;
    }
}
