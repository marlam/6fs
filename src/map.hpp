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

#include <cstdint>

#include "storage.hpp"


class Map {
private:
    Storage* _storage;
    uint64_t _bitChunksInStorage;
    uint64_t _currentBitChunk;
    uint64_t _currentBitChunkIndex;
    bool _currentBitChunkModified;
    uint64_t _firstZeroCandidate;
    uint64_t _lastOne;

    int setCurrentBitChunkIndex(uint64_t index);

public:
    Map(Storage* storage);

    int initialize();

    int firstZero(uint64_t* index);
    int set(uint64_t index, bool b);
    int get(uint64_t index, bool* b);

    int setZero(uint64_t index) { return set(index, false); }
    int setOne(uint64_t index) { return set(index, true); }

    int sync();

    uint64_t storageSizeInBytes() const;
};
