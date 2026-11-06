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
 * Unit tests for the albums MetadataExtractor gives the files it reads:
 * pictures and videos the album of their folder, or the camera's below the
 * camera folder; music none of those. The files are made in a new folder
 * in the current directory (or $TEST_TMPDIR), and removed again.
 */

#include "MediaFile.hh"
#include "MetadataExtractor.hh"

#include <cstdio>
#include <cstdlib>
#include <string>

#include <fcntl.h>
#include <sys/stat.h>
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

using namespace mediascanner;

static bool writeFile(const std::string &path)
{
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0)
        return false;
    // Not a real picture or video: the album comes from the path alone
    bool ok = write(fd, "not media", 9) == 9;
    return close(fd) == 0 && ok;
}

static MediaFile extract(MetadataExtractor &e, const std::string &path, const char *mime, MediaType type)
{
    return e.extract(DetectedFile(path, "etag", mime, type));
}

int main()
{
    const char *base = std::getenv("TEST_TMPDIR");
    std::string tmpl = std::string(base && *base ? base : ".") + "/folderalbum-XXXXXX";
    if (!mkdtemp(&tmpl[0])) {
        std::perror("mkdtemp");
        return 1;
    }
    const std::string root = tmpl;
    const std::string dcim = root + "/DCIM";
    const std::string sub = dcim + "/100PALM";
    const std::string videos = root + "/Videos";
    const std::string files[] = {
        root + "/movie.mp4", videos + "/clip.mp4", sub + "/VID_0001.mp4",
        sub + "/IMG_0001.jpg", videos + "/still.jpg", root + "/song.mp3"
    };
    if (mkdir(dcim.c_str(), 0700) != 0 || mkdir(sub.c_str(), 0700) != 0 || mkdir(videos.c_str(), 0700) != 0) {
        std::perror("mkdir");
        return 1;
    }
    for (const std::string &f : files) {
        if (!writeFile(f)) {
            std::perror(f.c_str());
            return 1;
        }
    }

    MetadataExtractor e;
    e.setCameraDirectory(dcim);

    // A video copied to the top of the storage: the album of that folder
    MediaFile v = extract(e, files[0], "video/mp4", VideoMedia);
    CHECK(v.albumPath() == root);
    CHECK(v.album() == root.substr(root.find_last_of('/') + 1));
    CHECK(v.mediaType() == "video");

    // In a folder of its own
    v = extract(e, files[1], "video/mp4", VideoMedia);
    CHECK(v.albumPath() == videos);
    CHECK(v.album() == "Videos");

    // Recorded by the camera: the camera's album, as its pictures are
    v = extract(e, files[2], "video/mp4", VideoMedia);
    CHECK(v.albumPath() == "camera://");
    CHECK(v.album() == "Camera");

    // Pictures as before
    MediaFile i = extract(e, files[3], "image/jpeg", ImageMedia);
    CHECK(i.albumPath() == "camera://");
    CHECK(i.album() == "Camera");
    CHECK(i.mediaType() == "image");

    // A picture and a video in one folder share its album
    i = extract(e, files[4], "image/jpeg", ImageMedia);
    CHECK(i.albumPath() == videos);
    CHECK(i.album() == "Videos");

    // Music has no folder album
    MediaFile a = extract(e, files[5], "audio/mpeg", AudioMedia);
    CHECK(a.albumPath().empty());

    for (const std::string &f : files)
        unlink(f.c_str());
    rmdir(sub.c_str());
    rmdir(dcim.c_str());
    rmdir(videos.c_str());
    rmdir(root.c_str());

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("MetadataExtractorAlbumTest: all passed\n");
    return 0;
}
