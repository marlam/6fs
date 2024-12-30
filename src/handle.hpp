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

#pragma once

#include <cstdint>
#include <functional>
#include <shared_mutex>

#include "inode.hpp"
#include "dirent.hpp"
#include "block.hpp"
#include "dump.hpp"

class Base;


class Handle
{
private:
    Base* _base;
    const uint64_t _inodeIndex;
    Inode _inode;
    uint64_t _slotCount;
    bool _readOnly;
    bool _append;
    int _refCount;
    std::shared_mutex _mutex;
    bool _removeOnceUnused;

    // internal locking helper functions
    void lockExclusive();
    void unlockExclusive();
    void lockShared();
    void unlockShared();

    /* Slot Management */

    static constexpr uint64_t N = sizeof(Block) / sizeof(uint64_t); // number of indirection slots per block
    static constexpr uint64_t maxSlotCount = 1 + N + N * N + N * N * N + N * N * N * N;
    uint64_t _cachedBlockIndices[4]; // one for each level of indirection
    Block _cachedBlocks[4];          // one for each level of indirection
    bool _cachedBlockIsModified[4];  // one for each level of indirection

    static void slotToTreeIndices(uint64_t slot, int* tree, uint64_t ijkl[4]);
    int cacheBlock(int indirectionLevel, uint64_t blockIndex);
    int saveCachedBlockIfModified(int treeLevel);

    uint64_t slotCount() const;
    int getSlot(uint64_t slot, uint64_t* i);
    int setSlot(uint64_t slot, uint64_t i);
    int insertSlot(uint64_t slot, uint64_t direntOrBlockIndex);
    int removeSlot(uint64_t slot, bool removeDirentOrBlock);

    // internal helper functions
    bool updateATime(); // update atime according to the relatime mount option rules; return true if modified
    int truncateNow(uint64_t length);
    int removeNow();

    // the dump() function may use internals
    friend int ::dump(const std::string& dirName,
            const std::vector<unsigned char>& key,
            const char* dumpInode,
            const char* dumpTree,
            const char* dumpDirent,
            const char* dumpSBlock,
            const char* dumpDBlock);

public:
    Handle(Base* base, uint64_t inodeIndex, const Inode& inode);

    /* Call before destroying the handle; there might be operations pending: */
    int cleanup();

    /* Get information about this handle */
    uint64_t inodeIndex() const;
    const Inode& inode() const;

    /* Helper functions for file system operations that work on this inode */

    bool removeOnceUnused() const;

    int& refCount();

    void getAttr(uint64_t* inodeIndex, Inode* inode);

    int link();
    int remove();

    int openDir();
    int findDirent(const char* name, size_t nameLen, uint64_t* direntSlot, uint64_t* direntIndex, Dirent* dirent);
    int findDirentNow(const char* name, size_t nameLen, uint64_t* direntSlot, uint64_t* direntIndex, Dirent* dirent); // no locking!
    int readDirent(uint64_t direntSlot, Dirent* dirent);
    int readDirentPlus(uint64_t direntSlot, Dirent* dirent, Inode* inode);

    int mkdirent(const char* name, size_t nameLen, uint64_t existingInodeIndex, std::function<Inode (const Inode& parentInode)> inodeCreator);
    int rmdirent(const char* name, size_t nameLen, std::function<int (const Inode& inode)> inodeChecker);

    int readlink(char* buf, size_t bufsize);

    int chmod(uint16_t mode);
    int chown(uint32_t uid, uint32_t gid);
    int utimens(bool updateAtime, const Time& atime, bool updateMtime, const Time& mtime, bool updateCtime, const Time& ctime);
    int truncate(uint64_t length);

    int open(bool readOnly, bool trunc, bool append);
    int read(uint64_t offset, unsigned char* buf, size_t count);
    int write(uint64_t offset, const unsigned char* buf, size_t count);

    int renameHelperAdd(uint64_t direntSlot, uint64_t direntIndex);
    int renameHelperRemove(uint64_t direntSlot);
    int renameHelperReplace(uint64_t direntSlot, uint64_t newDirentIndex);
};
