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

#include <mutex>

#include "storage_file.hpp"
#include "storage_memory.hpp"
#include "storage_mmap.hpp"
#include "base.hpp"
#include "encrypt.hpp"
#include "logger.hpp"
#include "emergency.hpp"


Base::Base(Storage::Type type, const std::string& dirName, uint64_t maxSize,
        const std::vector<unsigned char>& key, bool punchHoles) :
    _type(type),
    _dirName(dirName),
    _maxSize(maxSize),
    _key(key),
    _punchHoles(punchHoles),
    _inodeMapStorage(nullptr),
    _inodeChunkStorage(nullptr),
    _direntMapStorage(nullptr),
    _direntChunkStorage(nullptr),
    _blockMapStorage(nullptr),
    _blockChunkStorage(nullptr),
    _inodeMap(nullptr),
    _direntMap(nullptr),
    _blockMap(nullptr),
    _inodeMgr(nullptr),
    _direntMgr(nullptr),
    _blockMgr(nullptr)
{
}

bool Base::encrypt() const
{
    return _key.size() == crypto_secretbox_KEYBYTES;
}

uint64_t Base::storageSizeInBytes() const
{
    return _inodeMgr->storageSizeInBytes()
        + _direntMgr->storageSizeInBytes()
        + _blockMgr->storageSizeInBytes();
}

int Base::checkWriteAction(uint64_t additionalBytes) const
{
    int r = 0;
    if (emergencyType != EmergencyNone) {
        r = -EROFS;
    } else if (_maxSize > 0 && additionalBytes > 0) {
        additionalBytes += 4 * sizeof(Block); // for additional indirection blocks
        if (storageSizeInBytes() + additionalBytes > _maxSize)
            r = -ENOSPC;
    }
    return r;
}

int Base::entityAddRaw(ChunkManager* mgr, uint64_t* index, const unsigned char* rawData)
{
    int r = checkWriteAction(mgr->chunkSize());
    if (r == 0)
        r = mgr->add(index, rawData);
    return r;
}

int Base::entityRemove(ChunkManager* mgr, uint64_t index)
{
    int r = checkWriteAction(0);
    if (r == 0)
        r = mgr->remove(index);
    return r;
}

int Base::entityReadRaw(ChunkManager* mgr, uint64_t index, unsigned char* rawData)
{
    return mgr->read(index, reinterpret_cast<void*>(rawData));
}

int Base::entityWriteRaw(ChunkManager* mgr, uint64_t index, const unsigned char* rawData)
{
    int r = mgr->write(index, reinterpret_cast<const void*>(rawData));
    return r;
}

int Base::inodeAddRaw(uint64_t* index, const unsigned char* rawInode)
{
    return entityAddRaw(_inodeMgr, index, rawInode);
}

int Base::inodeRemove(uint64_t index)
{
    return entityRemove(_inodeMgr, index);
}

int Base::inodeReadRaw(uint64_t index, unsigned char* rawInode)
{
    return entityReadRaw(_inodeMgr, index, rawInode);
}

int Base::inodeWriteRaw(uint64_t index, const unsigned char* rawInode)
{
    return entityWriteRaw(_inodeMgr, index, rawInode);
}

int Base::direntAddRaw(uint64_t* index, const unsigned char* rawDirent)
{
    return entityAddRaw(_direntMgr, index, rawDirent);
}

int Base::direntRemove(uint64_t index)
{
    return entityRemove(_direntMgr, index);
}

int Base::direntReadRaw(uint64_t index, unsigned char* rawDirent)
{
    return entityReadRaw(_direntMgr, index, rawDirent);
}

int Base::direntWriteRaw(uint64_t index, const unsigned char* rawDirent)
{
    return entityWriteRaw(_direntMgr, index, rawDirent);
}

int Base::blockAddRaw(uint64_t* index, const unsigned char* rawBlock)
{
    return entityAddRaw(_blockMgr, index, rawBlock);
}

int Base::blockRemove(uint64_t index)
{
    return entityRemove(_blockMgr, index);
}

int Base::blockReadRaw(uint64_t index, unsigned char* rawBlock)
{
    return entityReadRaw(_blockMgr, index, rawBlock);
}

