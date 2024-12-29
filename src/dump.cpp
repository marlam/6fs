/*
 * Copyright (C) 2023, 2024
 * Martin Lambers <marlam@marlam.de>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

#include "base.hpp"
#include "index.hpp"

#include "dump.hpp"


static int getIndex(const char* s, uint64_t* index)
{
    char* endptr = nullptr;
    errno = 0;
    uint64_t i = strtoull(s, &endptr, 10);
    if (errno != 0 || endptr == s || *endptr != '\0') {
        fprintf(stderr, "Invalid index %s\n", s);
        return -ERANGE;
    }
    *index = i;
    return 0;
}

int dump(const std::string& dirName,
        const std::vector<unsigned char>& key,
        const char* dumpInode,
        const char* dumpTree,
        const char* dumpDirent,
        const char* dumpSBlock,
        const char* dumpDBlock)
{
    Base base(Storage::TypeFile, dirName, 0, key, false);
    std::string errStr;
    bool needsRootNode = false;
    int r = base.initialize(errStr, &needsRootNode);
    if (r < 0) {
        fprintf(stderr, "Cannot initialize 6fs base: %s\n", errStr.c_str());
        return 1;
    }
    if (needsRootNode) {
        fprintf(stderr, "6fs is empty\n");
        return 1;
    }

    if (dumpInode) {
        uint64_t index;
        r = getIndex(dumpInode, &index);
        if (r < 0)
            return 1;
        Inode inode;
        r = base.inodeRead(index, &inode);
        if (r < 0) {
            fprintf(stderr, "Reading inode %lu: %s\n", index, strerror(-r));
            return 1;
        }
        printf("Inode %lu:\n", index);
        printf("  Type: %s\n",
                  inode.type() == TypeSOCK ? "socket"
                : inode.type() == TypeLNK  ? "symlink"
                : inode.type() == TypeREG  ? "file"
                : inode.type() == TypeBLK  ? "block device"
                : inode.type() == TypeDIR  ? "directory"
                : inode.type() == TypeCHR  ? "character device"
                : inode.type() == TypeFIFO ? "fifo"
                : "invalid");
        printf("  Mode: 0%o\n", inode.typeAndMode & ~TypeMask);
        printf("  Atime: %ld seconds %u nanoseconds\n", inode.atime.seconds, inode.atime.nanoseconds);
        printf("  Ctime: %ld seconds %u nanoseconds\n", inode.ctime.seconds, inode.ctime.nanoseconds);
        printf("  Mtime: %ld seconds %u nanoseconds\n", inode.mtime.seconds, inode.mtime.nanoseconds);
        printf("  UID, GID: %u %u\n", inode.uid, inode.gid);
        printf("  nlink: %d\n", inode.nlink);
        printf("  rdev: %lu\n", inode.rdev);
        printf("  size: %lu\n", inode.size);
        printf("  slotTrees: %lu %lu %lu %lu %lu\n", inode.slotTrees[0], inode.slotTrees[1], inode.slotTrees[2], inode.slotTrees[3], inode.slotTrees[4]);
    }
    if (dumpDirent) {
        uint64_t index;
        r = getIndex(dumpDirent, &index);
        if (r < 0)
            return 1;
        Dirent dirent;
        r = base.direntRead(index, &dirent);
        if (r < 0) {
            fprintf(stderr, "Reading directory entry %lu: %s\n", index, strerror(-r));
            return 1;
        }
        printf("Directory entry %lu:\n", index);
        printf("  Name:  %s\n", dirent.name);
        printf("  Inode: %lu\n", dirent.inodeIndex);
    }
    if (dumpTree) {
        uint64_t index;
        r = getIndex(dumpTree, &index);
        if (r < 0)
            return 1;
        Inode inode;
        r = base.inodeRead(index, &inode);
        if (r < 0) {
            fprintf(stderr, "Reading inode %lu: %s\n", index, strerror(-r));
            return 1;
        }
        Handle handle(&base, index, inode);
        printf("Inode %lu slot tree:\n", index);
        printf("  slotCount: %lu\n", handle.slotCount());
        for (uint64_t s = 0; s < handle.slotCount(); s++) {
            uint64_t val;
            r = handle.getSlot(s, &val);
            if (r < 0) {
                fprintf(stderr, "Reading slot %lu: %s\n", s, strerror(-r));
                return 1;
            }
            printf("  slot %lu: %lu\n", s, val);
        }
    }
    if (dumpSBlock) {
        uint64_t index;
        r = getIndex(dumpSBlock, &index);
        if (r < 0)
            return 1;
        Block block;
        r = base.blockRead(index, &block);
        if (r < 0) {
            fprintf(stderr, "Reading block %lu: %s\n", index, strerror(-r));
            return 1;
        }
        printf("Block %lu indirection indices:\n", index);
        for (uint64_t i = 0; i < Handle::N; i++) {
            uint64_t j = block.indices[i];
            if (j != InvalidIndex) {
                printf("  %lu: %lu\n", i, j);
            }
        }
    }
    if (dumpDBlock) {
        uint64_t index;
        r = getIndex(dumpDBlock, &index);
        if (r < 0)
            return 1;
        Block block;
        r = base.blockRead(index, &block);
        if (r < 0) {
            fprintf(stderr, "Reading block %lu: %s\n", index, strerror(-r));
            return 1;
        }
        printf("Block %lu data bytes:\n", index);
        for (int j = 0; j < 128; j++) {
            for (int k = 0; k < 32; k++) {
                printf("%02X ", block.data[j * 32 + k]);
            }
            printf("\n");
        }
    }

    return 0;
}
