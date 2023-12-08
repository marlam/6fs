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

#include <sodium.h>

#include "inode.hpp"
#include "dirent.hpp"
#include "block.hpp"

constexpr size_t EncOverhead = 1 + crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES;
constexpr size_t EncInodeSize  = sizeof(Inode)  + EncOverhead;
constexpr size_t EncDirentSize = sizeof(Dirent) + EncOverhead;
constexpr size_t EncBlockSize  = sizeof(Block)  + EncOverhead;

void enc(const unsigned char* key, const unsigned char* msg, size_t msgSize, unsigned char* out);
int dec(const unsigned char* key, const unsigned char* in, size_t inSize, unsigned char* msg, size_t msgSize);
