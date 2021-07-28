## Key Value File System

## Known Issues 
To the best of our knowledge, our final submission meets all of the assignment specifications. However, there are certainly areas we would like to improve upon, given more time. Here is a list of them:

* While storing inodes in a continuous table is very simple, a major consequence of this simplicity is that file lookups are always `O(n)`. Because we do not implement a directory hierarchy, a large `kvfs` partition will always have very slow lookups. Because `VOP_LOOKUP()` is called before pretty much any I/O operation, this means that I/O is overall quite slow. 
    * This could be improved without changing the design by using a lookup cache.
* `VOP_WRITE` does not appear to update the modification timestamp. However, `touch(1)` does.
* When using an editor like `vi(1)` or `nano(1)`, writing to a file will panic the kernel. This appears to be because the editor will attempt to write past the end of a block which does not belong to us.
* `VFS_SYNC` and `VOP_FSYNC` are currently broken. Instead, we always synchronously write the buffers with `bwrite()`
    * Because if this, `O_ASYNC` is not supported.
    * This means that `O_SYNC` is technically the "default mode"

## Building and Loading

To build and load the `kvfs` filesystem kernel module, simply run
```
make
sudo make load
```

This will also build the `tools/` subdirectory, which contains the `mkkvfs` tool.

To format a disk with `mkkvfs`:
```
sudo tools/mkkvfs -f $DISK_DEVICE
```

If your `$DISK_DEVICE` is already formatted with `kvfs`, `mkkvfs` will ask for confirmation before rewriting the disk.

## Building the docs
To make the documentation (DESIGN.pdf) you will need the following:

- `pandoc >= 2.0`
- a LaTeX engine with PDF export support, such as `XeTeX` or `pdfTeX`.

Then run `make docs` and the design document will be built into a PDF.

## clang-format
A `.clang-format` file is provided, which is supposed to closely match the FreeBSD kernel style guide. It was taken directly from https://reviews.freebsd.org/source/svnsrc/browse/head/.clang-format

To reformat the source code, simply run `make format`.

Note: to install a recent version of clang-format on FreeBSD, the package `llvm-devel` is required. This will create an executable `clang-format-devel` which should be symlinked to `clang-format`.
