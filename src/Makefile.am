AM_CPPFLAGS = $(fuse3_CFLAGS) $(libsodium_CFLAGS)
AM_CXXFLAGS = -O3 -Wall -Wextra -std=gnu++20 -pthread -flto
AM_LDFLAGS = -pthread -flto

bin_PROGRAMS = 6fs

6fs_SOURCES = \
    index.hpp \
    logger.hpp logger.cpp \
    emergency.hpp emergency.cpp \
    storage.hpp storage.cpp \
    storage_file.hpp storage_file.cpp \
    storage_memory.hpp storage_memory.cpp \
    storage_mmap.hpp storage_mmap.cpp \
    map.hpp map.cpp \
    chunk.hpp chunk.cpp \
    time.hpp time.cpp \
    inode.hpp inode.cpp \
    dirent.hpp dirent.cpp \
    block.hpp block.cpp \
    handle.hpp handle.cpp \
    encrypt.hpp encrypt.cpp \
    base.hpp base.cpp \
    sixfs.hpp sixfs.cpp \
    dump.hpp dump.cpp \
    main.cpp

6fs_LDADD = $(fuse3_LIBS) $(libsodium_LIBS)
