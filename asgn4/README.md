# asgn4 (`ddfs`), by group ABAJ

## Known Issues
To the best of our knowledge, our final submission meets all of the assignment specifications. However, there are certainly areas we would like to improve upon, given more time. Here is a list of them:

## Building and Loading

To build and load the `ddfs` filesystem kernel module, simply run
```
make
sudo make load
```

This will also build the `tools/` subdirectory, which contains the `mkfs` and `newfs` tools. 

To format a disk with `ddfs`:
```
sudo tools/newfs-ddfs/mkfs -f $DISK_DEVICE
```

If you `$DISK_DEVICE` is already formatted with `ddfs`, `mkfs` will ask for confirmation before rewriting the disk. 

## Building the docs
To make the documentation (DESIGN.pdf) you will need the following:

- `pandoc >= 2.0`
- a LaTeX engine with PDF export support, such as `XeTeX` or `pdfTeX`.

Then run `make docs` and the design document will be built into a PDF.

## clang-format
A `.clang-format` file is provided, which is supposed to closely match the FreeBSD kernel style guide. It was taken directly from https://reviews.freebsd.org/source/svnsrc/browse/head/.clang-format

To reformat the source code, simply run `make format`.

Note: to install a recent version of clang-format on FreeBSD, the package `llvm-devel` is required. This will create an executable `clang-format-devel` which should be symlinked to `clang-format`.

