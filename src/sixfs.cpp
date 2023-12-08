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

#include <cstring>

#include "index.hpp"
#include "logger.hpp"
#include "emergency.hpp"
#include "base.hpp"
#include "sixfs.hpp"


SixFS::SixFS(const std::string& dirName, uint64_t maxSize,
            const std::vector<unsigned char>& key) :
    _dirName(dirName),
    _maxSize(maxSize),
    _key(key),
    _base(nullptr)
{
}

SixFS::~SixFS()
{
    unmount();
}

bool SixFS::isRemote() const
{
    return _dirName.size() == 0;
}

void SixFS::structureLockExclusive()
{
    _base->structureLockExclusive();
}

void SixFS::structureUnlockExclusive()
{
    _base->structureUnlockExclusive();
}

void SixFS::structureLockShared()
{
    _base->structureLockShared();
}

void SixFS::structureUnlockShared()
{
    _base->structureUnlockShared();
}

int SixFS::getHandle(uint64_t inodeIndex, Handle** handle)
{
    return _base->handleGet(inodeIndex, handle);
}

int SixFS::getHandle(const char* path, size_t pathLen, Handle** handle)
{
    *handle = nullptr;
    uint64_t inodeIndex;
    int r = findInode(path, pathLen, &inodeIndex);
    if (r == 0)
        r = getHandle(inodeIndex, handle);
    return r;
}

int SixFS::getHandle(const char* path, Handle** handle)
{
    return getHandle(path, strlen(path), handle);
}

int SixFS::releaseHandle(Handle* handle)
{
    return _base->handleRelease(handle);
}

void SixFS::separate(const char* path, size_t len, size_t* parentLen, size_t* nameOffset, size_t* nameLen)
{
    *parentLen = len - 1;
    while (*parentLen > 0) {
        if (path[*parentLen] == '/')
            break;
        (*parentLen)--;
    }
    if (*parentLen == 0) {
        *parentLen = 1;
    }
    *nameOffset = (*parentLen == 1 ? 1 : *parentLen + 1);
    *nameLen = len - *nameOffset;
}

int SixFS::recursiveFind(const char* path, size_t len, uint64_t* inodeIndex)
{
    if (len == 1) {
        *inodeIndex = 0; // root directory inode
        return 0;
    } else {
        *inodeIndex = InvalidIndex;

        int r;

        size_t parentLen, nameOffset, nameLen;
        separate(path, len, &parentLen, &nameOffset, &nameLen);
        if (nameLen >= sizeof(Dirent::name))
            return -ENAMETOOLONG;

        uint64_t parentIndex;
        r = recursiveFind(path, parentLen, &parentIndex);
        if (r < 0)
            return r;

        Inode parentInode;
        r = _base->inodeRead(parentIndex, &parentInode);
        if (r < 0)
            return r;
        if (parentInode.type() != TypeDIR)
            return -ENOTDIR;
        uint64_t direntSlot;
        uint64_t direntIndex;
        Dirent dirent;
        Handle handle(_base, parentIndex, parentInode);
        r = handle.findDirentNow(path + nameOffset, nameLen, &direntSlot, &direntIndex, &dirent);
        if (r < 0)
            return r;

        *inodeIndex = dirent.inodeIndex;
        return 0;
    }
}

int SixFS::findInode(const char* path, size_t pathLen, uint64_t* inodeIndex)
{
    int r = (path[0] != '/' ? -ENOENT : recursiveFind(path, pathLen, inodeIndex));
    //logger.log(Logger::Debug, "SixFS::findInode(\"%.*s\"): %s", int(pathLen), path, (r == 0 ? "success" : strerror(-r)));
    return r;
}

int SixFS::mkdirent(const char* path, uint64_t existingInodeIndex, std::function<Inode (const Inode& parentInode)> inodeCreator)
{
    int r;
    size_t pathLen = strlen(path);
    size_t parentLen, nameOffset, nameLen;
    separate(path, pathLen, &parentLen, &nameOffset, &nameLen);

    Handle* parentHandle = nullptr;
    r = getHandle(path, parentLen, &parentHandle);
    if (r == 0)
        r = parentHandle->mkdirent(path + nameOffset, nameLen, existingInodeIndex, inodeCreator);
    if (parentHandle) {
        int r2 = releaseHandle(parentHandle);
        if (r == 0 && r2 < 0)
            r = r2;
    }

    return r;
}

