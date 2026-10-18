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

#ifndef OWNEDENTRIES_HH
#define OWNEDENTRIES_HH

#include <string>

/*
 * Which entries of the media kinds are not the indexer's. Kept apart from
 * the db8 code so it can be tested on its own.
 */
namespace ownedentries {

/// True for an entry of an account: one whose accountId is set, as the
/// photos service (com.palm.service.photos) sets it on the albums and
/// photos it syncs from a cloud account into /media/internal/.photosApp.
/// The indexer never scans there, so it must neither take such an entry
/// for a file that is gone nor count such an album as empty. Its own
/// albums have an empty accountId; "local" is the device's own too.
bool isAccountEntry(const std::string& accountId);

} // namespace ownedentries

#endif // OWNEDENTRIES_HH
