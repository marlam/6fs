/*
 * Copyright (C) 2023
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

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <shared_mutex>

#include "inode.hpp"
#include "dirent.hpp"
#include "block.hpp"
#include "handle.hpp"
#include "base.hpp"


class SixFS
{
private:
    const std::string _dirName;
    const uint64_t _maxSize;
    const std::vector<unsigned char> _key;
    Base* _base;

    /* Helper functions for locking */
    void structureLockExclusive();
    void structureUnlockExclusive();
    void structureLockShared();
    void structureUnlockShared();

    /* Helper functions to get a handle from the Base */
    int getHandle(uint64_t inodeIndex, Handle** handle);
    int getHandle(const char* path, size_t pathLen, Handle** handle);
    int getHandle(const char* path, Handle** handle);
    int releaseHandle(Handle* handle); // might return errors associated with the inode

    /* Lookup functions. The caller must hold the structure lock. */
    static void separate(const char* path, size_t len, size_t* parentLen, size_t* nameOffset, size_t* nameLen);
    int recursiveFind(const char* path, size_t len, uint64_t* inodeIndex); // only to be called from findInode()!
    int findInode(const char* path, size_t pathLen, uint64_t* inodeIndex);

    /* Helper functions to create or remove directory entries. The caller must hold the structure lock. */
    int mkdirent(const char* path, uint64_t existingInodeIndex, std::function<Inode (const Inode& parentInode)> inodeCreator);
    int rmdirent(const char* path, std::function<int (const Inode& inode)> inodeChecker);

public:
    SixFS(const std::string& dirName, uint64_t maxSize, const std::vector<unsigned char>& key);
    ~SixFS();

    bool isRemote() const;

    int mount(std::string& errStr);
    int unmount();

    int statfs(size_t* blockSize, size_t* maxNameLen,
            uint64_t* maxBlockCount, uint64_t* freeBlockCount, uint64_t* maxInodeCount, uint64_t* freeInodeCount);

    int getAttr(Handle* handle, const char* path, uint64_t* inodeIndex, Inode* inode);

    int mkdir(const char* path, uint16_t typeAndMode);
    int rmdir(const char* path);

    int mknod(const char* path, uint16_t typeAndMode, uint64_t rdev);
    int unlink(const char* path);

    int symlink(const char* target, const char* linkpath);
    int readlink(const char* path, char* buf, size_t bufsize);

    int link(const char* oldpath, const char* newpath);

    typedef enum { RenameNormal, RenameNoreplace, RenameExchange } RenameMode;
    int rename(const char* oldpath, const char* newpath, RenameMode mode);

    int chmod(Handle* handle, const char* path, uint16_t mode);
    int chown(Handle* handle, const char* path, uint32_t uid, uint32_t gid);
    int utimens(Handle* handle, const char* path, bool updateAtime, const Time& atime, bool updateMtime, const Time& mtime, bool updateCtime, const Time& ctime);
    int truncate(Handle* handle, const char* path, uint64_t length);

    int openDir(const char* path, Handle** handle);
    int closeDir(Handle* handle);
    int readDirent(Handle* handle, uint64_t direntSlot, Dirent* dirent);
    int readDirentPlus(Handle* handle, uint64_t direntSlot, Dirent* dirent, Inode* inode);

    int open(const char* path, bool readOnly, bool trunc, bool append, Handle** handle);
    int close(Handle* handle);
    int read(Handle* handle, uint64_t offset, unsigned char* buf, size_t count);
    int write(Handle* handle, uint64_t offset, const unsigned char* buf, size_t count);
};
