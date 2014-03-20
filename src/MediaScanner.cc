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

#include<glib.h>
#include<glib-unix.h>
#include<gst/gst.h>

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
    mount_source(nullptr, g_source_unref), sigint_id(0), sigterm_id(0)
{
    unique_ptr<MediaStore> tmp(new MediaStore(mojoDb));
    store = move(tmp);
    extractor.reset(new MetadataExtractor());
}

void MediaScanner::setup(const std::string& path, const std::set<std::string> dirsToIgnore)
{
    mountDir = path;
    ignoredDirectories = dirsToIgnore;
    setupMountWatcher();
    addMountedVolumes();
}

MediaScanner::~MediaScanner() {
    if (sigint_id != 0) {
        g_source_remove(sigint_id);
    }
    if (sigterm_id != 0) {
        g_source_remove(sigterm_id);
    }
    if (mount_source) {
        g_source_destroy(mount_source.get());
    }
    close(mountfd);
}

void MediaScanner::addDir(const string &dir) {
    assert(dir[0] == '/');
    if(subtrees.find(dir) != subtrees.end()) {
        return;
    }

    if (ignoredDirectories.find(dir) != ignoredDirectories.end()) {
        g_warning("Ignoring directory %s", dir.c_str());
        return;
    }

    unique_ptr<SubtreeWatcher> sw(new SubtreeWatcher(*store.get(), *extractor.get(), ignoredDirectories));
    store->restoreItems(dir);
    store->pruneDeleted();
    readFiles(*store.get(), dir, AllMedia);
    sw->addDir(dir);
    subtrees[dir] = move(sw);
}

void MediaScanner::removeDir(const string &dir) {
    assert(dir[0] == '/');
    assert(subtrees.find(dir) != subtrees.end());
    subtrees.erase(dir);
}

void MediaScanner::readFiles(MediaStore &store, const string &subdir, const MediaType type) {
    Scanner s;
    vector<DetectedFile> files = s.scanFiles(extractor.get(), subdir, type, ignoredDirectories);
    for(auto &d : files) {
        // If the file is unchanged, skip it.
        if (d.etag == store.getETag(d.filename))
            continue;
        try {
            store.insert(extractor->extract(d));
        } catch(const exception &e) {
            fprintf(stderr, "Error when indexing: %s\n", e.what());
        }
    }
}

gboolean MediaScanner::sourceCallback(int, GIOCondition, gpointer data) {
    MediaScanner *daemon = static_cast<MediaScanner*>(data);
    daemon->processEvents();
    return TRUE;
}

void MediaScanner::setupMountWatcher() {
    mountfd = inotify_init();
    if(mountfd < 0) {
        string msg("Could not init inotify: ");
        msg += strerror(errno);
        throw runtime_error(msg);
    }
    int wd = inotify_add_watch(mountfd, mountDir.c_str(),
            IN_CREATE |  IN_DELETE | IN_ONLYDIR);
    if(wd == -1) {
        if (errno == ENOENT) {
            printf("Mount directory does not exist\n");
            return;
        }
        string msg("Could not create inotify watch object: ");
        msg += strerror(errno);
        throw runtime_error(msg);
    }

    mount_source.reset(g_unix_fd_source_new(mountfd, G_IO_IN));
    g_source_set_callback(mount_source.get(), reinterpret_cast<GSourceFunc>(&MediaScanner::sourceCallback), this, nullptr);
    g_source_attach(mount_source.get(), nullptr);
}

void MediaScanner::processEvents() {
    const int BUFSIZE= 4096;
    char buf[BUFSIZE];
    bool changed = false;
    ssize_t num_read;
    num_read = read(mountfd, buf, BUFSIZE);
    if(num_read == 0) {
        printf("Inotify returned 0.\n");
        return;
    }
    if(num_read == -1) {
        printf("Read error.\n");
        return;
    }
    for(char *p = buf; p < buf + num_read;) {
        struct inotify_event *event = (struct inotify_event *) p;
        string directory = mountDir;
        string filename(event->name);
        string abspath = directory + '/' + filename;
        struct stat statbuf;
        lstat(abspath.c_str(), &statbuf);
        if(S_ISDIR(statbuf.st_mode)) {
            if(event->mask & IN_CREATE) {
                printf("Volume %s was mounted.\n", abspath.c_str());
                addDir(abspath);
                changed = true;
            } else if(event->mask & IN_DELETE){
                printf("Volume %s was unmounted.\n", abspath.c_str());
                removeDir(abspath);
                changed = true;
            }
        }
        p += sizeof(struct inotify_event) + event->len;
    }
}

void MediaScanner::addMountedVolumes() {
    unique_ptr<DIR, int(*)(DIR*)> dir(opendir(mountDir.c_str()), closedir);
    if(!dir) {
        return;
    }
    unique_ptr<struct dirent, void(*)(void*)> entry((dirent*)malloc(sizeof(dirent) + NAME_MAX),
            free);
    struct dirent *de;
    while(readdir_r(dir.get(), entry.get(), &de) == 0 && de ) {
        struct stat statbuf;
        string fname = entry.get()->d_name;
        if(fname[0] == '.')
            continue;
        string fullpath = mountDir + "/" + fname;
        lstat(fullpath.c_str(), &statbuf);
        if(S_ISDIR(statbuf.st_mode)) {
            addDir(fullpath);
        }
    }
}
