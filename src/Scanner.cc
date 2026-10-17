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

#include "MetadataExtractor.hh"
#include "Scanner.hh"
#include "util.h"
#include <dirent.h>
#include <sys/stat.h>
#include <cstdio>
#include <memory>
#include <glib.h>
#include <algorithm>

using namespace std;

namespace mediascanner {

bool hasHiddenComponent(const std::string &path)
{
    for (size_t slash = path.find('/'); slash != string::npos; slash = path.find('/', slash + 1)) {
        if (slash + 1 < path.size() && path[slash + 1] == '.')
            return true;
    }
    return false;
}

bool isAtOrBelow(const std::string &path, const std::string &dir)
{
    if (path.compare(0, dir.size(), dir) != 0)
        return false;
    return path.size() == dir.size() || path[dir.size()] == '/' || (!dir.empty() && dir.back() == '/');
}

bool isInIgnoredDirectory(const std::string &path, const std::set<std::string> &ignoredDirectories)
{
    return std::any_of(ignoredDirectories.begin(), ignoredDirectories.end(),
                       [&path](const std::string &dir) { return isAtOrBelow(path, dir); });
}

Scanner::Scanner() {

}

Scanner::~Scanner() {

}

vector<DetectedFile> Scanner::scanFiles(MetadataExtractor *extractor, const std::string &root, const MediaType type,
                                        const std::set<std::string>& ignoredDirectories)
{
    vector<DetectedFile> result;
    unique_ptr<DIR, int(*)(DIR*)> dir(opendir(root.c_str()), closedir);
    printf("In subdir %s\n", root.c_str());
    if(!dir) {
        return result;
    }

    if (ignoredDirectories.find(root) != ignoredDirectories.end()) {
        g_warning("Ignoring directory %s", root.c_str());
        return result;
    }

    unique_ptr<struct dirent, void(*)(void*)> entry((dirent*)malloc(sizeof(dirent) + NAME_MAX),
                free);
    struct dirent *de;
    while(readdir_r(dir.get(), entry.get(), &de) == 0 && de ) {
        struct stat statbuf;
        string fname = entry.get()->d_name;
        if(fname[0] == '.') // Ignore hidden files and dirs.
            continue;
        string fullpath = root + "/" + fname;
        // Symbolic links are neither: they are not followed
        if (lstat(fullpath.c_str(), &statbuf) != 0)
            continue;
        if(S_ISREG(statbuf.st_mode)) {
            try {
                DetectedFile d = extractor->detect(fullpath);
                if (type == AllMedia || d.type == type) {
                    result.push_back(d);
                }
            } catch (const exception &e) {
                /* Ignore non-media files */
            }
        } else if(S_ISDIR(statbuf.st_mode)) {
            vector<DetectedFile> subdir = scanFiles(extractor, fullpath, type, ignoredDirectories);
            result.insert(result.end(), subdir.begin(), subdir.end());
        }
    }
    return result;
}

}