int SixFS::rmdirent(const char* path, std::function<int (const Inode& inode)> inodeChecker)
{
    int r;
    size_t pathLen = strlen(path);
    size_t parentLen, nameOffset, nameLen;
    separate(path, pathLen, &parentLen, &nameOffset, &nameLen);

    Handle* parentHandle = nullptr;
    r = getHandle(path, parentLen, &parentHandle);
    if (r == 0)
        r = parentHandle->rmdirent(path + nameOffset, nameLen, inodeChecker);
    if (parentHandle) {
        int r2 = releaseHandle(parentHandle);
        if (r == 0 && r2 < 0)
            r = r2;
    }

    return r;
}

int SixFS::mount(std::string& errStr)
{
    _base = new Base(_dirName, _maxSize, _key);
    bool needsRootNode = false;
    int r = _base->initialize(errStr, &needsRootNode);
    if (r == 0 && needsRootNode) {
        r = _base->createRootNode();
    }
    if (r < 0) {
        delete _base;
        _base = nullptr;
    }
    return r;
}

int SixFS::unmount()
{
    int r = 0;
    if (_base) {
        r = _base->cleanup();
        delete _base;
        _base = nullptr;
    }
    return r;
}

int SixFS::statfs(size_t* blockSize, size_t* maxNameLen,
        uint64_t* maxBlockCount, uint64_t* freeBlockCount, uint64_t* maxInodeCount, uint64_t* freeInodeCount)
{
    int r = _base->statfs(blockSize, maxNameLen, maxBlockCount, freeBlockCount, maxInodeCount, freeInodeCount);
    logger.log(Logger::Debug, "  SixFS::statfs(): %s", r == 0 ? "success" : strerror(-r));
    return r;
}

int SixFS::getAttr(Handle* handle, const char* path, uint64_t* inodeIndex, Inode* inode)
{
    int r = 0;
    if (handle) {
        handle->getAttr(inodeIndex, inode);
    } else {
        structureLockShared();
        Handle* h = nullptr;
        if (r == 0)
            r = getHandle(path, &h);
        if (r == 0)
            h->getAttr(inodeIndex, inode);
        if (h) {
            int r2 = releaseHandle(h);
            if (r == 0 && r2 < 0)
                r = r2;
        }
        structureUnlockShared();
    }
    logger.log(Logger::Debug, "  SixFS::getAttr(\"%s\"): inode=%lu: %s", path,
            (r == 0 ? *inodeIndex : InvalidIndex),
            (r == 0 ? "success" : strerror(-r)));
    return r;
}

int SixFS::openDir(const char* path, Handle** handle)
{
    structureLockShared();
    *handle = nullptr;
    int r = getHandle(path, handle);
    if (r == 0)
        r = (*handle)->openDir();
    if (r < 0 && *handle) {
        int r2 = releaseHandle(*handle);
        if (r2 < 0)
            logger.log(Logger::Error, "SixFS::openDir(): unhandled error after failure: %s", strerror(-r2));
        *handle = nullptr;
    }
    logger.log(Logger::Debug, "  SixFS::openDir(\"%s\"): inode=%lu: %s", path,
            (r == 0 ? (*handle)->inodeIndex() : InvalidIndex),
            (r == 0 ? "success" : strerror(-r)));
    structureUnlockShared();
    return r;
}

int SixFS::closeDir(Handle* handle)
{
    structureLockExclusive(); // the directory might be deleted on close, therefore we need the exclusive lock
    uint64_t inodeIndex = handle->inodeIndex();
    int r = releaseHandle(handle);
    logger.log(Logger::Debug, "  SixFS::closeDir(%lu): %s", inodeIndex, (r == 0 ? "success" : strerror(-r)));
    structureUnlockExclusive();
    return r;
}

int SixFS::readDirent(Handle* handle, uint64_t direntSlot, Dirent* dirent)
{
    structureLockShared();
    int r = handle->readDirent(direntSlot, dirent);
    logger.log(Logger::Debug, "  SixFS::readDirent(%lu, %lu): name=\"%s\" inode=%lu: %s",
            handle->inodeIndex(), direntSlot,
            (r == 0 ? dirent->name : ""),
            (r == 0 ? dirent->inodeIndex : InvalidIndex),
            (r == 0 ? "success" : strerror(-r)));
    structureUnlockShared();
    return r;
}

