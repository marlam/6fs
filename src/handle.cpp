/*
 * Copyright (C) 2023, 2024, 2025
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

#include "handle.hpp"
#include "base.hpp"
#include "index.hpp"
#include "emergency.hpp"
#include "logger.hpp"


static uint64_t slotCount(const Inode& inode)
{
    uint64_t ret;
    if (inode.type() == TypeREG) {
        ret = inode.size / sizeof(Block) + (inode.size % sizeof(Block) != 0 ? 1 : 0);
    } else if (inode.type() == TypeDIR) {
        ret = inode.size;
    } else {
        ret = 0;
    }
    return ret;
}

Handle::Handle(Base* base, uint64_t inodeIndex, const Inode& inode) :
    _base(base),
    _inodeIndex(inodeIndex),
    _inode(inode),
    _slotCount(::slotCount(_inode)),
    _readOnly(false),
    _append(false),
    _refCount(0),
    _removeOnceUnused(false),
    _cachedBlockIndices { InvalidIndex, InvalidIndex, InvalidIndex, InvalidIndex }
{
    static_assert(sizeof(Block) % sizeof(uint64_t) == 0);
}

void Handle::lockExclusive()
{
    _mutex.lock();
}

void Handle::unlockExclusive()
{
    _mutex.unlock();
}

void Handle::lockShared()
{
    _mutex.lock_shared();
}

void Handle::unlockShared()
{
    _mutex.unlock_shared();
}

bool Handle::updateATime()
{
    bool updated = false;
    Time now = Time::now();
    Time nowMinus24hrs = now;
    nowMinus24hrs.seconds -= 60 * 60 * 24;
    if (_inode.atime.isOlderThan(_inode.ctime)
            || _inode.atime.isOlderThan(_inode.mtime)
            || _inode.atime.isOlderThan(nowMinus24hrs)) {
        _inode.atime = now;
        updated = true;
    }
    return updated;
}

void Handle::slotToTreeIndices(uint64_t slot, int* tree, uint64_t ijkl[4])
{
    if (slot == 0) {
        *tree = 0;
        ijkl[0] = InvalidIndex;
        ijkl[1] = InvalidIndex;
        ijkl[2] = InvalidIndex;
        ijkl[3] = InvalidIndex;
        return;
    }
    slot -= 1;
    if (slot < N) {
        *tree = 1;
        ijkl[0] = slot;
        ijkl[1] = InvalidIndex;
        ijkl[2] = InvalidIndex;
        ijkl[3] = InvalidIndex;
        return;
    }
    slot -= N;
    if (slot < N * N) {
        *tree = 2;
        ijkl[0] = slot / N;
        slot -= ijkl[0] * N;
        ijkl[1] = slot % N;
        ijkl[2] = InvalidIndex;
        ijkl[3] = InvalidIndex;
        return;
    }
    slot -= N * N;
    if (slot < N * N * N) {
        *tree = 3;
        ijkl[0] = slot / (N * N);
        slot -= ijkl[0] * (N * N);
        ijkl[1] = slot / N;
        slot -= ijkl[1] * N;
        ijkl[2] = slot % N;
        ijkl[3] = InvalidIndex;
        return;
    }
    slot -= N * N * N;
    *tree = 4;
    ijkl[0] = slot / (N * N * N);
    slot -= ijkl[0] * (N * N * N);
    ijkl[1] = slot / (N * N);
    slot -= ijkl[1] * (N * N);
    ijkl[2] = slot / N;
    slot -= ijkl[2] * N;
    ijkl[3] = slot % N;
}

int Handle::cacheBlock(int treeLevel, uint64_t blockIndex)
{
    int r = 0;
    if (_cachedBlockIndices[treeLevel] != blockIndex) {
        r = _base->blockRead(blockIndex, &(_cachedBlocks[treeLevel]));
        _cachedBlockIndices[treeLevel] = (r == 0 ? blockIndex : InvalidIndex);
    }
    return r;
}

uint64_t Handle::slotCount() const
{
    return _slotCount;
}

int Handle::getSlot(uint64_t slot, uint64_t* index)
{
    if (slot >= slotCount()) {
        logger.log(Logger::Error, "Handle::getSlot(%lu) failed because we only have %lu slots", slot, slotCount());
        emergency(EmergencyBug);
        return -ENOTRECOVERABLE;
    }

    int tree;
    uint64_t ijkl[4];
    slotToTreeIndices(slot, &tree, ijkl);

    if (tree == 0) {
        *index = _inode.slotTrees[0];
        return 0;
    }

    uint64_t blockIndex = _inode.slotTrees[tree];
    for (int l = 0; l < tree; l++) {
        if (blockIndex == InvalidIndex) {
            *index = InvalidIndex;
            break;
        }
        int r = cacheBlock(l, blockIndex);
        if (r < 0)
            return r;
        if (l == tree - 1) {
            *index = _cachedBlocks[l].indices[ijkl[l]];
            break;
        }
        blockIndex = _cachedBlocks[l].indices[ijkl[l]];
    }

    return 0;
}

int Handle::setSlot(uint64_t slot, uint64_t index)
{
    if (slot >= slotCount()) {
        logger.log(Logger::Error, "Handle::setSlot(%lu, %lu) failed because we only have %lu slots", slot, index, slotCount());
        emergency(EmergencyBug);
        return -ENOTRECOVERABLE;
    }

    int tree;
    uint64_t ijkl[4];
    slotToTreeIndices(slot, &tree, ijkl);

    if (tree == 0) {
        _inode.slotTrees[0] = index;
        return 0;
    }

    int r;
    uint64_t blockIndex = _inode.slotTrees[tree];
    for (int l = 0; l < tree; l++) {
        if (blockIndex == InvalidIndex) {
            _cachedBlocks[l].initializeIndices();
            r = _base->blockAdd(&blockIndex, &(_cachedBlocks[l]));
            if (r < 0) {
                _cachedBlockIndices[l] = InvalidIndex;
                return r;
            }
            _cachedBlockIndices[l] = blockIndex;
            if (l > 0) {
                _cachedBlocks[l - 1].indices[ijkl[l - 1]] = blockIndex;
                r = _base->blockWrite(_cachedBlockIndices[l - 1], &(_cachedBlocks[l - 1]));
                if (r < 0)
                    return r;
            } else {
                _inode.slotTrees[tree] = blockIndex;
            }
        }
        r = cacheBlock(l, blockIndex);
        if (r < 0)
            return r;
        if (l == tree - 1) {
            uint64_t oldI = _cachedBlocks[l].indices[ijkl[l]];
            _cachedBlocks[l].indices[ijkl[l]] = index;
            bool allEntriesInvalid = (index == InvalidIndex);
            for (uint64_t j = 0; allEntriesInvalid && j < N; j++)
                if (_cachedBlocks[l].indices[j] != InvalidIndex)
                    allEntriesInvalid = false;
            if (allEntriesInvalid) {
                // delete this indirection block and, if possible, its predecessors
                for (int ll = l; allEntriesInvalid && ll >= 0; ll--) {
                    r = _base->blockRemove(_cachedBlockIndices[ll]);
                    if (r < 0)
                        return r;
                    _cachedBlockIndices[ll] = InvalidIndex;
                    if (ll > 0) {
                        _cachedBlocks[ll - 1].indices[ijkl[ll - 1]] = InvalidIndex;
                        for (uint64_t j = 0; allEntriesInvalid && j < N; j++)
                            if (_cachedBlocks[ll - 1].indices[j] != InvalidIndex)
                                allEntriesInvalid = false;
                        if (!allEntriesInvalid) {
                            r = _base->blockWrite(_cachedBlockIndices[ll - 1], &(_cachedBlocks[ll - 1]));
                            if (r < 0)
                                return r;
                        }
                    } else {
                        _inode.slotTrees[tree] = InvalidIndex;
                    }
                }
            } else {
                r = _base->blockWrite(_cachedBlockIndices[l], &(_cachedBlocks[l]));
                if (r < 0) {
                    _cachedBlocks[l].indices[ijkl[l]] = oldI;
                    return r;
                }
            }
            break;
        }
        blockIndex = _cachedBlocks[l].indices[ijkl[l]];
    }

    return 0;
}

int Handle::insertSlot(uint64_t slot, uint64_t index)
{
    if (slot >= slotCount() + 1) {
        logger.log(Logger::Error, "Handle::insertSlot(%lu, %lu): invalid slot %lu (we have %lu)", slot, index, slot, slotCount());
        emergency(EmergencyBug);
        return -ENOTRECOVERABLE;
    }

    if (slotCount() == maxSlotCount)
        return -ENOSPC;

    _slotCount++;

    int r = 0;
    for (int64_t i = int64_t(slotCount()) - 1; i > int64_t(slot); i--) {
        uint64_t tmp;
        r = getSlot(i - 1, &tmp);
        if (r < 0)
            return r;
        r = setSlot(i, tmp);
        if (r < 0)
            return r;
    }
    r = setSlot(slot, index);
    return r;
}

int Handle::removeSlot(uint64_t slot, bool removeDirentOrBlock)
{
    if (slot >= slotCount()) {
        logger.log(Logger::Error, "Handle::removeSlot(%lu): invalid slot %lu (we have %lu)", slot, slot, slotCount());
        emergency(EmergencyBug);
        return -ENOTRECOVERABLE;
    }

    int r;

    uint64_t index = InvalidIndex;
    if (removeDirentOrBlock) {
        r = getSlot(slot, &index);
        if (r < 0)
            return r;
    }

    for (uint64_t i = slot; i < slotCount() - 1; i++) {
        uint64_t tmp;
        r = getSlot(i + 1, &tmp);
        if (r < 0)
            return r;
        r = setSlot(i, tmp);
        if (r < 0)
            return r;
    }
    r = setSlot(slotCount() - 1, InvalidIndex);
    if (r < 0)
        return r;

    if (removeDirentOrBlock && index != InvalidIndex) {
        r = -EINVAL;
        if (_inode.type() == TypeDIR)
            r = _base->direntRemove(index);
        else if (_inode.type() == TypeREG)
            r = _base->blockRemove(index);
        if (r < 0)
            return r;
    }

    _slotCount--;
    return 0;
}

uint64_t Handle::inodeIndex() const
{
    return _inodeIndex;
}

const Inode& Handle::inode() const
{
    return _inode;
}

bool Handle::removeOnceUnused() const
{
    return _removeOnceUnused;
}

int& Handle::refCount()
{
    return _refCount;
}

void Handle::getAttr(uint64_t* inodeIndex, Inode* inode)
{
    lockExclusive();
    *inodeIndex = _inodeIndex;
    *inode = _inode;
    unlockExclusive();
}

int Handle::truncateNow(uint64_t length)
{
    int r = 0;
    if (length != _inode.size) {
        uint64_t origSize = _inode.size;
        uint64_t origBlockCount = slotCount();
        // remove or add blocks until the block count matches the new length
        uint64_t newBlockCount = length / sizeof(Block) + (length % sizeof(Block) != 0 ? 1 : 0);
        while (r == 0 && newBlockCount < slotCount()) {
            r = removeSlot(slotCount() - 1, true);
        }
        while (r == 0 && newBlockCount > slotCount()) {
            r = insertSlot(slotCount(), InvalidIndex);
        }
        // write zeroes where necessary
        if (r == 0 && length > _inode.size && origSize % sizeof(Block) != 0) {
            uint64_t lastOrigBlockSlot = origBlockCount - 1;
            uint64_t lastOrigBlockValidDataSize = _inode.size % sizeof(Block);
            uint64_t lastOrigBlockIndex;
            r = getSlot(lastOrigBlockSlot, &lastOrigBlockIndex);
            if (r == 0) {
                if (lastOrigBlockIndex != InvalidIndex) {
                    Block lastOrigBlock;
                    r = _base->blockRead(lastOrigBlockIndex, &lastOrigBlock);
                    if (r == 0) {
                        memset(lastOrigBlock.data + lastOrigBlockValidDataSize, 0, sizeof(Block) - lastOrigBlockValidDataSize);
                        r = _base->blockWrite(lastOrigBlockIndex, &lastOrigBlock);
                    }
                }
            }
        }
        if (r == 0) {
            _inode.size = length;
        }
    }
    return r;
}

int Handle::link()
{
    int r = 0;
    if (_inode.type() != TypeREG)
        r = -EINVAL;
    if (_inode.nlink == std::numeric_limits<uint64_t>::max())
        r = -EMLINK;
    if (r == 0) {
        lockExclusive();
        Time oldCtime = _inode.ctime;
        _inode.nlink++;
        _inode.ctime = Time::now();
        r = _base->inodeWrite(_inodeIndex, &_inode);
        if (r < 0) {
            _inode.nlink--;
            _inode.ctime = oldCtime;
        }
        unlockExclusive();
    }
    return r;
}

int Handle::remove()
{
    int r = 0;
    lockExclusive();
    if (refCount() <= 0) {
        r = removeNow();
    } else {
        _removeOnceUnused = true;
    }
    unlockExclusive();
    return r;
}

int Handle::removeNow()
{
    int r = 0;
    if (_inode.type() == TypeREG) {
        if (_inode.nlink == 0) {
            logger.log(Logger::Error, "Handle::removeNow(): inode.nlink was zero before we decreased it");
            emergency(EmergencyBug);
            r = -ENOTRECOVERABLE;
        } else {
            _inode.nlink--;
            if (_inode.nlink == 0) {
                r = _base->inodeRemove(_inodeIndex);
                /* The simple and direct way to delete all data blocks and indirection blocks is this:
                 * while (r == 0 && slotCount() > 0) {
                 *     r = removeSlot(slotCount() - 1, true);
                 * }
                 * But that rewrites (and reencrypts) indirection blocks for large files a lot,
                 * and we never need those blocks again.
                 * So we simply go over all blocks and remove them, and then have to remove
                 * the indirection blocks whenever a new one appears. */
                uint64_t lastRemovedIndirectionBlock[4] = { InvalidIndex, InvalidIndex, InvalidIndex, InvalidIndex };
                for (uint64_t slot = 0; r == 0 && slot < slotCount(); slot++) {
                    uint64_t blockIndex;
                    r = getSlot(slot, &blockIndex);
                    if (r == 0 && blockIndex != InvalidIndex)
                        r = _base->blockRemove(blockIndex);
                    for (int l = 0; r == 0 && l < 4; l++) {
                        if (_cachedBlockIndices[l] != lastRemovedIndirectionBlock[l]) {
                            if (_cachedBlockIndices[l] != InvalidIndex)
                                r = _base->blockRemove(_cachedBlockIndices[l]);
                            lastRemovedIndirectionBlock[l] = _cachedBlockIndices[l];
                        }
                    }
                }
            } else {
                _inode.ctime = Time::now();
                r = _base->inodeWrite(_inodeIndex, &_inode);
            }
        }
    } else if (_inode.type() == TypeLNK) {
        r = _base->inodeRemove(_inodeIndex);
        if (r == 0)
            r = _base->blockRemove(_inode.slotTrees[0]);
    } else {
        r = _base->inodeRemove(_inodeIndex);
    }
    return r;
}

