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

/*
 * Unit tests for VideoSize: a video's width and height from the boxes of an
 * MP4 or QuickTime file, built here byte by byte.
 */

#include "VideoSize.hh"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <unistd.h>

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n",           \
                         __FILE__, __LINE__, #cond);                    \
            failures++;                                                 \
        }                                                               \
    } while (0)

typedef std::vector<unsigned char> Bytes;

static void be32(Bytes &b, uint32_t v)
{
    b.push_back((unsigned char) (v >> 24));
    b.push_back((unsigned char) (v >> 16));
    b.push_back((unsigned char) (v >> 8));
    b.push_back((unsigned char) v);
}

static void fourcc(Bytes &b, const char *type)
{
    b.insert(b.end(), type, type + 4);
}

static Bytes box(const char *type, const Bytes &body)
{
    Bytes b;
    be32(b, (uint32_t) (8 + body.size()));
    fourcc(b, type);
    b.insert(b.end(), body.begin(), body.end());
    return b;
}

static Bytes cat(std::initializer_list<Bytes> parts)
{
    Bytes b;
    for (const Bytes &p : parts)
        b.insert(b.end(), p.begin(), p.end());
    return b;
}

static const uint32_t ONE = 0x00010000;
static const uint32_t MINUS_ONE = 0xFFFF0000;

struct Matrix {
    uint32_t a, b, c, d;
};
static const Matrix identity = {ONE, 0, 0, ONE};
static const Matrix turned90 = {0, ONE, MINUS_ONE, 0};
static const Matrix turned180 = {MINUS_ONE, 0, 0, MINUS_ONE};

// width and height in 16.16 fixed point
static Bytes tkhd(uint32_t width, uint32_t height, bool enabled = true, const Matrix &m = identity,
                  int version = 0)
{
    Bytes b;
    be32(b, (uint32_t) version << 24 | (enabled ? 1 : 0));
    int times = version == 1 ? 32 : 20;
    b.insert(b.end(), (size_t) times + 16, 0);
    be32(b, m.a);
    be32(b, m.b);
    be32(b, 0);
    be32(b, m.c);
    be32(b, m.d);
    be32(b, 0);
    be32(b, 0);
    be32(b, 0);
    be32(b, 0x40000000);
    be32(b, width);
    be32(b, height);
    return box("tkhd", b);
}

static Bytes hdlr(const char *handler)
{
    Bytes b;
    be32(b, 0);
    fourcc(b, "mhlr");
    fourcc(b, handler);
    b.insert(b.end(), 12, 0);
    return box("hdlr", b);
}

static Bytes trak(const Bytes &header, const char *handler)
{
    return box("trak", cat({header, box("mdia", cat({box("mdhd", Bytes(24, 0)), hdlr(handler)}))}));
}

static Bytes ftyp()
{
    Bytes b;
    fourcc(b, "isom");
    be32(b, 0x200);
    fourcc(b, "isom");
    fourcc(b, "mp41");
    return box("ftyp", b);
}

static Bytes mdat()
{
    return box("mdat", Bytes(1000, 0xAB));
}

static Bytes moov(std::initializer_list<Bytes> traks)
{
    return box("moov", cat({box("mvhd", Bytes(100, 0)), cat(traks)}));
}

static bool probe(const Bytes &file, unsigned &width, unsigned &height, uint64_t claimedSize = 0)
{
    FILE *f = std::tmpfile();
    if (!f)
        return false;
    if (!file.empty())
        std::fwrite(file.data(), 1, file.size(), f);
    std::fflush(f);
    width = height = 12345;
    bool ok = videosize::fromIsoBmff(fileno(f), claimedSize ? claimedSize : file.size(), width, height);
    std::fclose(f);
    return ok;
}

static bool is(const Bytes &file, unsigned w, unsigned h)
{
    unsigned width, height;
    if (!probe(file, width, height)) {
        std::fprintf(stderr, "  no size, expected %ux%u\n", w, h);
        return false;
    }
    if (width != w || height != h)
        std::fprintf(stderr, "  %ux%u, expected %ux%u\n", width, height, w, h);
    return width == w && height == h;
}

static bool none(const Bytes &file, uint64_t claimedSize = 0)
{
    unsigned width, height;
    return !probe(file, width, height, claimedSize);
}

