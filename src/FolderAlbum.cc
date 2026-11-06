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

#include "FolderAlbum.hh"

namespace folderalbum {

const char* const CameraPath = "camera://";
const char* const CameraName = "Camera";

static std::string asciiLower(std::string s)
{
    for (char &c : s) {
        if (c >= 'A' && c <= 'Z')
            c = (char) (c - 'A' + 'a');
    }
    return s;
}

static bool startsWith(const std::string& s, const std::string& prefix)
{
    return s.compare(0, prefix.size(), prefix) == 0;
}

std::string pathFor(const std::string& filePath, const std::string& cameraDirectory)
{
    if (!cameraDirectory.empty() && startsWith(asciiLower(filePath), asciiLower(cameraDirectory) + "/"))
        return CameraPath;

    return filePath.substr(0, filePath.find_last_of('/'));
}

std::string nameFor(const std::string& albumPath)
{
    if (albumPath == CameraPath)
        return CameraName;
    return albumPath.substr(albumPath.find_last_of('/') + 1);
}

bool hasFolderAlbum(mediascanner::MediaType type)
{
    return type == mediascanner::ImageMedia || type == mediascanner::VideoMedia;
}

bool inFolderAlbum(const std::string& kind)
{
    return startsWith(kind, "com.palm.media.image.file:") || startsWith(kind, "com.palm.media.video.file:");
}

bool isEmpty(int64_t images, int64_t videos)
{
    return images <= 0 && videos <= 0;
}

} // namespace folderalbum