int Handle::mkdirent(const char* name, size_t nameLen, uint64_t existingInodeIndex, std::function<Inode (const Inode& parentInode)> inodeCreator)
{
    int r = 0;
    if (r == 0 && _inode.type() != TypeDIR)
        r = -ENOTDIR;
    if (r == 0 && nameLen >= sizeof(Dirent::name))
        r = -ENAMETOOLONG;
    if (r == 0 && _inode.nlink == std::numeric_limits<uint64_t>::max())
        r = -EMLINK;
    if (r == 0 && slotCount() == maxSlotCount)
        r = -ENOSPC;
    if (r != 0)
        return r;

    lockExclusive();

    uint64_t direntSlot;
    uint64_t tmpDirentIndex;
    Dirent tmpDirent;
    if (r == 0)
        r = findDirentNow(name, nameLen, &direntSlot, &tmpDirentIndex, &tmpDirent);
    if (r == 0)
        r = -EEXIST;
    else if (r == -ENOENT)
        r = 0;

    uint64_t newInodeIndex = existingInodeIndex;
    if (r == 0 && newInodeIndex == InvalidIndex) {
        Inode newInode = inodeCreator(_inode);
        r = _base->inodeAdd(&newInodeIndex, &newInode);
    }

    Dirent dirent;
    uint64_t direntIndex;
    if (r == 0) {
        memcpy(dirent.name, name, nameLen);
        // dirent.name is already zero-terminated because the name was initialized with zeros
        dirent.inodeIndex = newInodeIndex;
        r = _base->direntAdd(&direntIndex, &dirent);
    }

    if (r == 0)
        r = insertSlot(direntSlot, direntIndex);

    if (r == 0) {
        _inode.size++;
        Time t = Time::now();
        _inode.mtime = t;
        _inode.ctime = t;
        _inode.nlink++;
        r = _base->inodeWrite(_inodeIndex, &_inode);
    }

    unlockExclusive();
    return r;
}

