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

#include <time.h>

#include "time.hpp"


Time::Time() : seconds(0), nanoseconds(0)
{
}

Time Time::now()
{
    Time t;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    t.seconds = ts.tv_sec;
    t.nanoseconds = ts.tv_nsec;
    return t;
}

bool Time::isOlderThan(const Time& t) const
{
    return (seconds < t.seconds || (seconds == t.seconds && nanoseconds < t.nanoseconds));
}
