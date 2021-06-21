// Test harness for KVFS filesystem

#include <sys/param.h>
#include <sys/mount.h>
// #include <sys/vfs.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Function declarations
static int test_mount(void);
static int test_open_close(char *filename);
static int test_write_read(void *buf);
static int test_statfs(int fd);
// static int test_mount(void);

int main(int argc, char const *argv[]) {

    int err = 0;

    // prompt to run open/close test
    printf("About to run open/close test, press c to continue: ");
    while (getchar() != 'c') {
        printf("About to run open/close test, press c to continue: ");
    }

    // run open/close test
    err = test_open_close("/mnt/FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
    if (err < 0) {
        printf("open(): Error value: %d\n", errno);
        printf("Open/close test failed, exiting\n");
        return err;
    }

    // prompt to run write/read test
    while (getchar() != 'c') {
        printf("About to run write/read tests, press c to continue: ");
    }

    // run write/read test
    char *tmp1 = calloc(20, sizeof(char));
    err = test_write_read(tmp1);
    if (err < 0) {
        printf("Write/read(): Error value: %d\n", errno);
        return err;
    }
    free(tmp1);

    // prompt to run statfs test
    while (getchar() != 'c') {
        printf("About to run statfs test, press c to continue: ");
    }

    // run statfs test
    int flags = O_CREAT | O_EXCL | O_RDWR;
    int fd = open("/mnt/0123456789ABCDEF0123456789ABCDEF01234567", flags);
    err = test_statfs(fd);
    if (err < 0) {
        printf("statfs(): Error value: %d\n", errno);
        printf("Statfs test failed, exiting\n");
        return err;
    }
    close(fd);

    // prompt to run EINVAL test
    while (getchar() != 'c') {
        printf("About to run EINVAL test, press c to continue: ");
    }

    // run EINVAL test
    fd = open("/mnt/invalid_file_name", flags);
    if (fd < 0) {
        printf("EINVAL test: invalid file name failed as expected\n");
    } else {
        printf("EINVAL test: did not fail as expected, exiting\n");
        return fd;
    }

    // prompt to run 4KiB write test
    // while (getchar() != 'c') {
    //     printf("About to run 4 KiB write test, press c to continue: ");
    // }

    // run 4KiB write test
    // fd = open("/mnt/0123456789ABCDEF0123456789ABCDEF01234568", flags);
    // if (fd) {
    //     printf("run 4KiB test: Error value: %d\n", errno);
    //     printf("run 4KiB test: error opening file, exiting\n");
    //     return fd;
    // }
    // char tmp2[4096];
    // memset(tmp2, '0', 4096);
    // printf("run 4KiB test: %s", tmp2);
    // int ret = write(fd, tmp2, 4096);
    // if (ret < 0) {
    //     printf("Error value: %d\n", errno);
    //     printf("run 4KiB test failed, exiting\n");
    //     return ret;
    // }

    return 0;
}

// Unit tests

static int test_open_close(char *filename) {
    int flags = O_CREAT | O_EXCL | O_RDWR;
    int fd = open("/mnt/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", flags);
    if (fd < 0) {
        printf("open(): Error value: %d\n", errno);
        return fd;
    }
    int ret = close(fd);
    if (ret < 0) {
        printf("close(): Error value: %d\n", errno);
        return ret;
    }
    return 0;
}

static int test_write_read(void *buf) {
    // Open file
    int flags = O_CREAT | O_EXCL | O_RDWR;
    int fd = open("/mnt/AB189EFD74591FEA99C225BAEC10482A296CC38F", flags);

    // Write "test" to file
    int ret = write(fd, "test", strlen("test") + 1);
    if (ret < 0) {
        printf("Error value: %d\n", errno);
        return ret;
    }
    printf("Wrote \'test\'\n");

    // Read "test" from file
    char tmp[5];
    ret = read(fd, tmp, 5);
    if (ret < 0) {
        printf("Error value: %d\n", errno);
        return ret;
    }
    printf("Read %s\n", tmp);

    // Close file
    close(fd);
    return 0;
}

static int test_statfs(int fd) {
    // Get file info
    struct statfs info;
    int ret = fstatfs(fd, &info);
    if (ret == 0) {
        // Print out info fields of struct (21 fields)
        printf("Structure version number:               %u\n",  info.f_version);
        printf("Type of filesystem:                     %u\n",  info.f_type);
        printf("Copy of mount exported flags:           %lu\n", info.f_flags);
        printf("Filesystem fragment size:               %lu\n", info.f_bsize);
        printf("Optimal transfer block size:            %lu\n", info.f_iosize);
        printf("Total data blocks in filesystem:        %lu\n", info.f_blocks);
        printf("Free blocks in filesystem:              %lu\n", info.f_bfree);
        printf("Free blocks available to non-superuser: %ld\n", info.f_bavail);
        printf("Total file nodes in filesystem:         %lu\n", info.f_files);
        printf("Free nodes available to non-superuser:  %ld\n", info.f_ffree);
        printf("Count of sync writes since mount:       %lu\n", info.f_syncwrites);
        printf("Count of async writes since mount:      %lu\n", info.f_asyncwrites);
        printf("Count of sync reads since mount:        %lu\n", info.f_syncreads);
        printf("Count of async reads since mount:       %lu\n", info.f_asyncreads);
        printf("Maximum filename length:                %u\n",  info.f_namemax);
        printf("User that mounted filesystem:           %d\n",  info.f_owner);
        printf("Filesystem ID:                          %d%d\n", info.f_fsid.val[0],
                                                                   info.f_fsid.val[1]);
        printf("Spare string space:                     %s\n", info.f_charspare);
        printf("Filesystem type name:                   %s\n", info.f_fstypename);
        printf("Mounted filesystem:                     %s\n", info.f_mntfromname);
        printf("Mounted directory:                      %s\n", info.f_mntonname);
    }
    return ret;
}

// static int test_mount(void) {
//     return mount("kvfs", "/dev/md0", 0, mount_kvfs);
// }

// static int test_unmount(void) {
//     return unmount("/dev/md0", 0);
// }

// Random test
// static int random_test(void) {
//     // Open test file
//     int flags = O_CREAT | O_EXCL | O_RDWR;
//     int fd = open("/mnt/random_test0123456789qwertyuiopasdfghjkl", flags);
//
//     // Write some random data
//     char rd_char;
//     printf("Wrote:");
//     for (int i = 0; i < 10; i++) {
//         rd_char = (char) (random() % 127);
//         write(fd, &rd_char, 1);
//         putchar(rd_char);
//     }
//     putchar('\n');
//     write(fd, "\0", 1);
//
//     // Check contents
//     char tmp[11];
//     int ret = read(fd, tmp, 11);
//     printf("Read: %s\n", tmp);
//
//     // Close file
//     close(fd);
//     return 0;
// }
