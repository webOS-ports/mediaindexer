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
 * Unit tests for OwnedEntries: which entries the indexer leaves to the
 * service that wrote them. No bus or db8 needed.
 */

#include "OwnedEntries.hh"

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

using ownedentries::isAccountEntry;

int main()
{
    // What the photos service writes: the account's db8 id
    CHECK(isAccountEntry("++JZ4K3lu+xYZabc"));
    CHECK(isAccountEntry("2+L1"));

    // The indexer's own albums carry "", the device's own account is "local"
    CHECK(!isAccountEntry(""));
    CHECK(!isAccountEntry("local"));

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("OwnedEntriesTest: all passed\n");
    return 0;
}
