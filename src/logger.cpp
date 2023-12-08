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

#include <unistd.h>
#include <syslog.h>

#include <cstdarg>
#include <cstring>
#include <cerrno>
#include <ctime>

#include "logger.hpp"


Logger::Logger() : _argv0(nullptr), _f(nullptr), _l(Warning)
{
}

Logger::~Logger()
{
    if (_f)
        fclose(_f);
    else
        closelog();
}

void Logger::setArgv0(const char* argv0)
{
    _argv0 = argv0;
}

void Logger::setLevel(Level l)
{
    _l = l;
}

void Logger::setOutput(const char* fileNameOrNullptr)
{
    bool openFailure = false;
    if (fileNameOrNullptr) {
        _f = fopen(fileNameOrNullptr, "a");
        if (_f)
            setlinebuf(_f);
        else
            openFailure = true;
    }
    if (!_f) {
        _f = nullptr;
        openlog(_argv0, LOG_PID, LOG_USER);
        if (openFailure) {
            log(Error, "cannot open log file %s (%s); using syslog instead", fileNameOrNullptr, strerror(errno));
        }
    }
}

void Logger::log(Level l, const char* fmt, ...)
{
    if (l < _l)
        return;
    std::unique_lock<std::mutex> lock(_mutex);
    if (_f) {
        long long pid = getpid();
        time_t t = time(nullptr);
        struct tm* tm = localtime(&t);
        char timeStr[128];
        strftime(timeStr, sizeof(timeStr), "%F %T", tm);
        fprintf(_f, "%s %s[%lld] %s: ", timeStr, _argv0, pid,
                l == Error ? "error" : l == Warning ? "warning" : l == Info ? "info" : "debug");
        va_list args;
        va_start(args, fmt);
        vfprintf(_f, fmt, args);
        va_end(args);
        fputc('\n', _f);
    } else {
        int priority = (l == Error ? LOG_ERR : l == Warning ? LOG_WARNING : l == Info ? LOG_INFO : LOG_DEBUG);
        va_list args;
        va_start(args, fmt);
        vsyslog(priority, fmt, args);
        va_end(args);
    }
}

Logger logger;
