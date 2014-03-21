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

#ifndef MEDIASCANNER_HH
#define MEDIASCANNER_HH

#include <glib.h>
#include <memory>
#include <string>
#include <map>
#include <set>

#include "ScannerCore.hh"

namespace mediascanner
{

class MediaStore;
class MetadataExtractor;
class SubtreeWatcher;
class MojoMediaDatabase;

class MediaScanner final
{
public:
    MediaScanner(MojoMediaDatabase *mojoDb);
    ~MediaScanner();

    void setup(const std::string& path, const std::set<std::string> dirsToIgnore);

private:
    void setupMountWatcher();
    void readFiles(MediaStore &store, const std::string &subdir, const MediaType type);
    void addDir(const std::string &dir);
    void removeDir(const std::string &dir);
    static gboolean sourceCallback(int, GIOCondition, gpointer data);
    void processEvents();
    void addMountedVolumes();
    void removeFilesBelowPath(MediaStore &store, const std::string &path);

    int mountfd;
    std::unique_ptr<GSource,void(*)(GSource*)> mount_source;
    int sigint_id, sigterm_id;
    std::string mountDir;
    std::string cachedir;
    std::unique_ptr<MediaStore> store;
    std::unique_ptr<MetadataExtractor> extractor;
    std::map<std::string, std::unique_ptr<SubtreeWatcher>> subtrees;
    std::set<std::string> ignoredDirectories;
};

} // namespace mediascanner

#endif // MEDIASCANNER_HH
