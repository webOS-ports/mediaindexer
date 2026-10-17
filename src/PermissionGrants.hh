/*
 * Copyright (C) 2026 Herman van Hazendonk <github.com@herrie.org>
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

#ifndef PERMISSIONGRANTS_HH
#define PERMISSIONGRANTS_HH

#include <core/MojService.h>
#include <core/MojServiceMessage.h>
#include <db/MojDbClient.h>

#include <list>

/*
 * grantRead: lets an application read media kinds this indexer owns.
 *
 * db8 lets only a kind's owner (or db8 itself) change who may read it, and
 * this indexer owns the com.palm.media.* kinds. com.palm.mediapermissions
 * asks the user whether an application may read the user's media; when
 * the user says yes, it calls
 *
 *   luna://org.webosports.service.mediaindexer/grantRead
 *   {"appId": "org.example.app", "kinds": ["com.palm.media.video.file:1"]}
 *
 * and this puts the read permissions as the owner. Only that service may
 * call it: the bus admits only holders of mediaindexer.permissions, an
 * "oem" group, and the handler checks the sender's name besides.
 */
class PermissionGrantHandler : public MojService::CategoryHandler
{
public:
    explicit PermissionGrantHandler(MojDbClient& dbClient);

private:
    class Request;
    friend class Request;

    MojErr handleGrantRead(MojServiceMessage* msg, const MojObject& payload);
    void done(Request* request);

    MojDbClient& db;
    std::list<MojRefCountedPtr<Request> > pending;
};

#endif // PERMISSIONGRANTS_HH
