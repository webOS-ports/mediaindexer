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
 * Unit tests for FolderAlbum: which album a picture or video is put in,
 * which entries have one, and when an album is empty. No bus or db8 needed.
 */

#include "FolderAlbum.hh"

#include <cstdio>
#include <string>

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n",           \
                         __FILE__, __LINE__, #cond);                    \
            failures++;                                                 \
        }                                                               \
    } while (0)

using namespace folderalbum;
using namespace mediascanner;

static const std::string Dcim = "/media/internal/DCIM";

int main()
{
    // A file outside the camera's folder: the folder it is in
    CHECK(pathFor("/media/internal/movie.mp4", Dcim) == "/media/internal");
    CHECK(pathFor("/media/internal/Videos/Holiday/clip.mp4", Dcim) == "/media/internal/Videos/Holiday");
    CHECK(nameFor("/media/internal") == "internal");
    CHECK(nameFor("/media/internal/Videos/Holiday") == "Holiday");

    // Below the camera's folder, at any depth and whatever the case: the
    // camera's album
    CHECK(pathFor("/media/internal/DCIM/100PALM/CIMG0001.mp4", Dcim) == "camera://");
    CHECK(pathFor("/media/internal/DCIM/clip.mp4", Dcim) == "camera://");
    CHECK(pathFor("/media/internal/dcim/Camera/VID_0001.mp4", Dcim) == "camera://");
    CHECK(nameFor("camera://") == "Camera");
    CHECK(std::string(CameraPath) == "camera://");

    // Only that folder: not one whose name starts the same, nor the files
    // next to it
    CHECK(pathFor("/media/internal/DCIMfoo/clip.mp4", Dcim) == "/media/internal/DCIMfoo");
    CHECK(pathFor("/media/internal/DCIM.mp4", Dcim) == "/media/internal");

    // No camera folder set: no camera album
    CHECK(pathFor("/media/internal/DCIM/clip.mp4", "") == "/media/internal/DCIM");

    // Another camera folder
    CHECK(pathFor("/media/internal/Camera/a.jpg", "/media/internal/Camera") == "camera://");
    CHECK(pathFor("/media/internal/DCIM/a.jpg", "/media/internal/Camera") == "/media/internal/DCIM");

    // Pictures and videos have folder albums, music and other files not
    CHECK(hasFolderAlbum(ImageMedia));
    CHECK(hasFolderAlbum(VideoMedia));
    CHECK(!hasFolderAlbum(AudioMedia));
    CHECK(!hasFolderAlbum(MiscMedia));
    CHECK(!hasFolderAlbum(UnknownMedia));

    // The entries whose removal or change counts their album again
    CHECK(inFolderAlbum("com.palm.media.image.file:1"));
    CHECK(inFolderAlbum("com.palm.media.video.file:1"));
    CHECK(!inFolderAlbum("com.palm.media.audio.file:1"));
    CHECK(!inFolderAlbum("com.palm.media.misc.file:1"));
    CHECK(!inFolderAlbum("com.palm.media.image.album:1"));
    CHECK(!inFolderAlbum("com.palm.media.video.file"));
    CHECK(!inFolderAlbum(""));

    // An album goes only with neither pictures nor videos in it
    CHECK(isEmpty(0, 0));
    CHECK(!isEmpty(1, 0));
    CHECK(!isEmpty(0, 1));
    CHECK(!isEmpty(3, 2));
    CHECK(!isEmpty(0, 600));

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("FolderAlbumTest: all passed\n");
    return 0;
}
