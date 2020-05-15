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

#include<pwd.h>
#include<sys/select.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<sys/inotify.h>
#include<unistd.h>
#include<cstdio>
#include<cerrno>
#include<cstring>
#include<map>
#include<memory>
#include<cassert>
#include <dirent.h>
#include <stdexcept>

#include<glib.h>
#include<glib-unix.h>

#include "MediaFile.hh"
#include "MediaStore.hh"
#include "MetadataExtractor.hh"
#include "SubtreeWatcher.hh"
#include "Scanner.hh"
#include "util.h"
#include "MediaScanner.hh"

using namespace std;

using namespace mediascanner;

static std::string getCurrentUser() {
    int uid = geteuid();
    struct passwd *pwd = getpwuid(uid);
    if (pwd == NULL) {
            string msg("Could not look up user name: ");
            msg += strerror(errno);
            throw runtime_error(msg);
    }
    return pwd->pw_name;
}

MediaScanner::MediaScanner(MojoMediaDatabase *mojoDb) :
    sigint_id(0), sigterm_id(0)
{
    unique_ptr<MediaStore> tmp(new MediaStore(mojoDb));
    store = move(tmp);
    extractor.reset(new MetadataExtractor());
}

void MediaScanner::setup(const std::set<std::string> dirsToIgnore)
{
    ignoredDirectories = dirsToIgnore;
}

MediaScanner::~MediaScanner() {
    if (sigint_id != 0) {
        g_source_remove(sigint_id);
    }
    if (sigterm_id != 0) {
        g_source_remove(sigterm_id);
    }
}

void MediaScanner::addDir(const string &dir) {
    assert(dir[0] == '/');
    if(subtrees.find(dir) != subtrees.end()) {
        g_message("%s: already watching this directory", __PRETTY_FUNCTION__);
        return;
    }

    if (ignoredDirectories.find(dir) != ignoredDirectories.end()) {
        g_warning("Ignoring directory %s", dir.c_str());
        return;
    }

    unique_ptr<SubtreeWatcher> sw(new SubtreeWatcher(*store.get(), *extractor.get(), ignoredDirectories));
    readFiles(*store.get(), dir, AllMedia);
    sw->addDir(dir);
    subtrees[dir] = move(sw);
}

void MediaScanner::removeDir(const string &dir) {
    assert(dir[0] == '/');
    assert(subtrees.find(dir) != subtrees.end());
    subtrees.erase(dir);

    // FIXME we need to remove all files below this directory
    removeFilesBelowPath(*store.get(), dir);
}

void MediaScanner::removeFilesBelowPath(MediaStore &store, const string &path)
{
    store.removeFilesBelowPath(path);
}

void MediaScanner::readFiles(MediaStore &store, const string &subdir, const MediaType type) {
    Scanner s;
    vector<DetectedFile> files = s.scanFiles(extractor.get(), subdir, type, ignoredDirectories);
    for(auto &d : files) {
        // If the file is unchanged, skip it.
        if (d.etag == store.getETag(d.path))
            continue;
        try {
            store.insert(extractor->extract(d));
        } catch(const exception &e) {
            fprintf(stderr, "Error when indexing: %s\n", e.what());
        }
    }
}
