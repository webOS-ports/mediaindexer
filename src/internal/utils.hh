/*
 * Copyright (C) 2013 Canonical, Ltd.
 *
 * Authors:
 *    Jussi Pakkanen <jussi.pakkanen@canonical.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SCAN_UTILS_H
#define SCAN_UTILS_H

#include<functional>
#include<string>

namespace mediascanner {

std::string sqlQuote(const std::string &input);
std::string filenameToTitle(const std::string &filename);
std::string getUri(const std::string &filename);
// printf-style; the compiler checks the arguments against the format
std::string string_format(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Creates the directory if needed and opens it, without following a link,
// and only if this process owns it and nobody else can write to it.
// Returns the descriptor, or -1 with errno set.
int openOwnDirectory(const char *path);

// Writes fileName in such a directory through write(fd), as root may below
// a folder users can write to: neither a link at the name nor someone
// else's directory decides what gets overwritten. False, with errno set,
// when the directory is refused or the file could not be written.
bool writeFileInOwnDirectory(const char *directory, const std::string &fileName,
                             const std::function<bool(int fd)> &write);

}

#endif
