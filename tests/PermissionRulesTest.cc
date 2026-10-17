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

/*
 * Unit tests for PermissionRules: what grantRead accepts as an application
 * id and as a media kind. No bus or db8 needed, so the x86-64 build runs
 * them on the build host and the device builds run them on a phone.
 */

#include "PermissionRules.hh"

#include <cstdio>
#include <string>

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n",           \
                         __FILE__, __LINE__, #cond);                    \
            failures++;                                                 \
        }                                                               \
    } while (0)

using permissionrules::isGrantableKind;
using permissionrules::isValidAppId;

static void appIds()
{
    CHECK(isValidAppId("org.example.app"));
    CHECK(isValidAppId("org.example-app_2"));
    CHECK(isValidAppId("_private"));
    CHECK(isValidAppId(std::string(128, 'a')));

    CHECK(!isValidAppId(""));
    CHECK(!isValidAppId(std::string(129, 'a')));
    // db8 reads "*" as a wildcard; a grant to it would reach every app.
    CHECK(!isValidAppId("*"));
    CHECK(!isValidAppId("org.example.*"));
    CHECK(!isValidAppId(".org"));
    CHECK(!isValidAppId("-org"));
    CHECK(!isValidAppId("org example"));
    CHECK(!isValidAppId("org/example"));
    CHECK(!isValidAppId(std::string("org\0x", 5)));
    CHECK(!isValidAppId("org.\xc3\xa9xample"));
}

static void kinds()
{
    const char* const grantable[] = {
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
    for (const char* kind : grantable)
        CHECK(isGrantableKind(kind));

    CHECK(!isGrantableKind(""));
    CHECK(!isGrantableKind("com.palm.media.permissions:1"));
    CHECK(!isGrantableKind("com.palm.media.video.file:2"));
    CHECK(!isGrantableKind("com.palm.media.video.file"));
    CHECK(!isGrantableKind("com.palm.media.video.file:1 "));
    CHECK(!isGrantableKind("com.palm.media."));
    CHECK(!isGrantableKind("com.palm.contact:1"));
    CHECK(!isGrantableKind("*"));
}

static void grantingService()
{
    CHECK(std::string(permissionrules::GrantingService) == "com.palm.mediapermissions");
}

int main()
{
    appIds();
    kinds();
    grantingService();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("PermissionRulesTest: all passed\n");
    return 0;
}
