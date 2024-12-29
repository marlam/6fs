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

#pragma once

#include <string>

#include "storage.hpp"

/* Storage in a file via mmap */
class StorageMmap : public Storage
{
private:
    const size_t _pagesize;
    std::string _name;
    int _fd;
    void* _map;
    size_t _len;
    size_t _size;

public:
    StorageMmap(const std::string& name);
    ~StorageMmap();

    virtual int open() override;
    virtual int close() override;
    virtual int stat(uint64_t* maxBytes, uint64_t* availableBytes) override;
    virtual int sizeInBytes(uint64_t* s) override;
    virtual int readBytes(uint64_t index, uint64_t size, void* buf) override;
    virtual int writeBytes(uint64_t index, uint64_t size, const void* buf) override;
    virtual int punchHoleBytes(uint64_t index, uint64_t size) override;
    virtual int setSizeBytes(uint64_t size) override;
};