int Handle::rmdirent(const char* name, size_t nameLen, std::function<int (const Inode& inode)> inodeChecker)
{
    int r = 0;
    if (r == 0 && _inode.type() != TypeDIR)
        r = -ENOTDIR;
    if (r == 0 && nameLen >= sizeof(Dirent::name))
        r = -ENAMETOOLONG;
    if (r == 0 && _inode.nlink == 2 /* minimum for "." and ".." */)
        r = -ENOENT;
    if (r != 0)
        return r;

    lockExclusive();

    uint64_t direntSlot;
    uint64_t direntIndex;
    Dirent dirent;
    Handle* handle = nullptr;
    if (r == 0)
        r = findDirentNow(name, nameLen, &direntSlot, &direntIndex, &dirent);
    if (r == 0)
        r = _base->handleGet(dirent.inodeIndex, &handle);
    if (r == 0)
        r = inodeChecker(handle->inode());
    if (r == 0)
        r = removeSlot(direntSlot, true);
    if (r == 0)
        r = handle->remove();
    if (handle)
        r = _base->handleRelease(handle);
    if (r == 0) {
        _inode.size--;
        Time t = Time::now();
        _inode.mtime = t;
        _inode.ctime = t;
        _inode.nlink--;
        r = _base->inodeWrite(_inodeIndex, &_inode);
    }

    unlockExclusive();
    return r;
}

