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

#include <gst/gst.h>
#include "MediaScannerServiceApp.hh"
#include "MojoMediaDatabase.hh"

int main(int argc, char** argv)
{
    gst_init (&argc, &argv);
    MediaScannerServiceApp app;
    return app.main(argc, argv);
}

const char* const MediaScannerServiceApp::ServiceName = "org.webosports.mediaindexer";

MediaScannerServiceApp::MediaScannerServiceApp() :
    db_client(&service),
    database(db_client),
    media_scanner(&database),
    rootPath("/media/internal")
{
    s_log.level(MojLogger::LevelTrace);

    ignoredDirectories.insert("/media/internal/android/Android");
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

    media_scanner.setup(rootPath, ignoredDirectories);

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
