/*
 * Copyright (C) 2023, 2024
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

#include <atomic>

#include <cstdint>
#include <cstddef>


/* The basic storage for any data (maps or chunks).
 *
 * The StorageFile and StorageMemory classes implement the byte-oriented interfaces;
 * the chunk stuff including encryption is handled by this class.
 *
 * Derived classes must be able to handle concurrent calls to readBytes() and writeBytes(),
 * but other calls don't need to be thread safe.
 */
class Storage
{
public:
    typedef enum {
        TypeMmap,
        TypeFile,
        TypeMem
    } Type;

private:
    size_t _chunkSize;
    std::atomic<uint64_t> _chunksIn;
    std::atomic<uint64_t> _chunksOut;
    std::atomic<uint64_t> _chunksPunchedHole;

public:
    Storage();
    virtual ~Storage();

    // Get/set chunk size, regardless of whether the chunks will be encrypted or not.
    size_t chunkSize() const;
    void setChunkSize(size_t chunkSize);

    // Open / close
    virtual int open() = 0;
    virtual int close() = 0;
    // For statfs()
    virtual int stat(uint64_t* maxBytes, uint64_t* availableBytes) = 0;

    // Byte-oriented input / output (this is what subclasses have to implement)
    virtual int sizeInBytes(uint64_t* s) = 0;
    virtual int readBytes(uint64_t index, uint64_t size, void* buf) = 0;
    virtual int writeBytes(uint64_t index, uint64_t size, const void* buf) = 0;
    virtual int punchHoleBytes(uint64_t index, uint64_t size) = 0;
    virtual int setSizeBytes(uint64_t size) = 0;

    // Chunk-oriented input / output (implemented by this class)
    int size(uint64_t* s);
    int read(uint64_t index, uint64_t size, void* buf);
    int write(uint64_t index, uint64_t size, const void* buf);
    int punchHole(uint64_t index, uint64_t size);
    int setSize(uint64_t size);

    // Statistics
    uint64_t chunksIn() const;
    uint64_t chunksOut() const;
    uint64_t chunksPunchedHole() const;
};
