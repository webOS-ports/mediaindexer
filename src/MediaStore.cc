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

#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <glib.h>

#include "mozilla/fts3_tokenizer.h"
#include "MediaStore.hh"
#include "MediaFile.hh"
#include "Album.hh"
#include "internal/utils.hh"
#include "internal/sqliteutils.hh"
#include "MojoMediaDatabase.hh"

using namespace std;

namespace  mediascanner {

// Increment this whenever changing db schema.
// It will cause dbstore to rebuild its tables.
static const int schemaVersion = 1;

static int getSchemaVersion(sqlite3 *db)
{
    int version = -1;
    try {
        Statement select(db, "SELECT version FROM schemaVersion");
        if (select.step())
            version = select.getInt(0);
    } catch (const exception &e) {
        /* schemaVersion table might not exist */
    }
    return version;
}

static void execute_sql(sqlite3 *db, const string &cmd)
{
    char *errmsg = nullptr;
    if(sqlite3_exec(db, cmd.c_str(), nullptr, nullptr, &errmsg) != SQLITE_OK) {
        throw runtime_error(errmsg);
    }
}

void deleteTables(sqlite3 *db)
{
    string deleteCmd(R"(
DROP TABLE IF EXISTS files;
DROP TABLE IF EXISTS schemaVersion;
)");
    execute_sql(db, deleteCmd);
}

void createTables(sqlite3 *db)
{
    string schema(R"(
CREATE TABLE schemaVersion (version INTEGER);
CREATE TABLE files (
    path TEXT PRIMARY KEY NOT NULL,
    etag TEXT);
)");
    execute_sql(db, schema);

    Statement version(db, "INSERT INTO schemaVersion (version) VALUES (?)");
    version.bind(1, schemaVersion);
    version.step();
}

#define LUNA_DATA_DIR   "/var/luna/data"

MediaStore::MediaStore(MojoMediaDatabase *mojoDb) :
    mFileDb(0),
    mMojoDb(mojoDb)
{
    if (!g_file_test(LUNA_DATA_DIR, G_FILE_TEST_EXISTS))
        g_mkdir_with_parents(LUNA_DATA_DIR, 0755);

    int err = sqlite3_open(LUNA_DATA_DIR "/filenotify.db3", &mFileDb);
    if (err) {
        g_critical("Could not open database: %s", sqlite3_errmsg(mFileDb));
        return;
    }

    if (getSchemaVersion(mFileDb) != schemaVersion) {
        deleteTables(mFileDb);
        createTables(mFileDb);
        mMojoDb->prepareForRebuild();
    }
}

MediaStore::~MediaStore()
{
}

void MediaStore::insert(const MediaFile &m)
{
    Statement query(mFileDb, "INSERT OR REPLACE INTO files (path, etag) VALUES (?, ?)");
    string fileName = m.path();
    query.bind(1, fileName);
    query.bind(2, m.etag());
    query.step();

    mMojoDb->insert(m);
}

void MediaStore::remove(const string &filename)
{
    Statement del(mFileDb, "DELETE FROM files WHERE path = ?");
    del.bind(1, filename);
    del.step();

    mMojoDb->remove(filename);
}

void MediaStore::removeFilesBelowPath(const string &path)
{
    Statement query(mFileDb, "SELECT path FROM files WHERE path like ?");
    query.bind(1, string_format("%s%%", path.c_str()));

    while (query.step()) {
        std::string filePath = query.getText(0);
        remove(filePath);
    }
}

std::string MediaStore::getETag(const string &filename)
{
    Statement query(mFileDb, R"(
SELECT etag FROM files WHERE path = ?
)");
    query.bind(1, filename);
    if (query.step()) {
        return query.getText(0);
    } else {
        return "";
    }
}

} // namespace mediascanner
