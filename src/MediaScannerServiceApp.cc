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

#include "config.h"
#include "MediaScannerServiceApp.hh"
#include "MojoMediaDatabase.hh"
#include "Scanner.hh"

#include <sys/stat.h>

int main(int argc, char** argv)
{
    MediaScannerServiceApp app;
    return app.main(argc, argv);
}

const char* const MediaScannerServiceApp::ServiceName = "org.webosports.service.mediaindexer";

static const char* const WallpaperPath = "/usr/share/wallpapers";

MediaScannerServiceApp::MediaScannerServiceApp() :
    db_client(&service),
    database(db_client),
    media_scanner(&database),
    rootPath("/media/internal")
{
    s_log.level(MojLogger::LevelTrace);

    ignoredDirectories.insert(THUMBNAIL_DIR);
    ignoredDirectories.insert("/media/internal/android/Android");
    ignoredDirectories.insert("/media/internal/.cache");
}

MojErr MediaScannerServiceApp::open()
{
    MojErr err;

    err = Base::open();
    MojErrCheck(err);

    err = service.open(ServiceName);
    MojErrCheck(err);

    err = service.attach(m_reactor.impl());
    MojErrCheck(err);

    // Before any entry is written: new indexes and readers of the kinds
    database.updateKinds();

    media_scanner.setup(ignoredDirectories, rootPath + "/DCIM");
    media_scanner.addDir(rootPath);
    media_scanner.addDir(WallpaperPath);

    // After the scan above has been queued: drop what was deleted, moved away
    // or hidden while the indexer was not running
    const std::string root = rootPath;
    const std::set<std::string> ignored = ignoredDirectories;
    database.reconcile([root, ignored](const std::string &path) {
        if (!mediascanner::isAtOrBelow(path, root) && !mediascanner::isAtOrBelow(path, WallpaperPath))
            return false;
        if (mediascanner::hasHiddenComponent(path) || mediascanner::isInIgnoredDirectory(path, ignored))
            return false;
        struct stat st;
        return lstat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
    });

    return MojErrNone;
}

MojErr MediaScannerServiceApp::configure(const MojObject &conf)
{
    MojErr err;
    err = MojServiceApp::configure(conf);
    MojErrCheck(err);

    MojObject rootPathObj;
    MojString rootPathStr;
    if (conf.get("rootPath", rootPathObj) && rootPathObj.stringValue(rootPathStr))
        rootPath = rootPathStr.data();

    MojObject ignoredDirectoriesObj;
    if (conf.get("ignoredDirectories", ignoredDirectoriesObj) &&
        ignoredDirectoriesObj.type() == MojObject::Type::TypeArray) {

        ignoredDirectories.clear();
        for (int n = 0; n < ignoredDirectoriesObj.size(); n++) {
            MojObject directoryObj;
            if (!ignoredDirectoriesObj.at(n, directoryObj))
                continue;

            MojString directoryStr;
            if (!directoryObj.stringValue(directoryStr))
                continue;

            ignoredDirectories.insert(directoryStr.data());
        }
    }

    return MojErrNone;
}
