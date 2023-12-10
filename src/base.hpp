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

#include "chunk.hpp"
#include "inode.hpp"
#include "dirent.hpp"
#include "block.hpp"
#include "handle.hpp"


class Base
{
private:
    const std::string _dirName;
    const uint64_t _maxSize;
    const std::vector<unsigned char> _key;
    const bool _punchHoles;

    Storage* _inodeMapStorage;
    Storage* _inodeChunkStorage;
    Storage* _direntMapStorage;
    Storage* _direntChunkStorage;
    Storage* _blockMapStorage;
    Storage* _blockChunkStorage;
    Map* _inodeMap;
    Map* _direntMap;
    Map* _blockMap;
    ChunkManager* _inodeMgr;
    ChunkManager* _direntMgr;
    ChunkManager* _blockMgr;

    std::shared_mutex _structureMutex; // rw lock for the node/dirent structure

    bool encrypt() const;

    uint64_t storageSizeInBytes() const;
    int checkWriteAction(uint64_t additionalBytes) const;

    int entityAddRaw(ChunkManager* mgr, uint64_t* index, const unsigned char* rawData);
    int entityRemove(ChunkManager* mgr, uint64_t index);
    int entityReadRaw(ChunkManager* mgr, uint64_t index, unsigned char* rawData);
    int entityWriteRaw(ChunkManager* mgr, uint64_t index, const unsigned char* rawData);

    int inodeAddRaw(uint64_t* index, const unsigned char* rawInode);
    int inodeReadRaw(uint64_t index, unsigned char* rawInode);
    int inodeWriteRaw(uint64_t index, const unsigned char* rawInode);
    int direntAddRaw(uint64_t* index, const unsigned char* rawDirent);
    int direntReadRaw(uint64_t index, unsigned char* rawDirent);
    int direntWriteRaw(uint64_t index, const unsigned char* rawDirent);
    int blockAddRaw(uint64_t* index, const unsigned char* rawBlock);
    int blockReadRaw(uint64_t index, unsigned char* rawBlock);
    int blockWriteRaw(uint64_t index, const unsigned char* rawBlock);

    std::shared_mutex _handleMapMutex;
    std::map<uint64_t, Handle*> _handleMap;

public:
    Base(const std::string& dirName, uint64_t maxSize, const std::vector<unsigned char>& key, bool punchHoles);

    int initialize(std::string& errStr, bool* needsRootNode);
    int createRootNode();
    int cleanup();

    void structureLockExclusive();
    void structureUnlockExclusive();
    void structureLockShared();
    void structureUnlockShared();

    int inodeAdd(uint64_t* index, const Inode* inode);
    int inodeRemove(uint64_t index);
    int inodeRead(uint64_t index, Inode* inode);
    int inodeWrite(uint64_t index, const Inode* inode);
    int direntAdd(uint64_t* index, const Dirent* dirent);
    int direntRemove(uint64_t index);
    int direntRead(uint64_t index, Dirent* dirent);
    int direntWrite(uint64_t index, const Dirent* dirent);
    int blockAdd(uint64_t* index, const Block* block);
    int blockRemove(uint64_t index);
    int blockRead(uint64_t index, Block* block);
    int blockWrite(uint64_t index, const Block* block);

    int handleGet(uint64_t inodeIndex, Handle** handle);
    int handleRelease(Handle* handle); // might return errors associated with the inode

    int statfs(size_t* blockSize, size_t* maxNameLen,
            uint64_t* maxBlockCount, uint64_t* freeBlockCount,
            uint64_t* maxInodeCount, uint64_t* freeInodeCount);

    friend class Handle;
};
