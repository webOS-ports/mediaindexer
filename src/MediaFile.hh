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

#ifndef MEDIAFILE_HH
#define MEDIAFILE_HH

#include <string>
#include <list>

#include "ScannerCore.hh"

namespace mediascanner
{

class MediaFile final
{
public:
    MediaFile();

    std::string etag() const noexcept { return _etag; }
    MediaType type() const noexcept { return _type; }

    uint64_t size() const noexcept { return _size; }
    std::string path() const noexcept { return _path; }
    std::list<std::string> sortKey() const noexcept { return _sortKey; }
    uint64_t createdTime() const noexcept { return _createdTime; }
    uint64_t modifiedTime() const noexcept { return _modifiedTime; }
    std::string searchKey() const noexcept { return _searchKey; }
    std::string name() const noexcept { return _name; }
    std::string extension() const noexcept { return _extension; }

    std::string title() const noexcept { return _title; }
    unsigned int trackPosition() const noexcept { return _trackPosition; }
    unsigned int trackTotal() const noexcept { return _trackTotal; }
    unsigned int discPosition() const noexcept { return _discPosition; }
    unsigned int discTotal() const noexcept { return _discTotal; }
    std::string genre() const noexcept { return _genre; }
    std::string artist() const noexcept { return _artist; }
    std::string album() const noexcept { return _album; }
    std::string albumArtist() const noexcept { return _albumArtist; }
    std::list<std::string> thumbnails() const noexcept { return _thumbnails; }
    unsigned int duration() const noexcept { return _duration; }
    unsigned int bookmark() const noexcept { return _bookmark; }
    bool isRingtone() const noexcept { return _isRingtone; }
    bool serviced() const noexcept { return _serviced; }
    bool hasResizedThumbnails() const noexcept { return _hasResizedThumbnails; }

    bool capturedOnDevice() const noexcept { return _capturedOnDevice; }
    std::string description() const noexcept { return _description; }
    unsigned int playbackPosition() const noexcept { return _playbackPosition; }
    std::string mediaType() const noexcept { return _mediaType; }
    bool appCacheCompleted() const noexcept { return _appCacheCompleted; }
    unsigned int lastPlayTime() const noexcept { return _lastPlayTime; }
    std::string albumId() const noexcept { return _albumId; }
    std::string albumPath() const noexcept { return _albumPath; }

    unsigned int year() const noexcept { return _year; }

    void setEtag(const std::string& value) { _etag = value; }
    void setType(MediaType value) { _type = value; }
    void setSize(uint64_t value) { _size = value; }
    void setPath(const std::string& value) { _path = value; }
    void setSortKey(const std::list<std::string> value) { _sortKey = value; }
    void setCreatedTime(uint64_t value) { _createdTime = value; }
    void setModifiedTime(uint64_t value) { _modifiedTime = value; }
    void setSearchKey(const std::string& value) { _searchKey = value; }
    void setName(const std::string& value) { _name = value; }
    void setExtension(const std::string& value) { _extension = value; }

    void setTitle(const std::string& value) { _title = value; }
    void setTrackPosition(unsigned int value) { _trackPosition = value; }
    void setTrackTotal(unsigned int value) { _trackTotal = value; }
    void setDiscPosition(unsigned int value) { _discPosition = value; }
    void setDiscTotal(unsigned int value) { _discTotal = value; }
    void setGenre(const std::string& value) { _genre = value; }
    void setArtist(const std::string& value) { _artist = value; }
    void setAlbum(const std::string& value) { _album = value; }
    void setAlbumArtist(const std::string& value) { _albumArtist = value; }
    void setThumbnails(const std::list<std::string>& value) { _thumbnails = value; }
    void setDuration(unsigned int value) { _duration = value; }
    void setBookmark(unsigned int value) { _bookmark = value; }
    void setIsRingtone(bool value) { _isRingtone = value; }
    void setServiced(bool value) { _serviced = value; }
    void setHasResizedThumbnails(bool value) { _hasResizedThumbnails = value; }

    void setCapturedOnDevice(bool value) { _capturedOnDevice = value; }
    void setDescription(const std::string& value) { _description = value; }
    void setPlaybackPosition(unsigned int value) { _playbackPosition = value; }
    void setLastPlayTime(unsigned int value) { _lastPlayTime = value; }
    void setMediaType(const std::string& value) { _mediaType = value; }
    void setAppCacheCompleted(bool value) { _appCacheCompleted = value; }
    void setAlbumId(const std::string& value) { _albumId = value; }
    void setAlbumPath(const std::string& value) { _albumPath = value; }

    void setYear(unsigned int value) { _year = value; }

    void rebuildSearchKey();

private:
    std::string _path;
    std::string _etag;
    MediaType _type;
    uint64_t _size;
    std::list<std::string> _sortKey;
    uint64_t _createdTime;
    uint64_t _modifiedTime;
    std::string _searchKey;
    std::string _extension;
    std::string _name;

    std::string _title;
    unsigned int _trackPosition;
    unsigned int _trackTotal;
    unsigned int _discPosition;
    unsigned int _discTotal;
    std::string _genre;
    std::string _artist;
    std::string _albumArtist;
    std::string _album;
    std::list<std::string> _thumbnails;
    unsigned int _duration;
    unsigned int _bookmark;
    bool _isRingtone;
    bool _serviced;
    bool _hasResizedThumbnails;

    bool _capturedOnDevice;
    std::string _description;
    unsigned int _playbackPosition;
    std::string _mediaType;
    bool _appCacheCompleted;
    std::string _albumId;
    std::string _albumPath;
    unsigned int _lastPlayTime;

    unsigned int _year;
};

}

#endif

