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

#include<string>

namespace mediascanner {

std::string sqlQuote(const std::string &input);
std::string filenameToTitle(const std::string &filename);
std::string getUri(const std::string &filename);
// printf-style; the compiler checks the arguments against the format
std::string string_format(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

}

#endif
