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
#include <ctime>
#include <vector>

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

namespace {

// A TIFF structure, as EXIF is one: byte order from its header, every read
// checked against its size. The data is whatever is in /media/internal.
struct Tiff {
    const unsigned char *data;
    size_t size;
    bool little;

    bool u16(size_t off, unsigned &v) const
    {
        if (off > size || size - off < 2)
            return false;
        v = little ? (data[off] | data[off + 1] << 8) : (data[off] << 8 | data[off + 1]);
        return true;
    }

    bool u32(size_t off, uint32_t &v) const
    {
        if (off > size || size - off < 4)
            return false;
        const unsigned char *b = data + off;
        v = little ? ((uint32_t) b[0] | (uint32_t) b[1] << 8 | (uint32_t) b[2] << 16 | (uint32_t) b[3] << 24)
                   : ((uint32_t) b[0] << 24 | (uint32_t) b[1] << 16 | (uint32_t) b[2] << 8 | (uint32_t) b[3]);
        return true;
    }

    // The offset of the tag's 12 byte entry in the IFD at ifd
    bool find(size_t ifd, unsigned tag, size_t &entry) const
    {
        unsigned count;
        if (!u16(ifd, count))
            return false;
        for (unsigned i = 0; i < count; i++) {
            size_t e = ifd + 2 + 12 * (size_t) i;
            if (e > size || size - e < 12)
                return false;
            unsigned t;
            u16(e, t);
            if (t == tag) {
                entry = e;
                return true;
            }
        }
        return false;
    }

    // An ASCII entry's text, up to its first NUL
    bool ascii(size_t entry, std::string &out) const
    {
        unsigned type;
        uint32_t count;
        if (!u16(entry + 2, type) || !u32(entry + 4, count) || type != 2 || count == 0 || count > 64)
            return false;
        size_t off = entry + 8;
        if (count > 4) {
            uint32_t at;
            if (!u32(entry + 8, at))
                return false;
            off = at;
        }
        if (off > size || size - off < count)
            return false;
        const char *s = (const char *) data + off;
        out.assign(s, strnlen(s, count));
        return true;
    }

    // An IFD offset (LONG or IFD type, one of it)
    bool pointer(size_t entry, uint32_t &out) const
    {
        unsigned type;
        uint32_t count;
        return u16(entry + 2, type) && (type == 4 || type == 13) && u32(entry + 4, count) && count == 1 &&
               u32(entry + 8, out);
    }
};

bool digits(const std::string &s, size_t at, size_t n, int &out)
{
    out = 0;
    for (size_t i = at; i < at + n; i++) {
        if (s[i] < '0' || s[i] > '9')
            return false;
        out = out * 10 + (s[i] - '0');
    }
    return true;
}

// "YYYY:MM:DD HH:MM:SS", in the camera's local time; with an offset
// ("+02:00") that time is placed exactly, without one it is taken as the
// device's local time
bool parseExifDate(const std::string &date, const std::string &offset, uint64_t &out)
{
    if (date.size() != 19 || date[4] != ':' || date[7] != ':' || date[10] != ' ' || date[13] != ':' || date[16] != ':')
        return false;
    int year, month, day, hour, minute, second;
    if (!digits(date, 0, 4, year) || !digits(date, 5, 2, month) || !digits(date, 8, 2, day) ||
        !digits(date, 11, 2, hour) || !digits(date, 14, 2, minute) || !digits(date, 17, 2, second))
        return false;
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60)
        return false;

    struct tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;

    time_t t;
    int offHours, offMinutes;
    if (offset.size() == 6 && (offset[0] == '+' || offset[0] == '-') && offset[3] == ':' &&
        digits(offset, 1, 2, offHours) && digits(offset, 4, 2, offMinutes) && offHours <= 14 && offMinutes <= 59) {
        t = timegm(&tm) - (offset[0] == '-' ? -1 : 1) * (offHours * 3600 + offMinutes * 60);
    } else {
        tm.tm_isdst = -1;
        t = mktime(&tm);
    }
    if (t < 0)
        return false;
    out = (uint64_t) t;
    return true;
}

bool tiffCaptureTime(const unsigned char *data, size_t size, uint64_t &out)
{
    if (size < 8)
        return false;
    Tiff tiff{data, size, false};
    if (data[0] == 'I' && data[1] == 'I')
        tiff.little = true;
    else if (!(data[0] == 'M' && data[1] == 'M'))
        return false;
    unsigned magic;
    uint32_t ifd0;
    if (!tiff.u16(2, magic) || magic != 42 || !tiff.u32(4, ifd0))
        return false;

    // DateTimeOriginal and OffsetTimeOriginal in the Exif IFD; failing that,
    // DateTime in IFD0. No IFD chains are followed.
    std::string date, offset;
    size_t entry;
    uint32_t exifIfd;
    if (tiff.find(ifd0, 0x8769, entry) && tiff.pointer(entry, exifIfd)) {
        if (tiff.find(exifIfd, 0x9003, entry))
            tiff.ascii(entry, date);
        if (tiff.find(exifIfd, 0x9011, entry))
            tiff.ascii(entry, offset);
    }
    if (date.empty() && tiff.find(ifd0, 0x0132, entry))
        tiff.ascii(entry, date);
    return parseExifDate(date, offset, out);
}

} // namespace

bool MetadataExtractor::captureTimeFromExif(const unsigned char *data, size_t size, uint64_t &out)
{
    if (size >= 2 && ((data[0] == 'I' && data[1] == 'I') || (data[0] == 'M' && data[1] == 'M')))
        return tiffCaptureTime(data, size, out);

    // A JPEG: segments up to the image data, the EXIF one is an APP1
    // starting "Exif\0\0"
    if (size < 4 || data[0] != 0xFF || data[1] != 0xD8)
        return false;
    size_t p = 2;
    while (p + 4 <= size) {
        if (data[p] != 0xFF)
            return false;
        unsigned marker = data[p + 1];
        if (marker == 0xFF) {
            p++;
            continue;
        }
        if (marker == 0xD9 || marker == 0xDA)
            return false;
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            p += 2;
            continue;
        }
        size_t length = (size_t) data[p + 2] << 8 | data[p + 3];
        if (length < 2 || length > size - p - 2)
            return false;
        if (marker == 0xE1 && length >= 8 && memcmp(data + p + 4, "Exif\0\0", 6) == 0)
            return tiffCaptureTime(data + p + 10, length - 8, out);
        p += 2 + length;
    }
    return false;
}

void MetadataExtractor::extractForImage(MediaFile &mf, int fd)
{
    mf.setMediaType("image");
    mf.setAlbumPath(getAlbumPathFromImage(mf.path()));
    mf.setAlbum(getAlbumNameFromPath(mf.albumPath()));

    // When the picture was taken, if it says. EXIF comes before the image
    // data, and is at most 64 KiB.
    std::vector<unsigned char> head(128 * 1024);
    ssize_t n = pread(fd, head.data(), head.size(), 0);
    uint64_t taken;
    if (n > 0 && captureTimeFromExif(head.data(), (size_t) n, taken)) {
        mf.setCreatedTime(taken);
        mf.setCreatedTimeFromFile(true);
    }
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
        extractForImage(mf, fd);

    mf.rebuildSearchKey();

    return mf;
}

}