int Base::blockWriteRaw(uint64_t index, const unsigned char* rawBlock)
{
    return entityWriteRaw(_blockMgr, index, rawBlock);
}

int Base::initialize(std::string& errStr, bool* needsRootNode)
{
    switch (_type) {
    case Storage::TypeMmap:
        _inodeMapStorage    = new StorageMmap(_dirName + '/' + "inodemap.6fs");
        _inodeChunkStorage  = new StorageMmap(_dirName + '/' + "inodedat.6fs");
        _direntMapStorage   = new StorageMmap(_dirName + '/' + "direnmap.6fs");
        _direntChunkStorage = new StorageMmap(_dirName + '/' + "direndat.6fs");
        _blockMapStorage    = new StorageMmap(_dirName + '/' + "blockmap.6fs");
        _blockChunkStorage  = new StorageMmap(_dirName + '/' + "blockdat.6fs");
        break;
    case Storage::TypeFile:
        _inodeMapStorage    = new StorageFile(_dirName + '/' + "inodemap.6fs");
        _inodeChunkStorage  = new StorageFile(_dirName + '/' + "inodedat.6fs");
        _direntMapStorage   = new StorageFile(_dirName + '/' + "direnmap.6fs");
        _direntChunkStorage = new StorageFile(_dirName + '/' + "direndat.6fs");
        _blockMapStorage    = new StorageFile(_dirName + '/' + "blockmap.6fs");
        _blockChunkStorage  = new StorageFile(_dirName + '/' + "blockdat.6fs");
        break;
    case Storage::TypeMem:
        _inodeMapStorage = new StorageMemory;
        _inodeChunkStorage = new StorageMemory;
        _direntMapStorage = new StorageMemory;
        _direntChunkStorage = new StorageMemory;
        _blockMapStorage = new StorageMemory;
        _blockChunkStorage = new StorageMemory;
        break;
    }

    int r;
    r = _inodeMapStorage->open();
    if (r == 0)
        r = _inodeChunkStorage->open();
    if (r == 0)
        r = _direntMapStorage->open();
    if (r == 0)
        r = _direntChunkStorage->open();
    if (r == 0)
        r = _blockMapStorage->open();
    if (r == 0)
        r = _blockChunkStorage->open();
    if (r == 0) {
        _inodeMap  = new Map(_inodeMapStorage);
        _direntMap = new Map(_direntMapStorage);
        _blockMap  = new Map(_blockMapStorage);
        _inodeMgr  = new ChunkManager(_inodeMap,  _inodeChunkStorage,  encrypt() ? EncInodeSize  : sizeof(Inode),  false);
        _direntMgr = new ChunkManager(_direntMap, _direntChunkStorage, encrypt() ? EncDirentSize : sizeof(Dirent), false);
        _blockMgr  = new ChunkManager(_blockMap,  _blockChunkStorage,  encrypt() ? EncBlockSize  : sizeof(Block),  _punchHoles);
    }
    if (r == 0)
        r = _inodeMgr->initialize();
    if (r == 0)
        r = _direntMgr->initialize();
    if (r == 0)
        r = _blockMgr->initialize();

    if (r == 0) {
        *needsRootNode = (_inodeMgr->chunksInStorage() == 0);
    }
    if (r == 0 && !(*needsRootNode)) {
        Inode inode;
        r = inodeRead(0, &inode);
        if (r == 0) {
            if (inode.typeAndMode >> 16u) {
                logger.log(Logger::Error, "inodes are in v0 format");
                r = -EBADF;
            }
        }
    }

    if (r < 0) {
        delete _blockMgr;
        _blockMgr = nullptr;
        delete _direntMgr;
        _direntMgr = nullptr;
        delete _inodeMgr;
        _inodeMgr = nullptr;
        delete _blockMap;
        _blockMap = nullptr;
        delete _direntMap;
        _direntMap = nullptr;
        delete _inodeMap;
        _inodeMap = nullptr;
        delete _blockChunkStorage;
        _blockChunkStorage = nullptr;
        delete _blockMapStorage;
        _blockMapStorage = nullptr;
        delete _direntChunkStorage;
        _direntChunkStorage = nullptr;
        delete _direntMapStorage;
        _direntMapStorage = nullptr;
        delete _inodeChunkStorage;
        _inodeChunkStorage = nullptr;
        delete _inodeMapStorage;
        _inodeMapStorage = nullptr;
        errStr = strerror(-r);
    }

    return r;
}

