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

#include "PermissionGrants.hh"
#include "PermissionRules.hh"

#include <glib.h>

#include <cstring>
#include <set>
#include <string>

// More than the media kinds there are; a longer list is not a real request.
static const MojSize MaxKinds = 16;

class PermissionGrantHandler::Request : public MojSignalHandler
{
public:
    Request(PermissionGrantHandler* handler, MojServiceMessage* msg) :
        owner(handler),
        message(msg),
        slot(this, &Request::response)
    {
    }

    MojErr put(const MojObject::ObjectVec& permissions)
    {
        return owner->db.putPermissions(slot, permissions.begin(), permissions.end());
    }

private:
    MojErr response(MojObject& /* reply */, MojErr err)
    {
        MojRefCountedPtr<Request> self(this);
        if (err == MojErrNone) {
            message->replySuccess();
        } else {
            g_warning("grantRead: db8 refused the permissions (%d)", err);
            message->replyError(err, "The database refused the permissions");
        }
        owner->done(this);
        return MojErrNone;
    }

    PermissionGrantHandler* owner;
    MojRefCountedPtr<MojServiceMessage> message;
    MojDbClient::Signal::Slot<Request> slot;
};

PermissionGrantHandler::PermissionGrantHandler(MojDbClient& dbClient) :
    db(dbClient)
{
    addMethod("grantRead", (Callback) &PermissionGrantHandler::handleGrantRead);
}

void PermissionGrantHandler::done(Request* request)
{
    for (auto it = pending.begin(); it != pending.end(); ++it) {
        if (it->get() == request) {
            pending.erase(it);
            return;
        }
    }
}

MojErr PermissionGrantHandler::handleGrantRead(MojServiceMessage* msg, const MojObject& payload)
{
    // Only the service that asked the user. The bus checks the group too.
    const char* sender = msg->senderId();
    if (!sender || std::strcmp(sender, permissionrules::GrantingService) != 0) {
        g_warning("grantRead: refused a call from %s", sender ? sender : "(unknown)");
        return msg->replyError(MojErrAccessDenied, "Only com.palm.mediapermissions may grant access");
    }

    MojString appIdStr;
    bool found = false;
    MojErr err = payload.get("appId", appIdStr, found);
    if (err != MojErrNone || !found)
        return msg->replyError(MojErrInvalidArg, "appId must be a string");
    const std::string appId = appIdStr.data();
    if (!permissionrules::isValidAppId(appId))
        return msg->replyError(MojErrInvalidArg, "appId is not a valid application id");

    MojObject kinds;
    if (!payload.get("kinds", kinds) || kinds.type() != MojObject::TypeArray ||
        kinds.size() == 0 || kinds.size() > MaxKinds)
        return msg->replyError(MojErrInvalidArg, "kinds must be a list of 1 to 16 media kinds");

    std::set<std::string> wanted;
    for (MojObject::ConstArrayIterator it = kinds.arrayBegin(); it != kinds.arrayEnd(); ++it) {
        if (it->type() != MojObject::TypeString)
            return msg->replyError(MojErrInvalidArg, "kinds must hold strings");
        MojString kind;
        err = it->stringValue(kind);
        if (err != MojErrNone)
            return msg->replyError(MojErrInvalidArg, "kinds must hold strings");
        if (!permissionrules::isGrantableKind(kind.data()))
            return msg->replyError(MojErrInvalidArg, "not a media kind this indexer can grant");
        wanted.insert(kind.data());
    }

    MojObject::ObjectVec permissions;
    for (const std::string& kind : wanted) {
        MojObject operations;
        MojObject permission;
        err = operations.putString("read", "allow");
        MojErrCheck(err);
        err = permission.putString("type", "db.kind");
        MojErrCheck(err);
        err = permission.putString("object", kind.c_str());
        MojErrCheck(err);
        err = permission.putString("caller", appId.c_str());
        MojErrCheck(err);
        err = permission.put("operations", operations);
        MojErrCheck(err);
        err = permissions.push(permission);
        MojErrCheck(err);
    }

    MojRefCountedPtr<Request> request(new Request(this, msg));
    MojAllocCheck(request.get());
    err = request->put(permissions);
    if (err != MojErrNone)
        return msg->replyError(err, "Could not ask the database");
    pending.push_back(request);
    g_message("grantRead: %s may read %zu media kind(s)", appId.c_str(), wanted.size());
    return MojErrNone;
}
