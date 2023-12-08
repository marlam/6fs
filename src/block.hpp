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

#include <cstddef>
#include <cstdint>


// A block can hold
// - either file data
// - or directory entry indices
// - or a symbolic link target name
class Block
{
public:
    static constexpr size_t size = 4096;

    Block();

    void initializeData();
    void initializeIndices();
    void initializeTarget();

    union {
        unsigned char data[size];                       // for file data
        uint64_t indices[size / sizeof(uint64_t)];      // for directory entries
        char target[size];                              // for symlinks
    };
};
