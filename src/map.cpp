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

#include <cerrno>
#include <cstring>

#include "logger.hpp"
#include "emergency.hpp"
#include "index.hpp"
#include "map.hpp"


Map::Map(Storage* storage) :
    _storage(storage),
    _bitChunksInStorage(0),
    _currentBitChunk(0),
    _currentBitChunkIndex(InvalidIndex),
    _currentBitChunkModified(false),
    _firstZeroCandidate(0)
{
    static_assert(sizeof(uint64_t) == sizeof(unsigned long long));
    _storage->setChunkSize(sizeof(uint64_t));
}

static uint64_t toBitChunkIndex(uint64_t index)
{
    return index / 64;
}

static uint64_t toBitIndex(uint64_t index)
{
    return index % 64;
}

int Map::initialize()
{
    int r = _storage->size(&_bitChunksInStorage);
    if (r < 0)
        return r;
    if (_bitChunksInStorage == 0) {
        _currentBitChunk = 0;
        _bitChunksInStorage = 1;
        r = _storage->setSize(_bitChunksInStorage);
    } else {
        r = _storage->read(0, 1, &_currentBitChunk);
    }
    if (r < 0)
        return r;
    _currentBitChunkIndex = 0;
    return 0;
}

int Map::setCurrentBitChunkIndex(uint64_t bitChunkIndex)
{
    if (bitChunkIndex == _currentBitChunkIndex)
        return 0;

    int r = sync();
    if (r < 0)
        return r;
    if (bitChunkIndex >= _bitChunksInStorage) {
        _currentBitChunk = 0;
        _bitChunksInStorage = bitChunkIndex + 1;
        r = _storage->setSize(_bitChunksInStorage);
    } else {
        r = _storage->read(bitChunkIndex, 1, &_currentBitChunk);
    }
    if (r < 0) {
        logger.log(Logger::Error, "Map::setCurrentBitChunkIndex(%lu) failed", bitChunkIndex);
        return r;
    }
    _currentBitChunkIndex = bitChunkIndex;
    return 0;
}

int Map::firstZero(uint64_t* index)
{
    int r;
    uint64_t bitChunkIndex = toBitChunkIndex(_firstZeroCandidate);
    uint64_t bitIndex = 0;
    for (;;) {
        r = setCurrentBitChunkIndex(bitChunkIndex);
        if (r < 0) {
            logger.log(Logger::Error, "Map::firstZero() failed");
            return r;
        }
        if (~_currentBitChunk != 0) {
            // the index of the first zero is the number of trailing ones
            // i.e. the number of trailing zeroes in the negated bit chunk
            bitIndex = __builtin_ctzll(~_currentBitChunk);
            break;
        }
        bitChunkIndex++;
    }
    _firstZeroCandidate = bitChunkIndex * 64 + bitIndex;
    *index = _firstZeroCandidate;
    return 0;
}

int Map::set(uint64_t index, bool b)
{
    int r = setCurrentBitChunkIndex(toBitChunkIndex(index));
    if (r < 0) {
        logger.log(Logger::Error, "Map::set(%lu, %d) failed", index, b ? 1 : 0);
        return r;
    }
    uint64_t previousBitChunk = _currentBitChunk;
    uint64_t mask = 1ULL << toBitIndex(index);
    if (b) {
        _currentBitChunk |= mask;
        if (index == _firstZeroCandidate)
            _firstZeroCandidate++;
    } else {
        _currentBitChunk &= ~mask;
        if (index < _firstZeroCandidate)
            _firstZeroCandidate = index;
    }
    _currentBitChunkModified = (previousBitChunk != _currentBitChunk);
    return 0;
}

int Map::get(uint64_t index, bool* b)
{
    int r = setCurrentBitChunkIndex(toBitChunkIndex(index));
    if (r < 0) {
        logger.log(Logger::Error, "Map::get(%lu) failed", index);
        return r;
    }
    uint64_t mask = 1ULL << toBitIndex(index);
    *b = (_currentBitChunk & mask);
    return 0;
}

int Map::sync()
{
    int r = 0;

    if (_currentBitChunkIndex >= _bitChunksInStorage) {
        logger.log(Logger::Error, "Map::sync(): invalid bit chunk index");
        emergency(EmergencyBug);
        return -ENOTRECOVERABLE;
    }

    if (_currentBitChunk == 0 && _currentBitChunkIndex + 1 == _bitChunksInStorage) {
        // we are at the end of the storage; remove this empty chunk and all
        // preceding empty chunks to save storage space
        // but leave at least one chunk in storage even if it is empty
        _bitChunksInStorage--;
        while (_currentBitChunkIndex > 1) {
            _currentBitChunkIndex--;
            r = _storage->read(_currentBitChunkIndex, 1, &_currentBitChunk);
            if (r < 0 || _currentBitChunk != 0)
                break;
        }
        if (r == 0)
            r = _storage->setSize(_bitChunksInStorage);
    } else if (_currentBitChunkModified) {
        r = _storage->write(_currentBitChunkIndex, 1, &_currentBitChunk);
    }

    if (r < 0) {
        logger.log(Logger::Error, "Map::sync() failed: %s", strerror(-r));
        return r;
    }

    _currentBitChunkModified = false;
    return 0;
}

uint64_t Map::storageSizeInBytes() const
{
    return _bitChunksInStorage * _storage->chunkSize();
}
