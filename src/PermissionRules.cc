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

#include "PermissionRules.hh"

namespace permissionrules {

const char* const GrantingService = "com.palm.mediapermissions";

static const char* const GrantableKinds[] = {
    "com.palm.media.audio.file:1",
    "com.palm.media.audio.album:1",
    "com.palm.media.audio.artist:1",
    "com.palm.media.audio.genre:1",
    "com.palm.media.image.file:1",
    "com.palm.media.image.album:1",
    "com.palm.media.misc.file:1",
    "com.palm.media.playlist.file:1",
    "com.palm.media.video.file:1",
};

bool isValidAppId(const std::string& appId)
{
    if (appId.empty() || appId.size() > 128)
        return false;
    if (appId[0] == '.' || appId[0] == '-')
        return false;
    for (char c : appId) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        if (!ok)
            return false;
    }
    return true;
}

bool isGrantableKind(const std::string& kind)
{
    for (const char* grantable : GrantableKinds) {
        if (kind == grantable)
            return true;
    }
    return false;
}

} // namespace permissionrules
