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

#include <shared_mutex>

#include "storage.hpp"
#include "map.hpp"


class ChunkManager
{
private:
    std::shared_mutex _rwMutex;
    Map* _map;
    Storage* _chunks;
    bool _punchHolesForEmptyChunks;
    uint64_t _chunksInStorage;

public:
    ChunkManager(Map* map, Storage* storage, size_t chunkSize, bool punchHolesForEmptyChunks);
    virtual ~ChunkManager();

    // must be called first; not thread safe
    virtual int initialize();

    // can always be called from any contex
    uint64_t chunksInStorage() const;
    size_t chunkSize() const;

    int add(uint64_t* index, const void* buf);
    int remove(uint64_t index);
    int read(uint64_t index, void* buf);
    int write(uint64_t index, const void* buf);

    int sync();
    uint64_t storageSizeInBytes() const;
};
