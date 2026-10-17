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

#include "MediaFile.hh"
#include "internal/utils.hh"
#include "MetadataExtractor.hh"

#include <glib-object.h>
#include <gio/gio.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <stdexcept>
#include <memory>
#include <iostream>
#include <iomanip>
#include <algorithm>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>

#include <taglib/taglib.h>
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/tfilestream.h>

using namespace std;

namespace mediascanner
{

const char* const MetadataExtractor::CameraAlbumPath = "camera://";
const char* const MetadataExtractor::CameraAlbumName = "Camera";

MetadataExtractor::MetadataExtractor(int seconds)
{
}

MetadataExtractor::~MetadataExtractor()
{
}

DetectedFile MetadataExtractor::detect(const std::string &path)
{
    std::unique_ptr<GFile, void(*)(void *)> file(
        g_file_new_for_path(path.c_str()), g_object_unref);
    if (!file) {
        throw runtime_error("Could not create file object");
    }

    GError *error = nullptr;
    std::unique_ptr<GFileInfo, void(*)(void *)> info(
        g_file_query_info(
            file.get(),
            G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE ","
            G_FILE_ATTRIBUTE_ETAG_VALUE,
            G_FILE_QUERY_INFO_NONE, /* cancellable */ NULL, &error),
        g_object_unref);
    if (!info) {
        string errortxt(error->message);
        g_error_free(error);

        string msg("Query of file info for ");
        msg += path;
        msg += " failed: ";
        msg += errortxt;
        throw runtime_error(msg);
    }

    string etag(g_file_info_get_etag(info.get()));
    string content_type(g_file_info_get_attribute_string(
        info.get(), G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE));
    if (content_type.empty()) {
        throw runtime_error("Could not determine content type.");
    }

    MediaType type;
    if (content_type.find("audio/") == 0) {
        type = AudioMedia;
    } else if (content_type.find("video/") == 0) {
        type = VideoMedia;
    } else if (content_type.find("image/") == 0) {
        type = ImageMedia;
    } else {
        type = MiscMedia;
    }

    return DetectedFile(path, etag, content_type, type);
}

// After the last dot of the file name; none for ".hidden" or "name."
static std::string get_filename_extension(const std::string &fileName)
{
    size_t dot = fileName.find_last_of('.');
    if (dot == std::string::npos || dot == 0)
        return "";
    return fileName.substr(dot + 1);
}

// "5" or "5/12": a missing or unreadable part leaves the value as it is
static void parsePosition(const TagLib::StringList &values, unsigned int &position, unsigned int &total)
{
    if (values.isEmpty())
        return;
    TagLib::StringList parts = values.front().split("/");
    if (parts.size() >= 1 && parts[0].toInt() > 0)
        position = parts[0].toInt();
    if (parts.size() == 2 && parts[1].toInt() > 0)
        total = parts[1].toInt();
}

void MetadataExtractor::extractForAudio(MediaFile &mf, int fd)
{
    // TagLib closes the descriptor it reads from; ours stays with extract()
    int streamFd = dup(fd);
    if (streamFd < 0)
        return;
    TagLib::FileStream stream(streamFd, true);
    if (!stream.isOpen()) {
        close(streamFd);
        return;
    }
    TagLib::FileRef file(&stream);

    if (!file.isNull() && file.tag()) {

        TagLib::PropertyMap tags = file.file()->properties();

        // toCString(true): UTF-8, not Latin-1
        if (tags.contains("ALBUM"))
            mf.setAlbum(file.tag()->album().toCString(true));
        if (tags.contains("ARTIST"))
            mf.setArtist(file.tag()->artist().toCString(true));
        if (tags.contains("TITLE"))
            mf.setTitle(file.tag()->title().toCString(true));
        if (tags.contains("GENRE"))
            mf.setGenre(file.tag()->genre().toCString(true));
        mf.setTrackPosition(file.tag()->track());
        mf.setYear(file.tag()->year());

        if (tags.contains("ALBUMARTIST")) {
            TagLib::StringList values = tags["ALBUMARTIST"];
            mf.setAlbumArtist(values.toString(", ").toCString(true));
        }
        else {
            mf.setAlbumArtist(mf.artist());
        }

        unsigned int position = mf.trackPosition(), total = mf.trackTotal();
        if (tags.contains("TRACKNUMBER"))
            parsePosition(tags["TRACKNUMBER"], position, total);
        mf.setTrackPosition(position);
        mf.setTrackTotal(total);

        position = mf.discPosition();
        total = mf.discTotal();
        if (tags.contains("DISCNUMBER"))
            parsePosition(tags["DISCNUMBER"], position, total);
        mf.setDiscPosition(position);
        mf.setDiscTotal(total);

        // FIXME parse for more tags. See http://taglib.github.io/api/classTagLib_1_1PropertyMap.html#a8b0c96a6df5b64a36654dc843b2375bc
        // for a list of possible available tags
    }
}

void MetadataExtractor::setCameraDirectory(const std::string &path)
{
    cameraDirectory = path;
}

static std::string asciiLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return g_ascii_tolower((gchar) c); });
    return s;
}

std::string MetadataExtractor::getAlbumPathFromImage(const std::string& path)
{
    // Whatever the case of "DCIM", and only that folder: not "DCIMfoo"
    std::string pathInLower = asciiLower(path);
    std::string dcimPath = asciiLower(cameraDirectory) + "/";
    if (!cameraDirectory.empty() && pathInLower.compare(0, dcimPath.size(), dcimPath) == 0)
        return CameraAlbumPath;

    return path.substr(0, path.find_last_of("/"));
}

std::string MetadataExtractor::getAlbumNameFromPath(const std::string& path)
{
    if (path == CameraAlbumPath)
        return CameraAlbumName;
    return path.substr(path.find_last_of("/") + 1, path.size() - 1);
}

void MetadataExtractor::extractForImage(MediaFile &mf, const DetectedFile &d)
{
    mf.setMediaType("image");
    mf.setAlbumPath(getAlbumPathFromImage(mf.path()));
    mf.setAlbum(getAlbumNameFromPath(mf.albumPath()));

    // FIXME once we have exif detection override createdTime here with the correct value
    // mf.setCreatedTime(0);
}

MediaFile MetadataExtractor::extract(const DetectedFile &d)
{
    // Read through one descriptor, and only if it is a regular file: a FIFO
    // put in its place since it was found would block the main loop, a link
    // would be followed
    int fd = open(d.path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        throw runtime_error("Could not open " + d.path + ": " + strerror(errno));
    std::unique_ptr<int, void(*)(int*)> closer(&fd, [](int *f) { close(*f); });

    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
        throw runtime_error(d.path + " is not a regular file");

    MediaFile mf;
    mf.setPath(d.path);
    mf.setEtag(d.etag);
    mf.setType(d.type);
    mf.setMimeType(d.content_type);

    std::string fileNameWithExtension = d.path.substr(d.path.find_last_of('/') + 1);
    std::string pureFileName = fileNameWithExtension.substr(0, fileNameWithExtension.find_last_of("."));

    mf.setName(pureFileName);
    mf.setExtension(get_filename_extension(fileNameWithExtension));

    // FIXME better take the creation time of the file itself and not when we have discovered the file
    mf.setCreatedTime(time(NULL));

    mf.setModifiedTime(st.st_mtime);
    mf.setSize(st.st_size);

    if (d.type == AudioMedia)
        extractForAudio(mf, fd);
    else if (d.type == ImageMedia)
        extractForImage(mf, d);

    mf.rebuildSearchKey();

    return mf;
}

}
