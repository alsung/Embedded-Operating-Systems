# asgn4 (`ddfs`), by group ABAJ

# Building and Loading

## Kernel Module
To build and load the `ddfs` filesystem kernel module, simply run
```
make
sudo make load
```

This will also build the `tools/` subdirectory, which contains the `newfs-ddfs` and `statddfs` tools.

You can now mount a disk device formatted with the `ddfs` filesystem:
```
sudo mount -t ddfs $DISK_DEVICE $MOUNT_LOCATION
```

## Formatting a Disk

To format a disk with `ddfs`:
```
sudo tools/newfs-ddfs/newfs-ddfs $DISK_DEVICE
```

If your `$DISK_DEVICE` is already formatted with `ddfs`, you can use the `-E` flag to fully erase the disk.

## Extra Credit Program

The extra credit program `statddfs` resides in `tools/extra-credit`. It can be used to calculate how much space was saved by deduplication in `ddfs`.

To use it, simply run:
```
sudo tools/extra-credit/statddfs -f $DISK_DEVICE
```

## Divergence from Stated Goals
Because of our choice to modify FFS, our design diverges from the intended design and stated goals in several notable ways:

* Instead of replacing 64-bit block pointers in a file's inode with 160-bit keys, block pointers from FFS are preserved, and simply swapped out for existing block pointers when doing deduplication.
* Because we retain 15 64-bit block pointers, the maximum file size is instead limited to what it was in Berkeley FFS, which is approximately 1TiB when using 4KiB blocks.
* Although not explicitly stated in the assignment specification, our disk layout is different from intended, reserving 12.5% of the disk for deduplication metadata, with the rest of the disk acting as a normal FFS filesystem.

We feel that our resulting `ddfs` is functionally similar to the design stated in the `ddfs` assignment specification, even though the internal details are different.

## Known Issues
To the best of our knowledge, our final submission most of the assignment specifications. However, there are certainly areas we would like to improve upon, given more time. Here is a list of them:

* Because our deduplication strategy uses a simple table, all lookups are O(n). We do deduplication lookups on every file I/O operation except `read()`. 
    * If given more time, we would have liked to implement caching for deduplication table lookups.
* We do not currently implement deduplication on indirect block pointers, and do not support them.
    * Unfortunately our `test_max` would always fail due to some weirdness with indirect block pointers. This test is currently disabled.
    * Because we utilize block allocation and read/write and all other code from FFS, files with indirect blocks _should_ still work. However, large files using indirect blocks have not been tested and debugged due to time constraints, so we just say we don't support them.
* Occasionally when unloading or reloading the module, `dmesg` will get filled with warnings about re-using sysctl leafs. These sysctls are unmodified from FFS code, and were not removed due to time constraints.
* Would like to clean up and remove unused code related to UFS1, soft updates, etc.
* The deduplication table is always synchronously written to disk with `bwrite()`. 