int SixFS::readDirentPlus(Handle* handle, uint64_t direntSlot, Dirent* dirent, Inode* inode)
{
    structureLockShared();
    int r = handle->readDirentPlus(direntSlot, dirent, inode);
    logger.log(Logger::Debug, "  SixFS::readDirentPlus(%lu, %lu): name=\"%s\" inode=%lu: %s",
            handle->inodeIndex(), direntSlot,
            (r == 0 ? dirent->name : ""),
            (r == 0 ? dirent->inodeIndex : InvalidIndex),
            (r == 0 ? "success" : strerror(-r)));
    structureUnlockShared();
    return r;
}

int SixFS::mkdir(const char* path, uint16_t typeAndMode)
{
    structureLockExclusive();
    int r = mkdirent(path, InvalidIndex,
                [&typeAndMode](const Inode& parentInode) { return Inode::directory(&parentInode, typeAndMode); });
    logger.log(Logger::Debug, "  SixFS::mkdir(\"%s\"): %s", path, (r == 0 ? "success" : strerror(-r)));
    structureUnlockExclusive();
    return r;
}

int SixFS::rmdir(const char* path)
{
    structureLockExclusive();
    int r = rmdirent(path, [](const Inode& inode) {
                if (inode.type() != TypeDIR)
                    return -ENOTDIR;
                if (inode.size > 0)
                    return -ENOTEMPTY;
                return 0;
                });
    logger.log(Logger::Debug, "  SixFS::rmdir(\"%s\"): %s", path, (r == 0 ? "success" : strerror(-r)));
    structureUnlockExclusive();
    return r;
}

int SixFS::mknod(const char* path, uint16_t typeAndMode, uint64_t rdev)
{
    structureLockExclusive();
    int r = mkdirent(path, InvalidIndex,
                [&typeAndMode, &rdev](const Inode&) { return Inode::node(typeAndMode, rdev); });
    logger.log(Logger::Debug, "  SixFS::mknod(\"%s\"): %s", path, (r == 0 ? "success" : strerror(-r)));
    structureUnlockExclusive();
    return r;
}

int SixFS::unlink(const char* path)
{
    structureLockExclusive();
    int r = rmdirent(path, [](const Inode& inode) {
                if (inode.type() == TypeDIR)
                    return -EISDIR;
                return 0;
                });
    logger.log(Logger::Debug, "  SixFS::unlink(\"%s\"): %s", path, (r == 0 ? "success" : strerror(-r)));
    structureUnlockExclusive();
    return r;
}

int SixFS::symlink(const char* target, const char* linkpath)
{
    structureLockExclusive();

    size_t targetLen = strlen(target);
    uint64_t blockIndex;

    int r = (targetLen > sizeof(Block::target) ? -ENAMETOOLONG : 0);
    if (r == 0) {
        Block block;
        block.initializeTarget();
        memcpy(block.target, target, targetLen);
        r = _base->blockAdd(&blockIndex, &block);
    }
    if (r == 0) {
        r = mkdirent(linkpath, InvalidIndex,
                [&targetLen, &blockIndex](const Inode&) { return Inode::symlink(targetLen, blockIndex); });
        if (r < 0) {
            int r2 = _base->blockRemove(blockIndex);
            if (r2 < 0) {
                logger.log(Logger::Error, "SixFS::symlink(): cannot recover from failure; a dead block remains: %s", strerror(-r2));
            }
        }
    }

    logger.log(Logger::Debug, "  SixFS::symlink(\"%s\", \"%s\"): %s", target, linkpath, (r == 0 ? "success" : strerror(-r)));
    structureUnlockExclusive();
    return r;
}

int SixFS::readlink(const char* path, char* buf, size_t bufsize)
{
    structureLockShared();
    Handle* handle = nullptr;
    int r = getHandle(path, &handle);
    if (r == 0)
        r = handle->readlink(buf, bufsize);
    if (handle) {
        int r2 = releaseHandle(handle);
        if (r == 0 && r2 < 0)
            r = r2;
    }
    logger.log(Logger::Debug, "  SixFS::readlink(\"%s\"): %s", path, (r == 0 ? "success" : strerror(-r)));
    structureUnlockShared();
    return r;
}