int Handle::readlink(char* buf, size_t bufsize)
{
    int r = 0;
    if (_inode.type() != TypeLNK)
        r = -EINVAL;

    if (r == 0) {
        lockExclusive();
        Block block;
        r = _base->blockRead(_inode.slotTrees[0], &block);
        if (r == 0) {
            size_t bytesToCopy = std::min(bufsize - 1, size_t(_inode.size));
            memcpy(buf, block.target, bytesToCopy);
            buf[bytesToCopy] = '\0';
            Inode oldInode = _inode;
            if (updateATime()) {
                r = _base->inodeWrite(_inodeIndex, &_inode);
                if (r != 0)
                    _inode = oldInode;
            }
        }
        unlockExclusive();
    }

    return r;
}

int Handle::chmod(uint32_t mode)
{
    lockExclusive();
    Inode oldInode = _inode;
    _inode.typeAndMode = (_inode.typeAndMode & TypeMask) | mode;
    _inode.ctime = Time::now();
    int r = _base->inodeWrite(_inodeIndex, &_inode);
    if (r != 0)
        _inode = oldInode;
    unlockExclusive();
    return r;
}

int Handle::chown(uint32_t uid, uint32_t gid)
{
    lockExclusive();
    Inode oldInode = _inode;
    _inode.uid = uid;
    _inode.gid = gid;
    _inode.typeAndMode &= ~(ModeSUID | ModeSGID);
    _inode.ctime = Time::now();
    int r = _base->inodeWrite(_inodeIndex, &_inode);
    if (r != 0)
        _inode = oldInode;
    unlockExclusive();
    return r;
}

