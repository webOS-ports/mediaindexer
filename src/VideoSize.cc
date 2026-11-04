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

#include "VideoSize.hh"

#include <cerrno>
#include <cstddef>
#include <limits>

#include <sys/types.h>
#include <unistd.h>

namespace videosize {

namespace {

constexpr uint32_t fourcc(const char (&s)[5])
{
    return (uint32_t) (unsigned char) s[0] << 24 | (uint32_t) (unsigned char) s[1] << 16 |
           (uint32_t) (unsigned char) s[2] << 8 | (uint32_t) (unsigned char) s[3];
}

uint32_t be32(const unsigned char *b)
{
    return (uint32_t) b[0] << 24 | (uint32_t) b[1] << 16 | (uint32_t) b[2] << 8 | (uint32_t) b[3];
}

// Exactly n bytes at off, or false
bool readAt(int fd, uint64_t off, unsigned char *buf, size_t n)
{
    size_t done = 0;
    while (done < n) {
        uint64_t at = off + done;
        if (at < off || at > (uint64_t) std::numeric_limits<off_t>::max())
            return false;
        ssize_t r = pread(fd, buf + done, n - done, (off_t) at);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return false;
        done += (size_t) r;
    }
    return true;
}

// A box ("atom" in QuickTime): its type, and where its contents start and
// it ends. The file is whatever is in /media/internal: every size is checked
// against the box it is in.
struct Box {
    uint32_t type;
    uint64_t body;
    uint64_t end;
};

// The box at `at`, which must end by `end`
bool boxAt(int fd, uint64_t at, uint64_t end, Box &box)
{
    if (at > end || end - at < 8)
        return false;
    unsigned char h[16];
    if (!readAt(fd, at, h, 8))
        return false;
    uint64_t size = be32(h);
    box.type = be32(h + 4);
    uint64_t header = 8;
    if (size == 1) {
        // A 64 bit size follows
        if (end - at < 16 || !readAt(fd, at + 8, h + 8, 8))
            return false;
        size = (uint64_t) be32(h + 8) << 32 | be32(h + 12);
        header = 16;
    } else if (size == 0) {
        // To the end of what it is in
        size = end - at;
    }
    if (size < header || size > end - at)
        return false;
    box.body = at + header;
    box.end = at + size;
    return true;
}

// The first box of the type in [from, end); at most `limit` boxes are looked at
bool find(int fd, uint64_t from, uint64_t end, uint32_t type, Box &out, unsigned limit = 1024)
{
    Box box;
    for (uint64_t at = from; limit > 0 && boxAt(fd, at, end, box); at = box.end, limit--) {
        if (box.type == type) {
            out = box;
            return true;
        }
    }
    return false;
}

struct Track {
    bool enabled;
    unsigned width;
    unsigned height;
};

// A trak that is a video: its header's size, turned as it is shown
bool videoTrack(int fd, const Box &trak, Track &track)
{
    Box mdia, hdlr;
    unsigned char handler[12];
    // A handler of "vide" (in QuickTime: subtype "vide" of an "mhlr"), at
    // the same place in both
    if (!find(fd, trak.body, trak.end, fourcc("mdia"), mdia, 64) ||
        !find(fd, mdia.body, mdia.end, fourcc("hdlr"), hdlr, 64) ||
        hdlr.end - hdlr.body < 12 || !readAt(fd, hdlr.body, handler, 12) || be32(handler + 8) != fourcc("vide"))
        return false;

    Box tkhd;
    if (!find(fd, trak.body, trak.end, fourcc("tkhd"), tkhd, 64))
        return false;

    // Version and flags; then the times and the track's ID (32 or 64 bit
    // times), 16 bytes of layer and volume, the 3x3 matrix, and the width
    // and height as 16.16 fixed point
    unsigned char t[96];
    if (tkhd.end - tkhd.body < 4 || !readAt(fd, tkhd.body, t, 4))
        return false;
    size_t matrix = t[0] == 1 ? 52 : t[0] == 0 ? 40 : 0;
    if (matrix == 0 || tkhd.end - tkhd.body < matrix + 44 || !readAt(fd, tkhd.body, t, matrix + 44))
        return false;

    track.enabled = (t[3] & 1) != 0;
    track.width = (unsigned) ((be32(t + matrix + 36) + 0x8000ull) >> 16);
    track.height = (unsigned) ((be32(t + matrix + 40) + 0x8000ull) >> 16);

    // a b u / c d v / x y w: a and d 0 is a quarter turn either way
    if (be32(t + matrix) == 0 && be32(t + matrix + 16) == 0) {
        unsigned tmp = track.width;
        track.width = track.height;
        track.height = tmp;
    }
    return track.width > 0 && track.height > 0;
}

} // namespace

bool fromIsoBmff(int fd, uint64_t fileSize, unsigned &width, unsigned &height)
{
    // The movie's header may come after the data (as a camera writes it):
    // the boxes before it are skipped, not read
    Box moov;
    if (!find(fd, 0, fileSize, fourcc("moov"), moov))
        return false;

    // The first enabled video track, else the first video track
    bool found = false;
    Track first = {}, track;
    Box box;
    unsigned limit = 1024;
    for (uint64_t at = moov.body; limit > 0 && boxAt(fd, at, moov.end, box); at = box.end, limit--) {
        if (box.type != fourcc("trak") || !videoTrack(fd, box, track))
            continue;
        if (!found || track.enabled) {
            first = track;
            found = true;
        }
        if (track.enabled)
            break;
    }
    if (!found)
        return false;
    width = first.width;
    height = first.height;
    return true;
}

}
