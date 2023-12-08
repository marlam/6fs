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

#include "storage_file.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/statvfs.h>


StorageFile::StorageFile(const std::string& name) : _name(name), _fd(0)
{
}

StorageFile::~StorageFile()
{
    if (_fd != 0)
        close();
}

int StorageFile::open()
{
    int r = ::open(_name.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (r < 0)
        return -errno;
    _fd = r;
    return 0;
}

int StorageFile::close()
{
    if (_fd != 0) {
        int r = ::close(_fd);
        if (r < 0)
            return -errno;
        _fd = 0;
    }
    return 0;
}

int StorageFile::stat(uint64_t* maxBytes, uint64_t* availableBytes)
{
    struct statvfs statfsbuf;
    if (statvfs(_name.c_str(), &statfsbuf) != 0)
        return -errno;
    *maxBytes = uint64_t(statfsbuf.f_blocks) * uint64_t(statfsbuf.f_frsize);
    *availableBytes = uint64_t(statfsbuf.f_bavail) * uint64_t(statfsbuf.f_frsize);
    return 0;
}

int StorageFile::sizeInBytes(uint64_t* s)
{
    struct stat statbuf;
    if (fstat(_fd, &statbuf) != 0)
        return -errno;
    *s = statbuf.st_size;
    return 0;
}

int StorageFile::readBytes(uint64_t index, uint64_t size, void* buf)
{
    while (size > 0) {
        ssize_t r = ::pread(_fd, buf, size, index);
        if (r < 0)
            return -errno;
        size -= r;
        index += r;
    }
    return 0;
}

int StorageFile::writeBytes(uint64_t index, uint64_t size, const void* buf)
{
    while (size > 0) {
        ssize_t r = ::pwrite(_fd, buf, size, index);
        if (r < 0)
            return -errno;
        size -= r;
        index += r;
    }
    return 0;
}

int StorageFile::punchHoleBytes(uint64_t index, uint64_t size)
{
    if (fallocate(_fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, index, size) != 0) {
        // not all filesystems support this, but that's ok, our structure is still valid
    }
    return 0;
}

int StorageFile::setSizeBytes(uint64_t size)
{
    if (ftruncate(_fd, size) != 0)
        return -errno;
    return 0;
}