int SixFS::link(const char* oldpath, const char* newpath)
{
    structureLockExclusive();

    Handle* handle;
    int r = getHandle(oldpath, &handle);
    if (r == 0)
        r = handle->link();
    if (r == 0) {
        r = mkdirent(newpath, handle->inodeIndex(), [](const Inode&) { return Inode(); });
        if (r < 0) {
            int r2 = handle->remove();
            if (r2 < 0) {
                logger.log(Logger::Error, "SixFS::link(): cannot recover from failure: %s", strerror(-r2));
                emergency(EmergencySystemFailure);
                r = -ENOTRECOVERABLE;
            }
        }
    }
    if (handle) {
        int r2 = releaseHandle(handle);
        if (r == 0 && r2 < 0)
            r = r2;
    }
    logger.log(Logger::Debug, "  SixFS::link(\"%s\", \"%s\"): %s", oldpath, newpath, (r == 0 ? "success" : strerror(-r)));
    structureUnlockExclusive();
    return r;
}

int SixFS::rename(const char* oldPath, const char* newPath, RenameMode mode)
{
    structureLockExclusive();

    size_t oldPathLen = strlen(oldPath);
    size_t oldParentLen, oldNameOffset, oldNameLen;
    separate(oldPath, oldPathLen, &oldParentLen, &oldNameOffset, &oldNameLen);
    size_t newPathLen = strlen(newPath);
    size_t newParentLen, newNameOffset, newNameLen;
    separate(newPath, newPathLen, &newParentLen, &newNameOffset, &newNameLen);
    int r = 0;
    if (oldNameLen >= sizeof(Dirent::name) || newNameLen >= sizeof(Dirent::name))
        r = -ENAMETOOLONG;

    Handle* oldParentHandle = nullptr;
    if (r == 0)
        r = getHandle(oldPath, oldParentLen, &oldParentHandle);
    if (r == 0 && oldParentHandle->inode().type() != TypeDIR)
        r = -ENOTDIR;

    Handle* newParentHandle = nullptr;
    uint64_t newParentInodeIndex = InvalidIndex;
    if (r == 0)
        r = findInode(newPath, newParentLen, &newParentInodeIndex);
    if (r == 0) {
        if (newParentInodeIndex == oldParentHandle->inodeIndex())
            newParentHandle = oldParentHandle;
        else
            r = getHandle(newPath, newParentLen, &newParentHandle);
    }
    if (r == 0 && newParentHandle->inode().type() != TypeDIR)
        r = -ENOTDIR;

    uint64_t oldDirentSlot;
    uint64_t oldDirentIndex;
    Dirent oldDirent;
    if (r == 0)
        r = oldParentHandle->findDirent(oldPath + oldNameOffset, oldNameLen, &oldDirentSlot, &oldDirentIndex, &oldDirent);

    uint64_t newDirentSlot = InvalidIndex;
    uint64_t newDirentIndex = InvalidIndex;
    Dirent newDirent;
    bool newPathExists = false;
    if (r == 0) {
        r = newParentHandle->findDirent(newPath + newNameOffset, newNameLen, &newDirentSlot, &newDirentIndex, &newDirent);
        newPathExists = (r == 0);
        if (r == -ENOENT)
            r = 0;
    }

    Inode oldInode;
    if (r == 0)
        r = _base->inodeRead(oldDirent.inodeIndex, &oldInode);
    Inode newInode;
    if (r == 0 && newPathExists)
        r = _base->inodeRead(newDirent.inodeIndex, &newInode);

    if (r == 0 && newPathExists && oldInode.type() == TypeDIR && newInode.type() != TypeDIR)
        r = -ENOTDIR;
    if (r == 0 && newPathExists && oldInode.type() == TypeDIR && newInode.type() != TypeDIR && newInode.size > 0)
        r = -ENOTEMPTY;
    if (r == 0 && newPathExists && oldInode.type() != TypeDIR && newInode.type() == TypeDIR)
        r = -EISDIR;
    if (r == 0 && newPathExists && mode == RenameNoreplace)
        r = -EEXIST;
    if (r == 0 && !newPathExists && mode == RenameExchange)
        r = -ENOENT;

    if (r == 0) {
        if (newPathExists && oldInode.type() == TypeREG && (oldDirent.inodeIndex == newDirent.inodeIndex)) {
            // both are hard links to the same file; do nothing
        } else {
            bool undo = false;
            switch (mode) {
            case RenameNoreplace:
            case RenameNormal:
                if (r == 0) {
                    memset(oldDirent.name, 0, sizeof(Dirent::name));
                    memcpy(oldDirent.name, newPath + newNameOffset, newNameLen);
                    oldDirent.name[newNameLen] = '\0';
                    r = _base->direntWrite(oldDirentIndex, &oldDirent);
                }
                if (r == 0) {
                    if (newPathExists) {
                        r = newParentHandle->renameHelperReplace(newDirentSlot, oldDirentIndex);
                        if (r == 0) {
                            int r2 = _base->direntRemove(newDirentIndex);
                            if (r2 < 0) {
                                logger.log(Logger::Error, "SixFS::rename(): cannot remove old directory entry; it remains: %s", strerror(-r2));
                            }
                            Handle* handle = nullptr;
                            int r3 = getHandle(newDirent.inodeIndex, &handle);
                            if (r3 < 0) {
                                logger.log(Logger::Error, "SixFS::rename(): cannot get handle for old inode; it remains: %s", strerror(-r3));
                            } else {
                                int r4 = handle->remove();
                                if (r4 < 0) {
                                    logger.log(Logger::Error, "SixFS::rename(): cannot remove old inode; it remains: %s", strerror(-r4));
                                }
                                int r5 = releaseHandle(handle);
                                if (r5 < 0) {
                                    logger.log(Logger::Error, "SixFS::rename(): removing old inode might have failed: %s", strerror(-r5));
                                }
                            }
                        }
                        if (r < 0)
                            undo = true;
                    } else {
                        r = newParentHandle->renameHelperAdd(newDirentSlot, oldDirentIndex);
                        if (r < 0)
                            undo = true;
                        if (r == 0) {
                            if (oldParentHandle->inodeIndex() == newParentHandle->inodeIndex()) {
                                // special case: same parent directory
                                if (oldDirentSlot >= newDirentSlot)
                                    oldDirentSlot++;
                            }
                        }
                    }
                }
                if (r == 0) {
                    r = oldParentHandle->renameHelperRemove(oldDirentSlot);
                    if (r < 0)
                        undo = true;
                }
                if (undo) {
                    memset(oldDirent.name, 0, sizeof(Dirent::name));
                    memcpy(oldDirent.name, oldPath + oldNameOffset, oldNameLen);
                    oldDirent.name[oldNameLen] = '\0';
                    int r2 = _base->direntWrite(oldDirentIndex, &oldDirent);
                    if (r2 < 0) {
                        logger.log(Logger::Error, "SixFS::rename(): cannot recover from failure: %s", strerror(-r2));
                        emergency(EmergencySystemFailure);
                        r = -ENOTRECOVERABLE;
                    }
                }
                break;
            case RenameExchange:
                r = oldParentHandle->renameHelperReplace(oldDirentSlot, newDirentIndex);
                if (r == 0) {
                    r = newParentHandle->renameHelperReplace(newDirentSlot, oldDirentIndex);
                    if (r < 0) {
                        int r2 = oldParentHandle->renameHelperReplace(oldDirentSlot, oldDirentIndex);
                        if (r2 < 0) {
                            logger.log(Logger::Error, "SixFS::rename(): cannot recover from failure: %s", strerror(-r2));
                            emergency(EmergencySystemFailure);
                            r = -ENOTRECOVERABLE;
                        }
                    }
                }
                break;
            }
        }
    }
    if (oldParentHandle) {
        int r2 = releaseHandle(oldParentHandle);
        if (r2 < 0)
            logger.log(Logger::Error, "sixfs::rename(): error on old parent handle (ignored): %s", strerror(-r2));
    }
    if (newParentHandle && newParentHandle != oldParentHandle) {
        int r2 = releaseHandle(newParentHandle);
        if (r2 < 0)
            logger.log(Logger::Error, "sixfs::rename(): error on new parent handle (ignored): %s", strerror(-r2));
    }

    logger.log(Logger::Debug, "  SixFS::rename(\"%s\", \"%s\"): %s", oldPath, newPath, (r == 0 ? "success" : strerror(-r)));
    structureUnlockExclusive();
    return r;
}

