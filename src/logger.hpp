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

#include <cstdio>

#include <mutex>


class Logger
{
public:
    typedef enum {
        Debug   = 0,
        Info    = 1,
        Warning = 2,
        Error   = 3
    } Level;

private:
    const char* _argv0;
    FILE* _f;
    Level _l;
    std::mutex _mutex;

public:
    Logger();
    ~Logger();

    void setArgv0(const char* argv0);
    void setLevel(Level l);
    void setOutput(const char* fileNameOrNullptr);

    void log(Level l, const char* fmt, ...) __attribute__ ((format (printf, 3, 4)));
};

extern Logger logger;
