#!/usr/bin/env bash

set -x

sudo make
sudo make load
sudo mdconfig -a -t swap -u 0 -s 5m
sudo tools/mkkvfs -f /dev/md0
sudo mount -t kvfs /dev/md0 /mnt
