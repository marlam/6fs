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

#include <mutex>

#include <cerrno>
#include <cstring>

#include "logger.hpp"
#include "emergency.hpp"
#include "chunk.hpp"


ChunkManager::ChunkManager(Map* map, Storage* storage, size_t chunkSize, bool punchHolesForEmptyChunks) :
    _map(map),
    _chunks(storage),
    _punchHolesForEmptyChunks(punchHolesForEmptyChunks),
    _chunksInStorage(0)
{
    _chunks->setChunkSize(chunkSize);
}

ChunkManager::~ChunkManager()
{
}

size_t ChunkManager::chunkSize() const
{
    return _chunks->chunkSize();
}

int ChunkManager::initialize()
{
    int r = _map->initialize();
    if (r == 0)
        r = _chunks->size(&_chunksInStorage);
    return r;
}

uint64_t ChunkManager::chunksInStorage() const
{
    uint64_t ret = _chunksInStorage;
    return ret;
}

int ChunkManager::sync()
{
    std::unique_lock<std::shared_mutex> lock(_rwMutex);
    int r = _map->sync();
    return r;
}

int ChunkManager::add(uint64_t* index, const void* buf)
{
    std::unique_lock<std::shared_mutex> lock(_rwMutex);

    int r = _map->firstZero(index);
    if (r == 0)
        r = _map->setOne(*index);
    if (r == 0 && *index >= _chunksInStorage) {
        _chunksInStorage = *index + 1;
        r = _chunks->setSize(_chunksInStorage);
        if (r < 0) {
            int r2 = _map->setZero(*index);
            if (r2 < 0) {
                logger.log(Logger::Error, "ChunkManager::add(): cannot recover from failure to set storage size; a dead chunk remains: %s", strerror(-r2));
            }
        }
    }
    if (r == 0) {
        r = _chunks->write(*index, 1, buf);
        if (r < 0) {
            int r2 = _map->setZero(*index);
            if (r2 == 0 && *index + 1 == _chunksInStorage) {
                _chunksInStorage--;
                r2 = _chunks->setSize(_chunksInStorage);
            }
            if (r2 < 0) {
                logger.log(Logger::Error, "ChunkManager::add(): cannot recover from failure to write chunk; a dead chunk remains: %s", strerror(-r2));
            }
        }
    }

    return r;
}

int ChunkManager::remove(uint64_t index)
{
    std::unique_lock<std::shared_mutex> lock(_rwMutex);

    if (index >= _chunksInStorage) {
        logger.log(Logger::Error, "ChunkManager::remove(): cannot remove chunk %lu (size %zu) because only %lu are in storage",
                index, chunkSize(), _chunksInStorage);
        emergency(EmergencyBug);
        return -ENOTRECOVERABLE;
    }

    int r = _map->setZero(index);
    if (r == 0) {
        if (index + 1 == _chunksInStorage) {
            // we are at the end of the storage; remove this empty chunk and all
            // preceding empty chunks to save storage space
            _chunksInStorage--;
            while (index > 0) {
                index--;
                bool b;
                int r2 = _map->get(index, &b);
                if (r2 < 0) {
                    logger.log(Logger::Error, "ChunkManager::remove(): cannot determine how many empty chunks to remove: %s", strerror(-r2));
                    emergency(EmergencySystemFailure);
                    r = -ENOTRECOVERABLE;
                    break;
                }
                if (b)
                    break;
                _chunksInStorage--;
            }
            if (r == 0) {
                int r2 = _chunks->setSize(_chunksInStorage);
                if (r2 < 0) {
                    logger.log(Logger::Error, "ChunkManager::remove(): cannot remove empty chunks: %s", strerror(-r2));
                    emergency(EmergencySystemFailure);
                    r = -ENOTRECOVERABLE;
                }
            }
        } else {
            // we are in the middle of the storage; punch a hole for the empty chunk
            if (_punchHolesForEmptyChunks) {
                int r2 = _chunks->punchHole(index, 1);
                if (r2 < 0) {
                    logger.log(Logger::Error, "ChunkManager::remove(): cannot punch hole; ignoring this error: %s", strerror(-r2));
                }
            }
        }
    }

    return r;
}

int ChunkManager::read(uint64_t index, void* buf)
{
    std::shared_lock<std::shared_mutex> lock(_rwMutex);

    int r = 0;
    if (index >= _chunksInStorage) {
        logger.log(Logger::Error, "ChunkManager::read(): cannot read chunk %lu (size %zu) because only %lu are in storage",
                index, chunkSize(), _chunksInStorage);
        emergency(EmergencyBug);
        r = -ENOTRECOVERABLE;
    }
    if (r == 0)
        r = _chunks->read(index, 1, buf);
    return r;
}

int ChunkManager::write(uint64_t index, const void* buf)
{
    std::shared_lock<std::shared_mutex> lock(_rwMutex);

    int r = 0;
    if (index >= _chunksInStorage) {
        logger.log(Logger::Error, "ChunkManager::write(): cannot write chunk %lu (size %zu) because only %lu are in storage",
                index, chunkSize(), _chunksInStorage);
        emergency(EmergencyBug);
        r = -ENOTRECOVERABLE;
    }
    if (r == 0)
        r = _chunks->write(index, 1, buf);
    return r;
}

uint64_t ChunkManager::storageSizeInBytes() const
{
    uint64_t ret = _chunksInStorage * _chunks->chunkSize() + _map->storageSizeInBytes();
    return ret;
}
