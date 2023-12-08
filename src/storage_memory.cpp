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

#include "storage_memory.hpp"

#include <cstring>
#include <cerrno>

#include <sys/sysinfo.h>


StorageMemory::StorageMemory() :
    _fatalError(false)
{
}

StorageMemory::~StorageMemory()
{
}

int StorageMemory::open()
{
    return 0;
}

int StorageMemory::close()
{
    if (_fatalError)
        return -EIO;
    _storage.clear();
    return 0;
}

int StorageMemory::stat(uint64_t* maxBytes, uint64_t* availableBytes)
{
    struct sysinfo info;
    if (sysinfo(&info) < 0)
        return -errno;

    uint64_t totalRam = info.totalram;
    totalRam *= info.mem_unit;
    uint64_t freeRam = info.freeram;
    freeRam *= info.mem_unit;

    *maxBytes = totalRam;
    *availableBytes = freeRam;
    return 0;
}

int StorageMemory::sizeInBytes(uint64_t* s)
{
    if (_fatalError)
        return -EIO;
    *s = _storage.size();
    return 0;
}

int StorageMemory::readBytes(uint64_t index, uint64_t size, void* buf)
{
    if (_fatalError)
        return -EIO;
    if (index + size > _storage.size())
        return -EIO;
    ::memcpy(buf, _storage.data() + index, size);
    return 0;
}

int StorageMemory::writeBytes(uint64_t index, uint64_t size, const void* buf)
{
    if (_fatalError)
        return -EIO;
    if (index + size > _storage.size()) {
        size_t newSize = index + size;
        try {
            _storage.resize(newSize);
        }
        catch (...) {
            _fatalError = true;
        }
        if (_fatalError)
            return -ENOMEM;
    }
    ::memcpy(_storage.data() + index, buf, size);
    return 0;
}

int StorageMemory::punchHoleBytes(uint64_t /* index */, uint64_t /* size */)
{
    if (_fatalError)
        return -EIO;
    return 0;
}

int StorageMemory::setSizeBytes(uint64_t size)
{
    if (_fatalError)
        return -EIO;
    try {
        _storage.resize(size, 0);
    }
    catch (...) {
        _fatalError = true;
    }
    if (_fatalError)
        return -ENOMEM;
    return 0;
}