int Handle::utimens(bool updateAtime, const Time& atime, bool updateMtime, const Time& mtime, bool updateCtime, const Time& ctime)
{
    if (!updateAtime && !updateMtime && !updateCtime)
        return 0;

    lockExclusive();
    Inode oldInode = _inode;
    if (updateAtime)
        _inode.atime = atime;
    if (updateMtime)
        _inode.mtime = mtime;
    if (updateCtime)
        _inode.ctime = ctime;
    int r = _base->inodeWrite(_inodeIndex, &_inode);
    if (r != 0)
        _inode = oldInode;
    unlockExclusive();
    return r;
}

int Handle::truncate(uint64_t length)
{
    lockExclusive();
    int r = truncateNow(length);
    if (r == 0) {
        _inode.typeAndMode &= ~(ModeSUID | ModeSGID);
        Time time = Time::now();
        _inode.mtime = time;
        _inode.ctime = time;
        r = _base->inodeWrite(_inodeIndex, &_inode);
    }
    unlockExclusive();
    return r;
}

int Handle::openDir()
{
    int r = 0;
    if (_inode.type() != TypeDIR)
        r = -ENOTDIR;
    if (r == 0) {
        lockExclusive();
        Inode oldInode = _inode;
        if (updateATime()) {
            r = _base->inodeWrite(_inodeIndex, &_inode);
            if (r != 0)
                _inode = oldInode;
        }
        unlockExclusive();
    }
    return r;
}