int SixFS::chmod(Handle* handle, const char* path, uint16_t mode)
{
    structureLockShared();
    int r = 0;
    Handle* h = handle;
    if (!h)
        r = getHandle(path, &h);
    if (r == 0)
        r = h->chmod(mode);
    if (h != handle) {
        int r2 = releaseHandle(h);
        if (r == 0 && r2 < 0)
            r = r2;
    }
    logger.log(Logger::Debug, "  SixFS::chmod(\"%s\", 0%ho): %s", path, mode,
            (r == 0 ? "success" : strerror(-r)));
    structureUnlockShared();
    return r;
}

int SixFS::chown(Handle* handle, const char* path, uint32_t uid, uint32_t gid)
{
    structureLockShared();
    int r = 0;
    Handle* h = handle;
    if (!h)
        r = getHandle(path, &h);
    if (r == 0)
        r = h->chown(uid, gid);
    if (h != handle) {
        int r2 = releaseHandle(h);
        if (r == 0 && r2 < 0)
            r = r2;
    }
    logger.log(Logger::Debug, "  SixFS::chown(\"%s\", %u, %u): %s", path, uid, gid,
            (r == 0 ? "success" : strerror(-r)));
    structureUnlockShared();
    return r;
}

int SixFS::utimens(Handle* handle, const char* path, bool updateAtime, const Time& atime, bool updateMtime, const Time& mtime, bool updateCtime, const Time& ctime)
{
    structureLockShared();
    int r = 0;
    Handle* h = handle;
    if (!h)
        r = getHandle(path, &h);
    if (r == 0)
        r = h->utimens(updateAtime, atime, updateMtime, mtime, updateCtime, ctime);
    if (h != handle) {
        int r2 = releaseHandle(h);
        if (r == 0 && r2 < 0)
            r = r2;
    }
    logger.log(Logger::Debug, "  SixFS::utimens(\"%s\"): %s", path, (r == 0 ? "success" : strerror(-r)));
    structureUnlockShared();
    return r;
}

