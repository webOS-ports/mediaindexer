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
    std::string kindName;

    err = obj.putString("path", file.getFileName().c_str());
    // ErrorToException(err);

    err = obj.putString("title", file.getTitle().c_str());
    // ErrorToException(err);

    err = obj.putString("author", file.getAuthor().c_str());
    // ErrorToException(err);

    if (file.getType() == MediaType::AudioMedia) {
        err = obj.putString("_kind", "com.palm.media.audio.file:1");
        // ErrorToException(err);
    }
    else if (file.getType() == MediaType::VideoMedia) {
        err = obj.putString("_kind", "com.palm.media.video.file:1");
        // ErrorToException(err);
    }
    else if (file.getType() == MediaType::ImageMedia) {
        err = obj.putString("_kind", "com.palm.media.image.file:1");
    }
    else if (file.getType() == MediaType::MiscMedia) {
        err = obj.putString("_kind", "com.palm.media.file:1");
    }
}

} // mediascanner
