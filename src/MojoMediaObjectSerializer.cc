/*
 * Copyright (C) 2014 Simon Busch <morphis@gravedo.de>
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

#include "MojoMediaObjectSerializer.hh"
#include "MediaFile.hh"
#include "ScannerCore.hh"
#include "internal/utils.hh"

namespace mediascanner
{

void MojoMediaObjectSerializer::SerializeToDatabaseObject(const MediaFile& file, MojObject& obj)
{
    MojErr err;

    err = obj.putString("path", file.path().c_str());
    err = obj.putString("name", file.name().c_str());
    err = obj.putString("extension", file.extension().c_str());
    err = obj.putInt("modifiedTime", file.modifiedTime());
    err = obj.putInt("createdTime", file.createdTime());
    err = obj.putInt("size", file.size());
    err = obj.putString("searchKey", file.searchKey().c_str());

    if (file.type() == MediaType::AudioMedia) {
        err = obj.putString("_kind", "com.palm.media.audio.file:1");

        err = obj.putString("albumArtist", file.albumArtist().c_str());
        err = obj.putString("album", file.album().c_str());

        MojObject trackObj(MojObject::Type::TypeObject);
        trackObj.putInt("position", file.trackPosition());
        trackObj.putInt("total", file.trackTotal());
        err = obj.put("track", trackObj);

        MojObject discObj(MojObject::Type::TypeObject);
        trackObj.putInt("position", file.discPosition());
        trackObj.putInt("total", file.discTotal());
        err = obj.put("disc", discObj);

        err = obj.putString("title", file.title().c_str());
        err = obj.putString("genre", file.genre().c_str());
        err = obj.putString("artist", file.artist().c_str());

        err = obj.putInt("duration", file.duration());
        err = obj.putInt("bookmark", file.bookmark());
        err = obj.putBool("isRingtone", file.isRingtone());

        err = obj.putBool("serviced", file.serviced());
        err = obj.putBool("hasResizedThumbnails", file.hasResizedThumbnails());

        // FIXME sortKey
        // FIXME thumbnails
    }
    else if (file.type() == MediaType::VideoMedia) {
        err = obj.putString("_kind", "com.palm.media.video.file:1");

        err = obj.putString("albumId", file.albumId().c_str());
        err = obj.putString("albumPath", file.albumPath().c_str());
        err = obj.putInt("lastPlayTime", file.lastPlayTime());
        err = obj.putString("title", file.title().c_str());

        err = obj.putBool("capturedOnDevice", file.capturedOnDevice());
        err = obj.putString("description", file.description().c_str());
        err = obj.putInt("playbackPosition", file.playbackPosition());
        err = obj.putString("mediaType", file.mediaType().c_str());
        err = obj.putString("appCacheCompleted", file.appCacheCompleted().c_str());

        // FIXME thumbnails

        err = obj.putString("type", "local");
    }
    else if (file.type() == MediaType::ImageMedia) {
        err = obj.putString("_kind", "com.palm.media.image.file:1");
    }
    else if (file.type() == MediaType::MiscMedia) {
        err = obj.putString("_kind", "com.palm.media.file:1");
    }
}

} // mediascanner