int Handle::findDirent(const char* name, size_t nameLen, uint64_t* direntSlot, uint64_t* direntIndex, Dirent* dirent)
{
    lockShared();
    int r = findDirentNow(name, nameLen, direntSlot, direntIndex, dirent);
    unlockShared();
    return r;
}

int Handle::findDirentNow(const char* name, size_t nameLen, uint64_t* direntSlot, uint64_t* direntIndex, Dirent* dirent)
{
    char nameCopy[sizeof(Dirent::name)];
    strncpy(nameCopy, name, nameLen);
    nameCopy[nameLen] = '\0';

    // Binary search
    int r = 0;
    int64_t a = 0;
    int64_t b = int64_t(slotCount()) - 1;
    bool found = false;
    while (b >= a) {
        int64_t c = (a + b) / 2;
        r = getSlot(c, direntIndex);
        if (r < 0)
            break;
        r = _base->direntRead(*direntIndex, dirent);
        if (r < 0)
            break;
        int cmp = strcmp(nameCopy, dirent->name);
        if (cmp > 0) {
            a = c + 1;
        } else if (cmp < 0) {
            b = c - 1;
        } else {
            *direntSlot = c;
            found = true;
            break;
        }
    }
    if (r == 0) {
        if (!found) {
            *direntSlot = a;
            r = -ENOENT;
        }
    }
    return r;
}

int Handle::readDirent(uint64_t direntSlot, Dirent* dirent)
{
    int r = 0;
    if (direntSlot >= slotCount())
        r = -EINVAL;
    if (r == 0) {
        lockExclusive();
        uint64_t direntIndex;
        r = getSlot(direntSlot, &direntIndex);
        if (r == 0)
            r = _base->direntRead(direntIndex, dirent);
        unlockExclusive();
    }
    return r;
}

int Handle::readDirentPlus(uint64_t direntSlot, Dirent* dirent, Inode* inode)
{
    int r = 0;
    if (direntSlot >= slotCount())
        r = -EINVAL;
    if (r == 0) {
        lockExclusive();
        uint64_t direntIndex;
        r = getSlot(direntSlot, &direntIndex);
        if (r == 0)
            r = _base->direntRead(direntIndex, dirent);
        if (r == 0)
            r = _base->inodeRead(dirent->inodeIndex, inode);
        unlockExclusive();
    }
    return r;
}

int Handle::open(bool readOnly, bool trunc, bool append)
{
    int r = 0;
    if (_inode.type() != TypeREG)
        r = -EINVAL;
    if (r == 0) {
        lockExclusive();
        readOnly = readOnly;
        append = append;
        if (trunc && _inode.size != 0)
            r = truncateNow(0);
        if (r == 0) {
            bool updated = false;
            if (readOnly) {
                updated = updateATime();
            } else {
                Time t = Time::now();
                _inode.mtime = t;
                _inode.ctime = t;
                _inode.typeAndMode &= ~(ModeSUID | ModeSGID);
                updated = true;
            }
            if (updated)
                r = _base->inodeWrite(_inodeIndex, &_inode);
        }
        unlockExclusive();
    }
    return r;
}

