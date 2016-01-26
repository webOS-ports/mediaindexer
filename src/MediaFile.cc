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

#include "MediaFile.hh"
#include "internal/utils.hh"

using namespace std;

namespace mediascanner
{

MediaFile::MediaFile() :
    _path(""),
    _etag(""),
    _type(UnknownMedia),
    _size(0),
    _createdTime(0),
    _modifiedTime(0),
    _searchKey(""),
    _trackPosition(0),
    _trackTotal(0),
    _discPosition(1),
    _discTotal(0),
    _genre("Unknown Genre"),
    _artist("Unknown Artist"),
    _albumArtist("Unknown Artist"),
    _title("Unknown Title"),
    _duration(0),
    _bookmark(0),
    _isRingtone(false),
    _serviced(false),
    _hasResizedThumbnails(false),
    _capturedOnDevice(false),
    _description(""),
    _playbackPosition(0),
    _mediaType("video"),
    _appCacheCompleted(true),
    _albumId(""),
    _albumPath(""),
    _albumName(""),
    _lastPlayTime(0),
    _year(0)
{
}

void MediaFile::rebuildSearchKey()
{
    if (_type == AudioMedia) {
        _searchKey = _artist;
        _searchKey += "\t\t";
        _searchKey += _album;
        _searchKey += "\t\t";
        _searchKey += _title;
    }
    else if (_type == VideoMedia) {
        _searchKey = _title;
    }
    else {
        _searchKey = _name;
    }
}

}
