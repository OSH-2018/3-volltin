#!/bin/sh

mkdir fs
./vtfs fs
cd fs

# standard test
ls -al
echo helloworld > testfile
ls -l testfile
cat testfile
dd if=/dev/zero of=testfile bs=1M count=200
ls -l testfile
dd if=/dev/urandom of=testfile bs=1M count=1 seek=10
ls -l testfile
dd if=testfile of=/dev/null
rm testfile
ls -al

# dir test

mkdir dir1 dir2 dir3
echo aaa > dir1/aaa
echo bbb > dir2/bbb
echo ccc > dir3/ccc
ls -al
cat dir1/aaa
cat dir2/bbb
cat dir3/ccc

cd ..
fusermount -u fs
rm -rf fs