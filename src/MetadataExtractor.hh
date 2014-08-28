/*
 * Copyright (C) 2013 Canonical, Ltd.
 *
 * Authors:
 *    Jussi Pakkanen <jussi.pakkanen@canonical.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of version 3 of the GNU General Public License as published
 * by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef METADATAEXTRACTOR_H
#define METADATAEXTRACTOR_H

#include <string>
#include "ScannerCore.hh"

namespace mediascanner {

class MediaFile;
struct MetadataExtractorPrivate;

struct DetectedFile {
    DetectedFile(const std::string &path,
                 const std::string &etag,
                 const std::string content_type,
                 MediaType type)
        : path(path), etag(etag), content_type(content_type)
        , type(type) {}

    std::string path;
    std::string etag;
    std::string content_type;
    MediaType type;
};

class MetadataExtractor final {
public:
    MetadataExtractor(int seconds=25);
    ~MetadataExtractor();
    MetadataExtractor(const MetadataExtractor&) = delete;
    MetadataExtractor& operator=(MetadataExtractor &o) = delete;

    DetectedFile detect(const std::string &path);
    MediaFile extract(const DetectedFile &media);
    void extractForAudio(const MediaFile &mf, const DetectedFile &d);
    void extractForImage(const MediaFile &mf, const DetectedFile &d);

    std::string getAlbumPathFromImage(const std::string& path);
    std::string getAlbumNameFromPath(const std::string& path);
};

}

#endif
