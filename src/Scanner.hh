/*
 * Copyright (C) 2013 Canonical, Ltd.
 *
 * Authors:
 *    Jussi Pakkanen <jussi.pakkanen@canonical.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of version 3 of the GNU General Public License as published
 * by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SCANNER_HH_
#define SCANNER_HH_

#include <string>
#include <vector>
#include <set>

#include "ScannerCore.hh"

namespace mediascanner {

class DetectedFile;
class MetadataExtractor;

/* True when a component of the absolute path, after the leading '/', starts
 * with a dot: such files and folders are not indexed. */
bool hasHiddenComponent(const std::string &path);

/* True when path is dir itself or lies below it. */
bool isAtOrBelow(const std::string &path, const std::string &dir);

/* True when path is at or below one of the ignored directories. */
bool isInIgnoredDirectory(const std::string &path, const std::set<std::string> &ignoredDirectories);

class Scanner final {
public:
    Scanner();
    ~Scanner();

    std::vector<DetectedFile> scanFiles(MetadataExtractor *extractor, const std::string &root, const MediaType type,
                                        const std::set<std::string>& ignoredDirectories);

};

}

#endif
