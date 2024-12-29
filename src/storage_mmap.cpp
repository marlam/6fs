/*
 * Copyright (C) 2024
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

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/mman.h>
#include <unistd.h>

#include "storage_mmap.hpp"


StorageMmap::StorageMmap(const std::string& name) :
    _pagesize(sysconf(_SC_PAGE_SIZE)),
    _name(name), _fd(0),
    _map(nullptr), _len(0), _size(0)
{
}

StorageMmap::~StorageMmap()
{
    close();
}

static size_t sizeToMapLength(size_t pagesize, size_t size)
{
    size_t l = size;
    if (l % pagesize != 0)
        l = (l / pagesize + 1) * pagesize;
    if (l == 0)
        l = pagesize;
    return l;
}

static int setFileSize(int fd, size_t size)
{
    if (::ftruncate(fd, size) != 0)
        return -errno;
    return 0;
}

int StorageMmap::open()
{
    int r = ::open(_name.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (r < 0)
        return -errno;
    _fd = r;

    struct stat statbuf;
    if (fstat(_fd, &statbuf) != 0) {
        ::close(_fd);
        _fd = 0;
        return -errno;
    }

    _size = statbuf.st_size;
    _len = sizeToMapLength(_pagesize, _size);
    r = setFileSize(_fd, _len);
    if (r != 0) {
        ::close(_fd);
        _fd = 0;
        _size = 0;
        _len = 0;
        return r;
    }

    void* p = mmap(nullptr, _len, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
    if (p == MAP_FAILED) {
        r = errno;
        ::close(_fd);
        _fd = 0;
        _len = 0;
        _size = 0;
        return -r;
    }
    _map = p;

    return 0;
}

int StorageMmap::close()
{
    if (_map) {
        ::munmap(_map, _len);
        _map = nullptr;
        _len = 0;
    }
    if (_fd != 0) {
        int r0 = setFileSize(_fd, _size);
        int r1 = ::close(_fd);
        _fd = 0;
        _size = 0;
        if (r0 != 0)
            return r0;
        if (r1 != 0)
            return -errno;
    }
    return 0;
}

int StorageMmap::stat(uint64_t* maxBytes, uint64_t* availableBytes)
{
    struct statvfs statfsbuf;
    if (statvfs(_name.c_str(), &statfsbuf) != 0)
        return -errno;
    *maxBytes = uint64_t(statfsbuf.f_blocks) * uint64_t(statfsbuf.f_frsize);
    *availableBytes = uint64_t(statfsbuf.f_bavail) * uint64_t(statfsbuf.f_frsize);
    return 0;
}

int StorageMmap::sizeInBytes(uint64_t* s)
{
    *s = _size;
    return 0;
}

int StorageMmap::readBytes(uint64_t index, uint64_t size, void* buf)
{
    if (index + size > _size)
        return -EIO;
    ::memcpy(buf, static_cast<unsigned char*>(_map) + index, size);
    return 0;
}

int StorageMmap::writeBytes(uint64_t index, uint64_t size, const void* buf)
{
    if (index + size > _size) {
        int r = setSizeBytes(index + size);
        if (r != 0)
            return r;
    }
    ::memcpy(static_cast<unsigned char*>(_map) + index, buf, size);
    return 0;
}

int StorageMmap::punchHoleBytes(uint64_t /* index */, uint64_t /* size */)
{
    return 0;
}

int StorageMmap::setSizeBytes(uint64_t size)
{
    size_t newLen = sizeToMapLength(_pagesize, size);
    if (newLen != _len) {
        int r = setFileSize(_fd, newLen);
        if (r != 0)
            return r;
        void* p = ::mremap(_map, _len, newLen, MREMAP_MAYMOVE);
        if (p == MAP_FAILED)
            return -errno;
        _map = p;
        _len = newLen;
    }
    _size = size;
    return 0;
}
