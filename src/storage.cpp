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

#include "storage.hpp"

#include <cstdlib>
#include <cstring>
#include <cerrno>


Storage::Storage() :
    _chunkSize(1),
    _chunksIn(0),
    _chunksOut(0),
    _chunksPunchedHole(0)
{
}

Storage::~Storage()
{
}

size_t Storage::chunkSize() const
{
    return _chunkSize;
}

void Storage::setChunkSize(size_t chunkSize)
{
    _chunkSize = chunkSize;
}

int Storage::size(uint64_t* s)
{
    uint64_t bytes;
    int r = sizeInBytes(&bytes);
    if (r < 0)
        return r;
    *s = bytes / _chunkSize;
    return 0;
}

int Storage::read(uint64_t index, uint64_t size, void* buf)
{
    int r = readBytes(index * _chunkSize, size * _chunkSize, buf);
    if (r < 0)
        return r;
    _chunksIn += size;
    return 0;
}

int Storage::write(uint64_t index, uint64_t size, const void* buf)
{
    int r = writeBytes(index * _chunkSize, size * _chunkSize, buf);
    if (r < 0)
        return r;
    _chunksOut += size;
    return 0;
}

int Storage::punchHole(uint64_t index, uint64_t size)
{
    int r = punchHoleBytes(index * _chunkSize, size * _chunkSize);
    if (r < 0)
        return r;
    _chunksPunchedHole += size;
    return 0;
}

int Storage::setSize(uint64_t size)
{
    return setSizeBytes(size * _chunkSize);
}

uint64_t Storage::chunksIn() const
{
    return _chunksIn;
}

uint64_t Storage::chunksOut() const
{
    return _chunksOut;
}

uint64_t Storage::chunksPunchedHole() const
{
    return _chunksPunchedHole;
}
