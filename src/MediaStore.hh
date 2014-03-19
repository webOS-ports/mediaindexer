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

#ifndef MEDIASTORE_H_
#define MEDIASTORE_H_

#include <vector>
#include <string>
#include <sqlite3.h>

#include "ScannerCore.hh"

namespace mediascanner {

class MediaFile;
class Album;

class MediaStore final
{
public:
    MediaStore();
    MediaStore(const MediaStore &other) = delete;
    MediaStore operator=(const MediaStore &other) = delete;
    ~MediaStore();

    void insert(const MediaFile &m);
    void remove(const std::string &fileName);
    std::string getETag(const std::string &filename);
    size_t size() const;
    void pruneDeleted();

    void archiveItems(const std::string &prefix);
    void restoreItems(const std::string &prefix);

private:
    sqlite3 *mFileDb;
};

} // namespace mediascanner

#endif
