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

#ifndef MEDIASCANNERSERVICEAPP_HH
#define MEDIASCANNERSERVICEAPP_HH

#include <glib.h>
#include <core/MojReactorApp.h>
#include <core/MojGmainReactor.h>
#include <luna/MojLunaService.h>
#include <db/MojDbServiceClient.h>

#include "MediaScanner.hh"
#include "MojoMediaDatabase.hh"

#include <set>
#include <string>

using namespace mediascanner;

class MediaScannerServiceApp : public MojReactorApp<MojGmainReactor>
{
public:
    static const char* const ServiceName;

    MediaScannerServiceApp();

    virtual MojErr open();
    virtual MojErr configure(const MojObject& conf);

private:
    typedef MojReactorApp<MojGmainReactor> Base;
    MojLunaService service;
    MojDbServiceClient db_client;
    MojoMediaDatabase database;
    MediaScanner media_scanner;
    std::string rootPath;
    std::set<std::string> ignoredDirectories;
};

#endif // MEDIASCANNERSERVICEAPP_HH
