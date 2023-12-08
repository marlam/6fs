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

#include "emergency.hpp"
#include "logger.hpp"


EmergencyType emergencyType = EmergencyNone;

const char* strerror(EmergencyType t)
{
    const char* r = nullptr;
    switch (t) {
    case EmergencyNone:
        r = "none";
        break;
    case EmergencyBug:
        r = "bug";
        break;
    case EmergencySystemFailure:
        r = "system failure";
        break;
    }
    return r;
}

void emergency(EmergencyType t)
{
    logger.log(Logger::Error, "Emergency (%s): file system is corrupt, enforcing read-only access", strerror(t));
    if (emergencyType == EmergencyNone)
        emergencyType = t;
}
