/*
 * Copyright (C) 2026 Herman van Hazendonk <github.com@herrie.org>
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

#ifndef VIDEOSIZE_HH
#define VIDEOSIZE_HH

#include <cstdint>

namespace videosize {

// Width and height of a video as shown (a phone's portrait recording, stored
// on its side with a 90 degree turn, comes out taller than wide), from the
// video track's header in an MP4 or QuickTime file (also 3GP, M4V, MOV).
// Read with pread() through fd; fileSize is the file's size. Other
// containers, and files that do not say, give false.
bool fromIsoBmff(int fd, uint64_t fileSize, unsigned &width, unsigned &height);

}

#endif