int Base::createRootNode()
{
    uint64_t rootIndex;
    Inode root = Inode::directory(nullptr, ModeRWXU);
    return inodeAdd(&rootIndex, &root);
}

static std::string humanReadableSize(size_t size)
{
    char buf[128];

    if (size >= 1024ULL * 1024ULL * 1024ULL * 1024ULL)
        snprintf(buf, sizeof(buf), "%.2f TiB", size / (double)(1024ULL * 1024ULL * 1024ULL * 1024ULL));
    else if (size >= 1024ULL * 1024ULL * 1024ULL)
        snprintf(buf, sizeof(buf), "%.2f GiB", size / (double)(1024ULL * 1024ULL * 1024ULL));
    else if (size >= 1024ULL * 1024ULL)
        snprintf(buf, sizeof(buf), "%.2f MiB", size / (double)(1024ULL * 1024ULL));
    else if (size >= 1024ULL)
        snprintf(buf, sizeof(buf), "%.2f KiB", size / (double)(1024ULL));
    else
        snprintf(buf, sizeof(buf), "%zu B", size);

    return std::string(buf);
}

int Base::cleanup()
{
    if (!_blockMgr && !_direntMgr && !_inodeMgr)
        return 0;

    // Statistics
    uint64_t inodeBitSetSize = 0, inodeBitSetsIn = 0, inodeBitSetsOut = 0;
    uint64_t inodeSize = 0, inodesIn = 0, inodesOut = 0;
    uint64_t direntBitSetSize = 0, direntBitSetsIn = 0, direntBitSetsOut = 0;
    uint64_t direntSize = 0, direntsIn = 0, direntsOut = 0;
    uint64_t blockBitSetSize = 0, blockBitSetsIn = 0, blockBitSetsOut = 0;
    uint64_t blockSize = 0, blocksIn = 0, blocksOut = 0, blocksPunchedHole = 0;

    // Shutdown / cleanup
    int r[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    if (_blockMgr) {
        r[0] = _blockMgr->sync();
        if (_blockChunkStorage) {
            blockSize = _blockChunkStorage->chunkSize();
            blocksIn = _blockChunkStorage->chunksIn();
            blocksOut = _blockChunkStorage->chunksOut();
            blocksPunchedHole = _blockChunkStorage->chunksPunchedHole();
            r[1] = _blockChunkStorage->close();
            delete _blockChunkStorage;
            _blockChunkStorage = nullptr;
        }
        if (_blockMapStorage) {
            blockBitSetSize = _blockMapStorage->chunkSize();
            blockBitSetsIn = _blockMapStorage->chunksIn();
            blockBitSetsOut = _blockMapStorage->chunksOut();
            r[2] = _blockMapStorage->close();
            delete _blockMapStorage;
            _blockMapStorage = nullptr;
        }
        if (_blockMap) {
            delete _blockMap;
            _blockMap = nullptr;
        }
        delete _blockMgr;
        _blockMgr = nullptr;
    }
    if (_direntMgr) {
        r[3] = _direntMgr->sync();
        if (_direntChunkStorage) {
            direntSize = _direntChunkStorage->chunkSize();
            direntsIn = _direntChunkStorage->chunksIn();
            direntsOut = _direntChunkStorage->chunksOut();
            r[4] = _direntChunkStorage->close();
            delete _direntChunkStorage;
            _direntChunkStorage = nullptr;
        }
        if (_direntMapStorage) {
            direntBitSetSize = _direntMapStorage->chunkSize();
            direntBitSetsIn = _direntMapStorage->chunksIn();
            direntBitSetsOut = _direntMapStorage->chunksOut();
            r[5] = _direntMapStorage->close();
            delete _direntMapStorage;
            _direntMapStorage = nullptr;
        }
        if (_direntMap) {
            delete _direntMap;
            _direntMap = nullptr;
        }
        delete _direntMgr;
        _direntMgr = nullptr;
    }
    if (_inodeMgr) {
        r[6] = _inodeMgr->sync();
        if (_inodeChunkStorage) {
            inodeSize = _inodeChunkStorage->chunkSize();
            inodesIn = _inodeChunkStorage->chunksIn();
            inodesOut = _inodeChunkStorage->chunksOut();
            r[7] = _inodeChunkStorage->close();
            delete _inodeChunkStorage;
            _inodeChunkStorage = nullptr;
        }
        if (_inodeMapStorage) {
            inodeBitSetSize = _inodeMapStorage->chunkSize();
            inodeBitSetsIn = _inodeMapStorage->chunksIn();
            inodeBitSetsOut = _inodeMapStorage->chunksOut();
            r[8] = _inodeMapStorage->close();
            delete _inodeMapStorage;
            _inodeMapStorage = nullptr;
        }
        if (_inodeMap) {
            delete _inodeMap;
            _inodeMap = nullptr;
        }
        delete _inodeMgr;
        _inodeMgr = nullptr;
    }

    // log statistics
    size_t inodeBitSetsInBytes  = inodeBitSetsIn  * inodeBitSetSize;
    size_t inodeBitSetsOutBytes = inodeBitSetsOut * inodeBitSetSize;
    size_t inodesInBytes  = inodesIn  * inodeSize;
    size_t inodesOutBytes = inodesOut * inodeSize;
    size_t direntBitSetsInBytes  = direntBitSetsIn  * direntBitSetSize;
    size_t direntBitSetsOutBytes = direntBitSetsOut * direntBitSetSize;
    size_t direntsInBytes  = direntsIn  * direntSize;
    size_t direntsOutBytes = direntsOut * direntSize;
    size_t blockBitSetsInBytes  = blockBitSetsIn  * blockBitSetSize;
    size_t blockBitSetsOutBytes = blockBitSetsOut * blockBitSetSize;
    size_t blocksInBytes  = blocksIn  * blockSize;
    size_t blocksOutBytes = blocksOut * blockSize;
    size_t totalInBytes = inodeBitSetsInBytes + inodesInBytes + direntBitSetsInBytes + direntsInBytes + blockBitSetsInBytes + blocksInBytes;
    size_t totalOutBytes = inodeBitSetsOutBytes + inodesOutBytes + direntBitSetsOutBytes + direntsOutBytes + blockBitSetsOutBytes + blocksOutBytes;
    logger.log(Logger::Info, "inode bit sets (%lu bytes):", inodeBitSetSize);
    logger.log(Logger::Info, "  in:  %lu (%s)", inodeBitSetsIn,  humanReadableSize(inodeBitSetsInBytes).c_str());
    logger.log(Logger::Info, "  out: %lu (%s)", inodeBitSetsOut, humanReadableSize(inodeBitSetsOutBytes).c_str());
    logger.log(Logger::Info, "inodes (%lu bytes):", inodeSize);
    logger.log(Logger::Info, "  in:  %lu (%s)", inodesIn,  humanReadableSize(inodesInBytes).c_str());
    logger.log(Logger::Info, "  out: %lu (%s)", inodesOut, humanReadableSize(inodesOutBytes).c_str());
    logger.log(Logger::Info, "dirent bit sets (%lu bytes):", direntBitSetSize);
    logger.log(Logger::Info, "  in:  %lu (%s)", direntBitSetsIn,  humanReadableSize(direntBitSetsInBytes).c_str());
    logger.log(Logger::Info, "  out: %lu (%s)", direntBitSetsOut, humanReadableSize(direntBitSetsOutBytes).c_str());
    logger.log(Logger::Info, "dirents (%lu bytes):", direntSize);
    logger.log(Logger::Info, "  in:  %lu (%s)", direntsIn,  humanReadableSize(direntsInBytes).c_str());
    logger.log(Logger::Info, "  out: %lu (%s)", direntsOut, humanReadableSize(direntsOutBytes).c_str());
    logger.log(Logger::Info, "block bit sets (%lu bytes):", blockBitSetSize);
    logger.log(Logger::Info, "  in:  %lu (%s)", blockBitSetsIn,  humanReadableSize(blockBitSetsInBytes).c_str());
    logger.log(Logger::Info, "  out: %lu (%s)", blockBitSetsOut, humanReadableSize(blockBitSetsOutBytes).c_str());
    logger.log(Logger::Info, "blocks (%lu bytes):", blockSize);
    logger.log(Logger::Info, "  in:  %lu (%s)", blocksIn,  humanReadableSize(blocksInBytes).c_str());
    logger.log(Logger::Info, "  out: %lu (%s)", blocksOut, humanReadableSize(blocksOutBytes).c_str());
    logger.log(Logger::Info, "  punched holes: %lu", blocksPunchedHole);
    logger.log(Logger::Info, "grand total:");
    logger.log(Logger::Info, "  in:  %s", humanReadableSize(totalInBytes).c_str());
    logger.log(Logger::Info, "  out: %s", humanReadableSize(totalOutBytes).c_str());

    // return
    int ret = 0;
    for (int i = 0; i < 9; i++) {
        if (r[i] < 0) {
            ret = r[i];
            break;
        }
    }
    return ret;
}

void Base::structureLockExclusive()
{
    _structureMutex.lock();
}

void Base::structureUnlockExclusive()
{
    _structureMutex.unlock();
}

void Base::structureLockShared()
{
    _structureMutex.lock_shared();
}

void Base::structureUnlockShared()
{
    _structureMutex.unlock_shared();
}

int Base::statfs(size_t* blockSize, size_t* maxNameLen,
        uint64_t* maxBlockCount, uint64_t* freeBlockCount,
        uint64_t* maxInodeCount, uint64_t* freeInodeCount)
{
    *blockSize = sizeof(Block);
    *maxNameLen = sizeof(Dirent::name) - 1;
    *maxBlockCount = 0;
    *freeBlockCount = 0;
    *maxInodeCount = 0;
    *freeInodeCount = 0;

    uint64_t storageMaxSize;
    uint64_t storageAvailableSize;
    int r = _blockChunkStorage->stat(&storageMaxSize, &storageAvailableSize);
    if (r == 0) {
        uint64_t maxSize = _maxSize;
        uint64_t currentSize = storageSizeInBytes();
        uint64_t availableSize = (maxSize > currentSize ? maxSize - currentSize : 0);
        if (maxSize == 0) {
            maxSize = storageMaxSize;
            availableSize = storageAvailableSize;
        }
        *maxBlockCount = maxSize / sizeof(Block);
        *freeBlockCount = availableSize / sizeof(Block);
        *maxInodeCount = maxSize / (sizeof(Inode) + sizeof(Dirent));
        *freeInodeCount = availableSize / (sizeof(Inode) + sizeof(Dirent));
    }

    return r;
}

int Base::handleGet(uint64_t inodeIndex, Handle** handle)
{
    std::unique_lock<std::shared_mutex> handleMapLock(_handleMapMutex);
    *handle = nullptr;
    auto it = _handleMap.find(inodeIndex);
    int r = 0;
    if (it != _handleMap.end()) {
        *handle = it->second;
    } else {
        Inode inode;
        r = inodeRead(inodeIndex, &inode);
        if (r == 0) {
            try { *handle = new Handle(this, inodeIndex, inode); }
            catch (...) { r = -ENOMEM; }
        }
        if (r == 0) {
            try { _handleMap.insert(std::pair<uint64_t, Handle*>(inodeIndex, *handle)); }
            catch (...) { r = -ENOMEM; }
        }
        if (r < 0) {
            if (*handle) {
                delete *handle;
                *handle = nullptr;
            }
        }
    }
    if (r == 0) {
        (*handle)->refCount()++;
    }
    return r;
}

int Base::handleRelease(Handle* handle)
{
    std::unique_lock<std::shared_mutex> handleMapLock(_handleMapMutex);
    int r = 0;
    if (handle) {
        handle->refCount()--;
        bool dead = (handle->refCount() == 0);
        if (dead) {
            _handleMap.erase(handle->inodeIndex());
            if (handle->removeOnceUnused())
                r = handle->remove();
            delete handle;
        }
    }
    return r;
}

int Base::inodeAdd(uint64_t* index, const Inode* inode)
{
    int r;
    if (encrypt()) {
        unsigned char buf[EncInodeSize];
        enc(_key.data(), reinterpret_cast<const unsigned char*>(inode), sizeof(Inode), buf);
        r = inodeAddRaw(index, buf);
    } else {
        r = inodeAddRaw(index, reinterpret_cast<const unsigned char*>(inode));
    }
    return r;
}

int Base::inodeRead(uint64_t index, Inode* inode)
{
    int r;
    if (encrypt()) {
        unsigned char buf[EncInodeSize];
        r = inodeReadRaw(index, buf);
        if (r == 0)
            r = dec(_key.data(), buf, EncInodeSize, reinterpret_cast<unsigned char*>(inode), sizeof(Inode));
    } else {
        r = inodeReadRaw(index, reinterpret_cast<unsigned char*>(inode));
    }
    return r;
}

int Base::inodeWrite(uint64_t index, const Inode* inode)
{
    int r;
    if (encrypt()) {
        unsigned char buf[EncInodeSize];
        enc(_key.data(), reinterpret_cast<const unsigned char*>(inode), sizeof(Inode), buf);
        r = inodeWriteRaw(index, buf);
    } else {
        r = inodeWriteRaw(index, reinterpret_cast<const unsigned char*>(inode));
    }
    return r;
}

int Base::direntAdd(uint64_t* index, const Dirent* dirent)
{
    int r;
    if (encrypt()) {
        unsigned char buf[EncDirentSize];
        enc(_key.data(), reinterpret_cast<const unsigned char*>(dirent), sizeof(Dirent), buf);
        r = direntAddRaw(index, buf);
    } else {
        r = direntAddRaw(index, reinterpret_cast<const unsigned char*>(dirent));
    }
    return r;
}

int Base::direntRead(uint64_t index, Dirent* dirent)
{
    int r;
    if (encrypt()) {
        unsigned char buf[EncDirentSize];
        r = direntReadRaw(index, buf);
        if (r == 0)
            r = dec(_key.data(), buf, EncDirentSize, reinterpret_cast<unsigned char*>(dirent), sizeof(Dirent));
    } else {
        r = direntReadRaw(index, reinterpret_cast<unsigned char*>(dirent));
    }
    return r;
}

int Base::direntWrite(uint64_t index, const Dirent* dirent)
{
    int r;
    if (encrypt()) {
        unsigned char buf[EncDirentSize];
        enc(_key.data(), reinterpret_cast<const unsigned char*>(dirent), sizeof(Dirent), buf);
        r = direntWriteRaw(index, buf);
    } else {
        r = direntWriteRaw(index, reinterpret_cast<const unsigned char*>(dirent));
    }
    return r;
}

int Base::blockAdd(uint64_t* index, const Block* block)
{
    int r;
    if (encrypt()) {
        unsigned char buf[EncBlockSize];
        enc(_key.data(), reinterpret_cast<const unsigned char*>(block), sizeof(Block), buf);
        r = blockAddRaw(index, buf);
    } else {
        r = blockAddRaw(index, reinterpret_cast<const unsigned char*>(block));
    }
    return r;
}

int Base::blockRead(uint64_t index, Block* block)
{
    int r;
    if (encrypt()) {
        unsigned char buf[EncBlockSize];
        r = blockReadRaw(index, buf);
        if (r == 0)
            r = dec(_key.data(), buf, EncBlockSize, reinterpret_cast<unsigned char*>(block), sizeof(Block));
    } else {
        r = blockReadRaw(index, reinterpret_cast<unsigned char*>(block));
    }
    return r;
}

int Base::blockWrite(uint64_t index, const Block* block)
{
    int r;
    if (encrypt()) {
        unsigned char buf[EncBlockSize];
        enc(_key.data(), reinterpret_cast<const unsigned char*>(block), sizeof(Block), buf);
        r = blockWriteRaw(index, buf);
    } else {
        r = blockWriteRaw(index, reinterpret_cast<const unsigned char*>(block));
    }
    return r;
}
