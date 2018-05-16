TARGETS = vtfs

CC ?= g++

INCLUDE_DIR = /usr/include/fuse

CFLAGS_FUSE  = -I$(INCLUDE_DIR)
CFLAGS_FUSE += -DFUSE_USE_VERSION=26
CFLAGS_FUSE += -D_FILE_OFFSET_BITS=64
CFLAGS_FUSE += -D_DARWIN_USE_64_BIT_INODE
CFLAGS_EXTRA = -Ofast $(CFLAGS)

LIBS = -lfuse

.cpp:
	$(CC) $(CFLAGS_FUSE) $(CFLAGS_EXTRA) -o $@ $< $(LIBS)

all: $(TARGETS)

vtfs: vtfs.cpp

clean:
	rm -f $(TARGETS) *.o
	rm -rf *.dSYM