int Handle::read(uint64_t offset, unsigned char* buf, size_t count)
{
    lockExclusive();

    if (offset >= _inode.size)
        count = 0;
    else if (offset + count > _inode.size)
        count = _inode.size - offset;
    int ret = count;

    Block block;
    int r = 0;

    while (count > 0) {
        uint64_t blockSlot = offset / sizeof(block);
        uint64_t blockIndex;
        r = getSlot(blockSlot, &blockIndex);
        if (r < 0)
            break;
        if (blockIndex == InvalidIndex)
            block.initializeData();
        else
            r = _base->blockRead(blockIndex, &block);
        if (r < 0)
            break;
        size_t blockOffset = offset % sizeof(block);
        size_t len = std::min(count, sizeof(block) - blockOffset);
        memcpy(buf, block.data + blockOffset, len);
        offset += len;
        buf += len;
        count -= len;
    }

    unlockExclusive();
    return (r < 0 ? r : ret);
}

int Handle::write(uint64_t offset, const unsigned char* buf, size_t count)
{
    lockExclusive();

    Inode origInode = _inode;
    int ret = count;

    Block block;
    int r = 0;

    if (_append)
        offset = _inode.size;

    if (offset > _inode.size)
        r = truncateNow(offset);

    while (r == 0 && count > 0) {
        uint64_t blockIndex = InvalidIndex;
        uint64_t blockSlot = offset / sizeof(block);
        size_t blockOffset = offset % sizeof(block);
        size_t len = std::min(count, sizeof(block) - blockOffset);

        if (blockSlot >= maxSlotCount) {
            r = -ENOSPC;
            break;
        }
        if (blockSlot > slotCount()) {
            logger.log(Logger::Error, "Handle::write(): blockSlot %lu is too large for block count %lu", blockSlot, slotCount());
            emergency(EmergencyBug);
            r = -ENOTRECOVERABLE;
            break;
        }
        if (blockSlot < slotCount()) {
            r = getSlot(blockSlot, &blockIndex);
            if (r < 0)
                break;
        }
        if (blockIndex == InvalidIndex) {
            if (!(blockOffset == 0 && len == sizeof(block)))
                block.initializeData();
            memcpy(block.data + blockOffset, buf, len);
            r = _base->blockAdd(&blockIndex, &block);
            if (r == 0) {
                if (blockSlot == slotCount()) {
                    r = insertSlot(blockSlot, blockIndex);
                } else {
                    r = setSlot(blockSlot, blockIndex);
                }
            }
        } else {
            if (!(blockOffset == 0 && len == sizeof(block))) {
                r = _base->blockRead(blockIndex, &block);
                if (r < 0)
                    break;
            }
            memcpy(block.data + blockOffset, buf, len);
            r = _base->blockWrite(blockIndex, &block);
        }
        if (r < 0)
            break;

        if (offset + len > _inode.size)
            _inode.size = offset + len;

        offset += len;
        buf += len;
        count -= len;
    }

    if (memcmp(&_inode, &origInode, sizeof(Inode)) != 0) {
        r = _base->inodeWrite(_inodeIndex, &_inode);
    }

    unlockExclusive();
    return (r < 0 ? r : ret);
}

int Handle::renameHelperAdd(uint64_t direntSlot, uint64_t direntIndex)
{
    lockExclusive();
    int r = insertSlot(direntSlot, direntIndex);
    if (r == 0) {
        _inode.size++;
        r = _base->inodeWrite(_inodeIndex, &_inode);
    }
    unlockExclusive();
    return r;
}

int Handle::renameHelperRemove(uint64_t direntSlot)
{
    lockExclusive();
    int r = removeSlot(direntSlot, false);
    if (r == 0) {
        _inode.size--;
        r = _base->inodeWrite(_inodeIndex, &_inode);
    }
    unlockExclusive();
    return r;
}

int Handle::renameHelperReplace(uint64_t direntSlot, uint64_t newDirentIndex)
{
    lockExclusive();
    int r = setSlot(direntSlot, newDirentIndex);
    if (r == 0) {
        r = _base->inodeWrite(_inodeIndex, &_inode);
    }
    unlockExclusive();
    return r;
}
