/*
 * Copyright (C) 2023, 2024, 2025
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
#include <cassert>

#include <unistd.h>

#include "index.hpp"
#include "inode.hpp"


Inode::Inode() :
    atime(),
    ctime(),
    mtime(),
    uid(0),
    gid(0),
    typeAndMode(0),
    nlink(0),
    rdev(0),
    size(0),
    slotTrees { InvalidIndex, InvalidIndex, InvalidIndex, InvalidIndex, InvalidIndex },
    xattrIndex(InvalidIndex)
{
}

Inode Inode::empty()
{
    Inode inode;
    Time t = Time::now();
    inode.atime = t;
    inode.ctime = t;
    inode.mtime = t;
    inode.uid = geteuid();
    inode.gid = getegid();
    inode.nlink = 1;
    return inode;
}

Inode Inode::directory(const Inode* parent, uint32_t mode)
{
    Inode inode = empty();
    if (parent && parent->typeAndMode & ModeSGID)
        inode.gid = parent->gid;
    inode.typeAndMode = TypeDIR | (~TypeMask & mode);
    if (parent && parent->typeAndMode & ModeSGID)
        inode.typeAndMode |= ModeSGID;
    inode.nlink = 2; // "." and ".."
    return inode;
}

Inode Inode::node(uint32_t typeAndMode, uint64_t rdev)
{
    Inode inode = empty();
    inode.typeAndMode = typeAndMode;
    inode.rdev = rdev;
    return inode;
}

Inode Inode::symlink(size_t targetLen, uint64_t blockIndex)
{
    Inode inode = empty();
    inode.typeAndMode = TypeLNK;
    inode.size = targetLen;
    inode.slotTrees[0] = blockIndex;
    return inode;
}
