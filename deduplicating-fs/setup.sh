#!/bin/sh

set -x

sudo make
sudo make load
sudo mdconfig -a -t swap -u 0 -s 20m
sudo tools/newfs-ddfs/newfs-ddfs /dev/md0
sudo mount -t ddfs /dev/md0 /mnt