int SixFS::truncate(Handle* handle, const char* path, uint64_t length)
{
    structureLockShared();
    int r = 0;
    Handle* h = handle;
    if (!h)
        r = getHandle(path, &h);
    if (r == 0 && handle->inode().type() != TypeREG)
        r = -EINVAL;
    if (r == 0)
        r = h->truncate(length);
    if (h != handle) {
        int r2 = releaseHandle(h);
        if (r == 0 && r2 < 0)
            r = r2;
    }
    logger.log(Logger::Debug, "  SixFS::truncate(\"%s\", %lu): %s", path, length,
            (r == 0 ? "success" : strerror(-r)));
    structureUnlockShared();
    return r;
}

int SixFS::open(const char* path, bool readOnly, bool trunc, bool append, Handle** handle)
{
    structureLockExclusive();
    *handle = nullptr;
    int r = getHandle(path, handle);
    if (r == 0)
        r = (*handle)->open(readOnly, trunc, append);
    if (r < 0 && *handle) {
        int r2 = releaseHandle(*handle);
        if (r2 < 0) {
            logger.log(Logger::Error, "SixFS::open(): unhandled error after failure: %s", strerror(-r2));
        }
        *handle = nullptr;
    }
    logger.log(Logger::Debug, "  SixFS::open(\"%s\", %s, %s, %s): inode=%lu: %s", path,
            readOnly ? "ro" : "rw",
            trunc ? "trunc" : "notrunc",
            append ? "append" : "noappend",
            (r == 0 ? (*handle)->inodeIndex() : InvalidIndex),
            (r == 0 ? "success" : strerror(-r)));
    structureUnlockExclusive();
    return r;
}

int SixFS::close(Handle* handle)
{
    structureLockExclusive(); // the file might be deleted, therefore we need the exclusive lock
    uint64_t inodeIndex = handle->inodeIndex();
    int r = releaseHandle(handle);
    logger.log(Logger::Debug, "  SixFS::close(%lu): %s", inodeIndex, (r == 0 ? "success" : strerror(-r)));
    structureUnlockExclusive();
    return r;
}

int SixFS::read(Handle* handle, uint64_t offset, unsigned char* buf, size_t count)
{
    int r = handle->read(offset, buf, count);
    logger.log(Logger::Debug, "  SixFS::read(%lu, offset=%lu, count=%zu): %d (%s)", handle->inodeIndex(), offset, count,
            r, (r < 0 ? strerror(-r) : "success"));
    return r;
}

int SixFS::write(Handle* handle, uint64_t offset, const unsigned char* buf, size_t count)
{
    int r = handle->write(offset, buf, count);
    logger.log(Logger::Debug, "  SixFS::write(%lu, offset=%lu, count=%zu): %d (%s)", handle->inodeIndex(), offset, count,
            r, (r < 0 ? strerror(-r) : "success"));
    return r;
}
