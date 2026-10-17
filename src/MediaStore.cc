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
static const int schemaVersion = 6;

// Versions whose db8 entries only lack fields added since: for those the file
// list alone is rebuilt, every file is read again and its entry updated, and
// what apps stored in the entries is kept. (6 added mimeType.)
static bool entriesStillUsable(int version)
{
    return version == 5;
}

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
        string msg(errmsg ? errmsg : sqlite3_errmsg(db));
        sqlite3_free(errmsg);
        throw runtime_error(msg);
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

static sqlite3 *openFileList(const char *path)
{
    sqlite3 *db = nullptr;
    int err = sqlite3_open(path, &db);
    if (err != SQLITE_OK) {
        string msg(db ? sqlite3_errmsg(db) : sqlite3_errstr(err));
        sqlite3_close(db);
        throw runtime_error(msg);
    }
    return db;
}

MediaStore::MediaStore(MojoMediaDatabase *mojoDb) :
    mFileDb(0),
    mMojoDb(mojoDb)
{
    if (!g_file_test(LUNA_DATA_DIR, G_FILE_TEST_EXISTS))
        g_mkdir_with_parents(LUNA_DATA_DIR, 0755);

    try {
        mFileDb = openFileList(LUNA_DATA_DIR "/filenotify.db3");

        int version = getSchemaVersion(mFileDb);
        if (version != schemaVersion) {
            deleteTables(mFileDb);
            createTables(mFileDb);
            if (!entriesStillUsable(version))
                mMojoDb->prepareForRebuild(true);
        }
    } catch (const exception &e) {
        // Kept in memory instead: every start then reads every file again
        // and updates its entry, but the index works. Throwing here would
        // end the service, and systemd would restart it into the same error.
        g_critical("Could not open the file list in %s, keeping it in memory: %s", LUNA_DATA_DIR, e.what());
        sqlite3_close(mFileDb);
        mFileDb = openFileList(":memory:");
        createTables(mFileDb);
    }
}

MediaStore::~MediaStore()
{
    int err = sqlite3_close(mFileDb);
    if (err != SQLITE_OK) {
        g_warning("Could not close database connection: %s", sqlite3_errmsg(mFileDb));
    }
}

void MediaStore::insert(const MediaFile &m)
{
    // The file counts as indexed once db8 has its entry. Recorded before,
    // an insert that failed, or did not run because the indexer stopped
    // with it still queued, left the file out of db8 until it changed.
    const string path = m.path();
    const string etag = m.etag();
    mMojoDb->insert(m, [this, path, etag]() {
        try {
            setETag(path, etag);
        } catch (const exception &e) {
            g_warning("Could not record %s as indexed: %s", path.c_str(), e.what());
        }
    });
}

void MediaStore::setETag(const string &path, const string &etag)
{
    Statement query(mFileDb, "INSERT OR REPLACE INTO files (path, etag) VALUES (?, ?)");
    query.bind(1, path);
    query.bind(2, etag);
    query.step();
}

void MediaStore::remove(const string &filename)
{
    Statement del(mFileDb, "DELETE FROM files WHERE path = ?");
    del.bind(1, filename);
    del.step();

    mMojoDb->remove(filename);
}

std::vector<std::string> MediaStore::filesBelowPath(const string &path)
{
    // A plain prefix comparison: with LIKE, "_" and "%" in a folder's name
    // would match other folders too, and case would not count. The prefix
    // ends in "/", or /m/Music would take /m/Music2 along. As blobs, so
    // that the length counts bytes rather than characters.
    string prefix = path;
    if (prefix.empty() || prefix.back() != '/')
        prefix += '/';
    Statement query(mFileDb, "SELECT path FROM files WHERE substr(CAST(path AS BLOB), 1, ?) = CAST(? AS BLOB)");
    query.bind(1, (int) prefix.size());
    query.bind(2, prefix);

    std::vector<std::string> paths;
    while (query.step())
        paths.push_back(query.getText(0));
    return paths;
}

void MediaStore::removeFilesBelowPath(const string &path)
{
    // Collected first, as remove() deletes from the table being read
    for (const auto &filePath : filesBelowPath(path))
        remove(filePath);
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
