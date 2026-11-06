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

#ifndef FOLDERALBUM_HH
#define FOLDERALBUM_HH

#include <cstdint>
#include <string>

#include "ScannerCore.hh"

/*
 * The albums of pictures and videos (com.palm.media.image.album:1): one
 * per folder, and one for the camera. Kept apart from the db8 code so it
 * can be tested on its own.
 */
namespace folderalbum {

/// The camera's album, whatever folders below the camera's are in it
extern const char* const CameraPath;
extern const char* const CameraName;

/// The album of the picture or video at filePath: the camera's for a file
/// below cameraDirectory (whatever the case of its letters, and only that
/// folder: not one whose name only starts the same), the folder the file
/// is in otherwise. An empty cameraDirectory means no camera album.
std::string pathFor(const std::string& filePath, const std::string& cameraDirectory);

/// The name shown for the album at albumPath: the folder's own name
std::string nameFor(const std::string& albumPath);

/// Pictures and videos are put in the album of their folder; music has
/// albums, genres and artists of its own
bool hasFolderAlbum(mediascanner::MediaType type);

/// Whether an entry of this kind ("com.palm.media.video.file:1") is in a
/// folder album, which is to be counted again once the entry goes or changes
bool inFolderAlbum(const std::string& kind);

/// An album with neither pictures nor videos in it goes
bool isEmpty(int64_t images, int64_t videos);

} // namespace folderalbum

#endif // FOLDERALBUM_HH
