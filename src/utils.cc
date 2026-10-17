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

#include"internal/utils.hh"

#include<vector>
#include<stdexcept>
#include<glib.h>
#include <memory>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace mediascanner {

std::string string_format(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *formatted = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    std::string result(formatted ? formatted : "");
    g_free(formatted);
    return result;
}


std::string sqlQuote(const std::string &input) {
    std::vector<char> out;
    out.reserve(input.size() + 2);
    const char quote = '\'';
    out.push_back(quote);
    for(size_t i=0; i<input.size(); i++) {
        char x = input[i];
        if(x == quote)
            out.push_back(quote);
        out.push_back(x);
    }
    out.push_back(quote);
    out.push_back('\0');
    return std::string(&out[0]);
}

// Convert filename into something that full text search
// will be able to find. That is, separate words with spaces.
#include<cstdio>
std::string filenameToTitle(const std::string &filename) {
    auto fname_start = filename.rfind('/');
    auto suffix_dot = filename.rfind('.');
    std::string result;
    if(fname_start == std::string::npos) {
        if(suffix_dot == std::string::npos) {
            result = filename;
        } else {
            result = filename.substr(0, suffix_dot);
        }
    } else {
        if(suffix_dot == std::string::npos) {
            result = filename.substr(fname_start+1, filename.size());
        } else {
            result = filename.substr(fname_start+1, suffix_dot-fname_start-1);
        }
    }
    for(size_t i=0; i<result.size(); i++) {
        auto c = result[i];
        if(c == '.' || c == '_' || c == '(' || c == ')' ||
           c == '[' || c == ']' || c == '{' || c == '}' ||
           c == '\\') {
            result[i] = ' ';
        }
    }
    return result;
}

std::string getUri(const std::string &filename) {
    GError *error = NULL;
    char *uristr = g_filename_to_uri(filename.c_str(), "", &error);
    if (error) {
        std::string msg("Could not build URI: ");
        msg += error->message;
        g_error_free(error);
        throw std::runtime_error(msg);
    }

    std::string uri(uristr);
    g_free(uristr);
    return uri;
}

int openOwnDirectory(const char *path)
{
    if (mkdir(path, 0755) != 0 && errno != EEXIST)
        return -1;
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return -1;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_uid != geteuid() || (st.st_mode & (S_IWGRP | S_IWOTH))) {
        close(fd);
        errno = EPERM;
        return -1;
    }
    return fd;
}

bool writeFileInOwnDirectory(const char *directory, const std::string &fileName,
                             const std::function<bool(int fd)> &write)
{
    int dirFd = openOwnDirectory(directory);
    if (dirFd < 0)
        return false;

    // Written beside its name and renamed into place: a link put at the name
    // is replaced rather than followed, and readers never see half a file
    std::string tmpName = "." + fileName + ".tmp";
    unlinkat(dirFd, tmpName.c_str(), 0);
    bool written = false;
    int fd = openat(dirFd, tmpName.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd >= 0) {
        errno = 0;
        written = write(fd);
        if (!written && errno == 0)
            errno = EIO;
        written = close(fd) == 0 && written;
        written = written && renameat(dirFd, tmpName.c_str(), dirFd, fileName.c_str()) == 0;
        int saved = errno;
        if (!written)
            unlinkat(dirFd, tmpName.c_str(), 0);
        errno = saved;
    }
    int saved = errno;
    close(dirFd);
    errno = saved;
    return written;
}

}