int main()
{
    const uint32_t W = 1920 << 16, H = 1080 << 16;

    // Faststart (header first) and as a camera writes it (header last)
    CHECK(is(cat({ftyp(), moov({trak(tkhd(W, H), "vide")}), mdat()}), 1920, 1080));
    CHECK(is(cat({ftyp(), mdat(), moov({trak(tkhd(W, H), "vide")})}), 1920, 1080));

    // A phone held upright: stored landscape, turned a quarter
    CHECK(is(cat({ftyp(), mdat(), moov({trak(tkhd(W, H, true, turned90), "vide")})}), 1080, 1920));
    // Upside down is still landscape
    CHECK(is(cat({ftyp(), mdat(), moov({trak(tkhd(W, H, true, turned180), "vide")})}), 1920, 1080));

    // 64 bit times
    CHECK(is(cat({ftyp(), moov({trak(tkhd(W, H, true, identity, 1), "vide")})}), 1920, 1080));

    // The sound track comes first, a subtitle track has a size too
    CHECK(is(cat({ftyp(), moov({trak(tkhd(0, 0), "soun"), trak(tkhd(640 << 16, 80 << 16), "sbtl"),
                                trak(tkhd(W, H), "vide")})}),
             1920, 1080));

    // The enabled video track, not the first one
    CHECK(is(cat({ftyp(), moov({trak(tkhd(320 << 16, 240 << 16, false), "vide"), trak(tkhd(W, H), "vide")})}),
             1920, 1080));
    // ... the first of two enabled ones
    CHECK(is(cat({ftyp(), moov({trak(tkhd(W, H), "vide"), trak(tkhd(320 << 16, 240 << 16), "vide")})}),
             1920, 1080));
    // ... and if none is enabled, the first
    CHECK(is(cat({ftyp(), moov({trak(tkhd(320 << 16, 240 << 16, false), "vide"),
                                trak(tkhd(W, H, false), "vide")})}),
             320, 240));

    // Non-square pixels: the shown width is a fraction, rounded
    CHECK(is(cat({ftyp(), moov({trak(tkhd((853 << 16) + 0x5555, 480 << 16), "vide")})}), 853, 480));
    CHECK(is(cat({ftyp(), moov({trak(tkhd((853 << 16) + 0x8000, 480 << 16), "vide")})}), 854, 480));

    // A 64 bit mdat size before the header
    {
        Bytes big;
        be32(big, 1);
        fourcc(big, "mdat");
        be32(big, 0);
        be32(big, 16 + 100);
        big.insert(big.end(), 100, 0);
        CHECK(is(cat({ftyp(), big, moov({trak(tkhd(W, H), "vide")})}), 1920, 1080));
    }

    // A last box of size 0 runs to the end of the file
    {
        Bytes last = moov({trak(tkhd(W, H), "vide")});
        last[0] = last[1] = last[2] = last[3] = 0;
        CHECK(is(cat({ftyp(), last}), 1920, 1080));
    }

    // No video, no size
    CHECK(none(cat({ftyp(), moov({trak(tkhd(0, 0), "soun")})})));
    CHECK(none(cat({ftyp(), moov({trak(tkhd(0, 0), "vide")})})));
    CHECK(none(cat({ftyp(), mdat()})));
    CHECK(none(Bytes()));

    // Other containers: Matroska, AVI
    CHECK(none(Bytes{0x1A, 0x45, 0xDF, 0xA3, 0x9F, 0x42, 0x86, 0x81, 0x01, 0x42, 0xF7, 0x81}));
    {
        Bytes avi;
        fourcc(avi, "RIFF");
        be32(avi, 0x10000000);
        fourcc(avi, "AVI ");
        avi.insert(avi.end(), 100, 0);
        CHECK(none(avi));
    }

    // Damaged files: cut short, boxes bigger than what they are in, sizes
    // below their own header, a track header cut short, an unknown version
    {
        Bytes good = cat({ftyp(), mdat(), moov({trak(tkhd(W, H), "vide")})});
        Bytes cut(good.begin(), good.end() - 30);
        CHECK(none(cut));
        // The file is shorter than its size says
        CHECK(none(cut, good.size()));

        Bytes tiny = cat({ftyp(), mdat(), moov({trak(tkhd(W, H), "vide")})});
        size_t m = ftyp().size();
        tiny[m] = tiny[m + 1] = tiny[m + 2] = 0;
        tiny[m + 3] = 4;
        CHECK(none(tiny));

        Bytes shortHeader = box("tkhd", Bytes(40, 0));
        CHECK(none(cat({ftyp(), moov({trak(shortHeader, "vide")})})));
        // ... and not read on into the box after it
        CHECK(none(cat({ftyp(), moov({trak(cat({shortHeader, box("free", Bytes(100, 0x11))}), "vide")})})));

        Bytes v2 = tkhd(W, H);
        v2[8] = 2;
        CHECK(none(cat({ftyp(), moov({trak(v2, "vide")})})));

        // A trak claiming more than its moov
        Bytes inner = trak(tkhd(W, H), "vide");
        inner[0] = 0x7F;
        CHECK(none(cat({ftyp(), moov({inner})})));
    }

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("VideoSizeTest: all passed\n");
    return 0;
}
