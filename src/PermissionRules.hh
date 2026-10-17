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

#ifndef PERMISSIONRULES_HH
#define PERMISSIONRULES_HH

#include <string>

/*
 * What grantRead may hand out. Kept apart from the bus code so it can be
 * tested on its own.
 */
namespace permissionrules {

/// The one caller grantRead answers: the service that asked the user.
extern const char* const GrantingService;

/// An application id db8 can take as a caller: letters, digits, '.', '-'
/// and '_', not starting with '.' or '-', at most 128 characters. No '*',
/// which db8 reads as a wildcard and would grant every app at once.
bool isValidAppId(const std::string& appId);

/// One of the media kinds this indexer owns and fills with files the user
/// put on the device. Not com.palm.media.types or the music app's kinds.
bool isGrantableKind(const std::string& kind);

} // namespace permissionrules

#endif // PERMISSIONRULES_HH
