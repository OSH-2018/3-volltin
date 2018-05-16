# 内存文件系统

## 简介

一个实现了基本文件和目录操作的内存文件系统，数据结构设计和行为见 [源代码注释](https://github.com/OSH-2018/3-volltin/blob/master/vtfs.cpp)。

## 编译

Linux:

```shell
make
# cc -I/usr/include/fuse -DFUSE_USE_VERSION=26 -D_FILE_OFFSET_BITS=64 -Ofast   -o vtfs vtfs.cpp -lfuse
```

macOS (with osxfuse):

```shell
make -f Makefile.mac
# cc -I/usr/local/include/osxfuse/fuse -L/usr/local/lib -DFUSE_USE_VERSION=26 -D_FILE_OFFSET_BITS=64 -D_DARWIN_USE_64_BIT_INODE -Ofast  -o vtfs vtfs.cpp -losxfuse
```