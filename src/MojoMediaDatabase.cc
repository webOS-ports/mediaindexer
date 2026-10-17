/*
 * Copyright (C) 2014 Simon Busch <morphis@gravedo.de>
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

#include <QFile>
#include <QImage>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

#include <Settings.h>

#include "config.h"
#include "MojoMediaDatabase.hh"
#include "MojoMediaObjectSerializer.hh"
#include "internal/utils.hh"
#include "MediaFile.hh"

#ifndef DB_KIND_DIRECTORY
#define DB_KIND_DIRECTORY "/etc/palm/db/kinds/"
#endif
#ifndef DB_PERMISSION_DIRECTORY
#define DB_PERMISSION_DIRECTORY "/etc/palm/db/permissions/"
#endif
// How long a command waits for db8 to reply
#ifndef COMMAND_TIMEOUT_SECONDS
#define COMMAND_TIMEOUT_SECONDS 30
#endif

namespace mediascanner
{

const int MojoMediaDatabase::CommandTimeoutSeconds = COMMAND_TIMEOUT_SECONDS;

void CheckErr(MojErr err, const char* filename, int line)
{
    if(err)
        throw std::runtime_error(string_format("%s: %d: err %d", filename, line, (int) err));
}

void CheckErr(MojObject& response, MojErr err, const char* filename, int line)
{
    if(err) {
        bool hasErrorText = false;
        MojString errorText;
        if(response.get("errorText", errorText, hasErrorText) == MojErrNone && hasErrorText) {
            throw std::runtime_error(string_format("%s: %d: err %d message %s", filename, line, (int) err,
                                                   errorText.data()));
        } else {
            throw std::runtime_error(string_format("%s: %d: err %d", filename, line, (int) err));
        }
    }
}

#define ErrorToException(err) \
    do { \
        if(err) CheckErr(err, __FILE__, __LINE__); \
    } while(0);

#define ResponseToException(response, err) \
    do { \
        if(err) CheckErr(response, err, __FILE__, __LINE__); \
    } while(0);

std::string DescribeErr(MojObject& response, MojErr err, const char* filename, int line)
{
    bool hasErrorText = false;
    MojString errorText;
    if (response.get("errorText", errorText, hasErrorText) == MojErrNone && hasErrorText)
        return string_format("%s: %d: err %d message %s", filename, line, (int) err, errorText.data());
    return string_format("%s: %d: err %d", filename, line, (int) err);
}

// In the handlers of database replies: an exception thrown there would not
// reach executeNextCommand(), and the command would never finish, which
// stops the whole queue. So a failure ends the command instead.
#define FailOnError(err) \
    do { \
        if(err) { \
            database->fail(string_format("%s: %d: err %d", __FILE__, __LINE__, (int) (err))); \
            return MojErrNone; \
        } \
    } while(0)

// Also what every reply handler starts with: a command given up on does
// nothing more with a reply that comes late, as what it would act on is old
#define IgnoreIfAbandoned() \
    do { \
        if (abandoned()) \
            return MojErrNone; \
    } while(0)

#define FailOnResponseError(response, err) \
    do { \
        IgnoreIfAbandoned(); \
        if(err) { \
            database->fail(DescribeErr(response, err, __FILE__, __LINE__)); \
            return MojErrNone; \
        } \
    } while(0)

// What a command sees of the queue. A command given up on, as no reply
// came in time, may still get its reply later: that must not finish or fail
// whichever command runs by then.
class CommandQueue
{
public:
    CommandQueue(MojoMediaDatabase *database, const BaseCommand *owner) :
        database(database),
        owner(owner)
    {
    }

    CommandQueue *operator->() { return this; }
    operator MojoMediaDatabase*() const { return database; }

    void finish();
    void fail(const std::string &message);
    void enqueue(BaseCommand *command, bool restart = true) { database->enqueue(command, restart); }
    void enqueueFront(BaseCommand *command) { database->enqueueFront(command); }
    MojDbClient& databaseClient() const { return database->databaseClient(); }

private:
    MojoMediaDatabase *database;
    const BaseCommand *owner;
};

class BaseCommand : public MojSignalHandler
{
public:
    BaseCommand(const std::string& name, MojoMediaDatabase *database) :
        database(database, this),
        _name(name)
    {
    }

    virtual void execute() = 0;

    // The same command, to send again when no reply came; none by default
    virtual BaseCommand *again() const { return nullptr; }

    std::string name() const { return _name; }

    void abandon() { _abandoned = true; }
    bool abandoned() const { return _abandoned; }

protected:
    CommandQueue database;
    std::string _name;

private:
    bool _abandoned = false;
};

void CommandQueue::finish()
{
    if (!owner->abandoned())
        database->finish();
}

void CommandQueue::fail(const std::string &message)
{
    if (owner->abandoned())
        g_message("%s: after it was given up on: %s", owner->name().c_str(), message.c_str());
    else
        database->fail(message);
}

// A find answers with at most 500 results, so totals counted from the
// results stopped at 500. Asked for a count, db8 counts every match; one
// result is all that comes back.
static MojErr findCount(MojoMediaDatabase *database, MojDbClient::Signal::SlotRef slot, MojDbQuery query)
{
    query.limit(1);
    return database->databaseClient().find(slot, query, false, true);
}

static MojErr mergeTotal(MojoMediaDatabase *database, MojDbClient::Signal::SlotRef slot,
                         const MojObject &id, const MojObject &total)
{
    MojObject toMerge;
    MojErr err = toMerge.put("_id", id);
    MojErrCheck(err);
    err = toMerge.put("total", total);
    MojErrCheck(err);

    MojObject::ObjectVec objects;
    err = objects.push(toMerge);
    MojErrCheck(err);

    return database->databaseClient().merge(slot, objects.begin(), objects.end());
}

// An album, genre or artist no file is in any more goes
static MojErr deleteEntry(MojoMediaDatabase *database, MojDbClient::Signal::SlotRef slot, const MojObject &id)
{
    MojObject::ObjectVec ids;
    MojErr err = ids.push(id);
    MojErrCheck(err);
    return database->databaseClient().del(slot, ids.begin(), ids.end(), MojDbFlagPurge);
}

// Deletes a thumbnail the indexer made, named as stored in an entry; only a
// file directly in its own folder, opened as it was written
static void deleteThumbnail(const MojObject &thumbnail)
{
    MojString path;
    bool found = false;
    if (thumbnail.get("path", path, found) != MojErrNone || !found)
        return;
    const std::string dir = std::string(THUMBNAIL_DIR) + "/";
    const std::string p = path.data();
    if (p.compare(0, dir.size(), dir) != 0 || p.find('/', dir.size()) != std::string::npos || p.size() == dir.size())
        return;
    int dirFd = openOwnDirectory(THUMBNAIL_DIR);
    if (dirFd < 0)
        return;
    unlinkat(dirFd, p.c_str() + dir.size(), 0);
    close(dirFd);
}

class GenerateAlbumThumbnailsCommand : public BaseCommand
{
public:
    GenerateAlbumThumbnailsCommand(MojoMediaDatabase *database, const MojObject& albumId) :
        BaseCommand("GenerateAlbumThumbnailsCommand", database),
        albumId(albumId)
    {
    }

    void execute()
    {
        database->finish();
    }

private:
    MojObject albumId;
};

#include "MojoMediaDatabaseImage.cc"
#include "MojoMediaDatabaseAudio.cc"

// What a file's entry is selected with, so that what it belonged to can be
// counted again once it is gone or changed
static void selectCollections(MojDbQuery &query)
{
    static const char* const props[] = { "_kind", "album", "albumArtist", "genre", "artist", "albumId", "appGridThumbnail" };
    for (const char *prop : props)
        query.select(prop);
}

static void enqueueRecounts(MojoMediaDatabase *database, const MojObject &entry);

// After an entry was removed: what it was in is counted again, and its
// thumbnail goes
static void forgetEntry(MojoMediaDatabase *database, const MojObject &entry)
{
    enqueueRecounts(database, entry);
    MojObject thumbnail;
    if (entry.get("appGridThumbnail", thumbnail))
        deleteThumbnail(thumbnail);
}

// Counts again the album, genre and artist, or the image album, an entry was in
static void enqueueRecounts(MojoMediaDatabase *database, const MojObject &entry)
{
    MojString kind;
    bool found = false;
    if (entry.get("_kind", kind, found) != MojErrNone || !found)
        return;

    if (strncmp(kind.data(), "com.palm.media.audio.file:", strlen("com.palm.media.audio.file:")) == 0) {
        MojObject album, albumArtist, genre, artist;
        if (entry.get("album", album) && entry.get("albumArtist", albumArtist))
            database->enqueue(new RecountAudioCollectionCommand(database, RecountAudioCollectionCommand::Album,
                                                                album, albumArtist), false);
        if (entry.get("genre", genre))
            database->enqueue(new RecountAudioCollectionCommand(database, RecountAudioCollectionCommand::Genre,
                                                                genre, MojObject()), false);
        if (entry.get("artist", artist))
            database->enqueue(new RecountAudioCollectionCommand(database, RecountAudioCollectionCommand::Artist,
                                                                artist, MojObject()), false);
    }
    else if (strncmp(kind.data(), "com.palm.media.image.file:", strlen("com.palm.media.image.file:")) == 0) {
        // "" until the image was assigned to its album
        MojObject albumId;
        MojString albumIdStr;
        if (entry.get("albumId", albumId) && albumId.stringValue(albumIdStr) == MojErrNone && !albumIdStr.empty())
            database->enqueue(new CountAlbumImagesCommand(database, albumId), false);
    }
}

class InsertCommand : public BaseCommand
{
public:
    InsertCommand(MojoMediaDatabase *database, MediaFile file, std::function<void()> stored) :
        BaseCommand("Insert", database),
        find_existing_slot(this, &InsertCommand::FindExistingResponse),
        insert_slot(this, &InsertCommand::InsertResponse),
        update_slot(this, &InsertCommand::UpdateResponse),
        file(file),
        stored(stored)
    {
    }

    BaseCommand *again() const
    {
        if (retried)
            return nullptr;
        InsertCommand *command = new InsertCommand(database, file, stored);
        command->retried = true;
        return command;
    }

    void execute()
    {
        MojDbQuery query;
        query.select("_id");
        selectCollections(query);
        for (const char* field : keptFields)
            query.select(field);

        // We're querying for the base type of all stored files here to avoid
        // querying each type separately
        query.from("com.palm.media.file:1");

        MojString filenameStr;
        filenameStr.assign(file.path().c_str());
        MojObject filenameObj(filenameStr);
        query.where("path", MojDbQuery::OpEq, filenameObj);

        MojErr err = database->databaseClient().find(find_existing_slot, query);
        ErrorToException(err);
    }

protected:
    // What apps keep in an entry (a bookmark, a play position, thumbnails),
    // when the file was first found, and the image album it was put in (the
    // path, and so the album, is the same; written as "" until the album is
    // looked up again, its album would count no images and be deleted): a
    // file written again leaves them as they are. A field the entry does not
    // have yet is filled in.
    static constexpr const char* keptFields[] = {
        "bookmark", "playbackPosition", "lastPlayTime", "thumbnails", "serviced",
        "hasResizedThumbnails", "appCacheCompleted", "appCacheComplete", "createdTime", "albumId"
    };

    MojDbClient::Signal::Slot<InsertCommand> find_existing_slot;
    MojDbClient::Signal::Slot<InsertCommand> insert_slot;
    MojDbClient::Signal::Slot<InsertCommand> update_slot;

    MojErr FindExistingResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        FailOnError(err);

        MojObject::ObjectVec objectsToInsert;
        MojObject mediaObj;
        MojoMediaObjectSerializer::SerializeToDatabaseObject(file, mediaObj);

        if (results.size() > 0 && results.at(0, existing)) {
            // A file written again: its size, times and tags change
            for (const char* field : keptFields) {
                if (!existing.contains(field))
                    continue;
                // Unless the file itself says when it was made
                if (strcmp(field, "createdTime") == 0 && file.createdTimeFromFile())
                    continue;
                bool deleted = false;
                err = mediaObj.del(field, deleted);
                FailOnError(err);
            }
            MojObject existingId;
            err = existing.getRequired("_id", existingId);
            FailOnError(err);
            err = mediaObj.put("_id", existingId);
            FailOnError(err);
            err = objectsToInsert.push(mediaObj);
            FailOnError(err);
            err = database->databaseClient().merge(update_slot, objectsToInsert.begin(), objectsToInsert.end());
            FailOnError(err);
            return MojErrNone;
        }

        objectsToInsert.push(mediaObj);

        err = database->databaseClient().put(insert_slot, objectsToInsert.begin(), objectsToInsert.end());
        FailOnError(err);

        return MojErrNone;
    }

    MojErr InsertResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        FailOnError(err);

        if (results.size() == 0) {
            database->finish();
            return MojErrNone;
        }

        MojObject item;
        if(!results.at(0, item)) {
            database->finish();
            return MojErrNone;
        }

        MojObject idToUpdate;
        err = item.getRequired("id", idToUpdate);
        FailOnError(err);

        if (stored)
            stored();

        enqueueFollowUps(idToUpdate);

        database->finish();

        return MojErrNone;
    }

    MojErr UpdateResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);
        if (stored)
            stored();

        // Its thumbnail, album, genre and artist follow what the file is now;
        // what it was in before is counted again, as it may have left them
        MojObject id;
        MojErr err = existing.getRequired("_id", id);
        FailOnError(err);
        enqueueFollowUps(id);
        enqueueRecounts(database, existing);

        database->finish();
        return MojErrNone;
    }

    void enqueueFollowUps(const MojObject &idToUpdate)
    {
        if (file.type() == ImageMedia && file.albumPath().size() > 0) {

            database->enqueue(new GenerateImageThumbnailCommand(database, idToUpdate, file.path()), false);
            database->enqueue(new AddAlbumForImageCommand(database, file, idToUpdate), false);
        }
        else if (file.type() == AudioMedia) {

            if (file.album().size() > 0) {
                database->enqueue(new AddAlbumForAudioCommand(database, file, idToUpdate), false);
            }
            if (file.genre().size() > 0) {
                database->enqueue(new AddGenreForAudioCommand(database, file, idToUpdate), false);
            }
            if (file.artist().size() > 0) {
                database->enqueue(new AddArtistForAudioCommand(database, file, idToUpdate), false);
            }
        }
    }

private:
    MediaFile file;
    std::function<void()> stored;
    MojObject existing;
    bool retried = false;
};

/*
 * Removes the entries of files that are no longer there to index: deleted,
 * moved away or into a hidden folder while the indexer was not running, or
 * below a folder that is now ignored. Goes through every entry, a page at a
 * time.
 */
class ReconcileCommand : public BaseCommand
{
public:
    ReconcileCommand(MojoMediaDatabase *database, std::function<bool(const std::string&)> stillIndexed) :
        BaseCommand("Reconcile", database),
        find_slot(this, &ReconcileCommand::FindResponse),
        remove_slot(this, &ReconcileCommand::RemoveResponse),
        stillIndexed(stillIndexed),
        removed(0)
    {
    }

    void execute()
    {
        MojDbQuery query;
        query.select("_id");
        query.select("path");
        selectCollections(query);
        query.from("com.palm.media.file:1");
        query.limit(PageSize);
        if (havePage)
            query.page(page);

        MojErr err = database->databaseClient().find(find_slot, query);
        ErrorToException(err);
    }

protected:
    static const MojUInt32 PageSize = 200;

    MojDbClient::Signal::Slot<ReconcileCommand> find_slot;
    MojDbClient::Signal::Slot<ReconcileCommand> remove_slot;

    MojErr FindResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        FailOnError(err);

        MojObject::ObjectVec removeIds;
        for (MojSize n = 0; n < results.size(); n++) {
            MojObject item;
            if (!results.at(n, item))
                continue;

            MojString pathStr;
            bool havePath = false;
            err = item.get("path", pathStr, havePath);
            FailOnError(err);
            if (havePath && stillIndexed(pathStr.data()))
                continue;

            MojObject idToRemove;
            err = item.getRequired("_id", idToRemove);
            FailOnError(err);
            err = removeIds.push(idToRemove);
            FailOnError(err);
            err = removedEntries.push(item);
            FailOnError(err);
        }

        MojObject next;
        havePage = response.get("next", next) && next.type() == MojObject::TypeString;
        if (havePage) {
            err = page.fromObject(next);
            FailOnError(err);
        }

        if (removeIds.size() > 0) {
            removed += static_cast<unsigned long>(removeIds.size());
            err = database->databaseClient().del(remove_slot, removeIds.begin(), removeIds.end(), MojDbFlagPurge);
            FailOnError(err);
            return MojErrNone;
        }

        return nextPage();
    }

    MojErr RemoveResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);
        for (const MojObject &entry : removedEntries)
            forgetEntry(database, entry);
        return nextPage();
    }

    MojErr nextPage()
    {
        if (havePage) {
            // The same command again, from where this page ended
            database->enqueueFront(new ReconcileCommand(*this));
        } else {
            g_message("Reconciled the index: %lu entries of files no longer there removed", removed);
        }
        database->finish();
        return MojErrNone;
    }

private:
    ReconcileCommand(const ReconcileCommand& other) :
        BaseCommand("Reconcile", other.database),
        find_slot(this, &ReconcileCommand::FindResponse),
        remove_slot(this, &ReconcileCommand::RemoveResponse),
        stillIndexed(other.stillIndexed),
        removed(other.removed),
        havePage(other.havePage),
        page(other.page)
    {
    }

    std::function<bool(const std::string&)> stillIndexed;
    MojObject::ObjectVec removedEntries;
    unsigned long removed;
    bool havePage = false;
    MojDbQuery::Page page;
};

/*
 * Counts every album, genre and artist again, a page at a time, so that the
 * empty ones go: those left by files removed before empty ones were deleted.
 */
class RecountCollectionsCommand : public BaseCommand
{
public:
    RecountCollectionsCommand(MojoMediaDatabase *database, unsigned kind = 0) :
        BaseCommand("RecountCollections", database),
        find_slot(this, &RecountCollectionsCommand::FindResponse),
        kind(kind)
    {
    }

    void execute()
    {
        MojDbQuery query;
        query.select("_id");
        query.select("name");
        query.select("artist");
        query.from(Kinds[kind]);
        query.limit(PageSize);
        if (havePage)
            query.page(page);

        MojErr err = database->databaseClient().find(find_slot, query);
        ErrorToException(err);
    }

protected:
    static const MojUInt32 PageSize = 200;
    static constexpr const char* Kinds[] = {
        "com.palm.media.audio.album:1", "com.palm.media.audio.genre:1",
        "com.palm.media.audio.artist:1", "com.palm.media.image.album:1"
    };

    MojDbClient::Signal::Slot<RecountCollectionsCommand> find_slot;

    MojErr FindResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        FailOnError(err);

        for (MojObject::ConstArrayIterator it = results.arrayBegin(); it != results.arrayEnd(); ++it) {
            MojObject id, name, artist;
            if (!it->get("_id", id))
                continue;
            it->get("name", name);
            it->get("artist", artist);
            if (kind == 0)
                database->enqueue(new CountAlbumAudiosCommand(database, id, name, artist), false);
            else if (kind == 1)
                database->enqueue(new CountGenreAudiosCommand(database, id, name), false);
            else if (kind == 2)
                database->enqueue(new CountArtistAudiosCommand(database, id, name), false);
            else
                database->enqueue(new CountAlbumImagesCommand(database, id), false);
        }

        // The same kind from where this page ended, or the next kind
        MojObject next;
        RecountCollectionsCommand *more = nullptr;
        if (response.get("next", next) && next.type() == MojObject::TypeString) {
            more = new RecountCollectionsCommand(database, kind);
            err = more->page.fromObject(next);
            more->havePage = err == MojErrNone;
        } else if (kind + 1 < sizeof(Kinds) / sizeof(Kinds[0])) {
            more = new RecountCollectionsCommand(database, kind + 1);
        }
        if (more)
            database->enqueueFront(more);

        database->finish();
        return MojErrNone;
    }

private:
    unsigned kind;
    bool havePage = false;
    MojDbQuery::Page page;
};

class RemoveCommand : public BaseCommand
{
public:
    RemoveCommand(MojoMediaDatabase *database, std::string filename) :
        BaseCommand("Remove", database),
        query_removal_slot(this, &RemoveCommand::QueryForRemovalResponse),
        remove_slot(this, &RemoveCommand::RemoveResponse),
        filename(filename)
    {
    }

    BaseCommand *again() const
    {
        if (retried)
            return nullptr;
        RemoveCommand *command = new RemoveCommand(database, filename);
        command->retried = true;
        return command;
    }

    void execute()
    {
        MojDbQuery query;
        query.select("_id");
        selectCollections(query);

        // We're querying for the base type of all stored files here to avoid
        // querying each type separately
        query.from("com.palm.media.file:1");

        MojString filenameStr;
        filenameStr.assign(filename.c_str());
        MojObject filenameObj(filenameStr);
        query.where("path", MojDbQuery::OpEq, filenameObj);

        MojErr err = database->databaseClient().find(query_removal_slot, query);
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<RemoveCommand> query_removal_slot;
    MojDbClient::Signal::Slot<RemoveCommand> remove_slot;

    MojErr QueryForRemovalResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        FailOnError(err);

        if (results.size() == 0) {
            database->finish();
            return MojErrNone;
        }

        MojObject::ObjectVec removeIds;

        for (int n = 0; n < results.size(); n++) {
            MojObject item;
            if(!results.at(n, item))
                continue;

            MojObject idToRemove;
            err = item.getRequired("_id", idToRemove);
            FailOnError(err);

            err = removeIds.push(idToRemove);
            FailOnError(err);

            err = removedEntries.push(item);
            FailOnError(err);
        }

        err = database->databaseClient().del(remove_slot, removeIds.begin(), removeIds.end(), MojDbFlagPurge);
        FailOnError(err);

        return MojErrNone;
    }

    MojErr RemoveResponse(MojObject &response, MojErr err)
    {
        FailOnResponseError(response, err);

        // Totals of the albums, genres and artists it was in
        for (const MojObject &entry : removedEntries)
            forgetEntry(database, entry);

        database->finish();

        return MojErrNone;
    }

private:
    std::string filename;
    MojObject::ObjectVec removedEntries;
    bool retried = false;
};

class RemoveAllCommand : public BaseCommand
{
public:
    RemoveAllCommand(MojoMediaDatabase *database) :
        BaseCommand("RemoveAll", database),
        remove_slot(this, &RemoveAllCommand::RemoveResponse)
    {
    }

    void execute()
    {
        // By query: db8 deletes every match, where a find answers with at
        // most 500 of them
        MojDbQuery query;

        // The base type of all stored files, to avoid going through each
        // type separately
        query.from("com.palm.media.types:1");

        MojErr err = database->databaseClient().del(remove_slot, query, MojDbFlagPurge);
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<RemoveAllCommand> remove_slot;

    MojErr RemoveResponse(MojObject &, MojErr responseErr)
    {
        IgnoreIfAbandoned();

        // Not an error on a first start, when the kinds are not there yet
        if (responseErr != MojErrNone)
            g_message("RemoveAll: nothing removed (err %d)", (int) responseErr);

        database->finish();

        return MojErrNone;
    }
};

class PutKindCommand :public BaseCommand
{
public:
    PutKindCommand(MojoMediaDatabase *database, const char* kindName) :
        BaseCommand("PutKind", database),
        kindName(kindName),
        put_slot(this, &PutKindCommand::PutResponse),
        put_permissions_slot(this, &PutKindCommand::PutPermissionsResponse)
    {
    }

    void execute()
    {
        gchar *kindContent = 0;
        gsize kindContentLength = 0;
        std::string kindPath = DB_KIND_DIRECTORY;
        kindPath += kindName;

        if (!g_file_test(kindPath.c_str(), G_FILE_TEST_IS_REGULAR) ||
            !g_file_get_contents(kindPath.c_str(), &kindContent, &kindContentLength, NULL) ||
            kindContent == 0 ||
            kindContentLength == 0) {

            g_free(kindContent);
            database->finish();
            return;
        }

        MojObject kindObj;
        MojErr parseErr = kindObj.fromJson(kindContent, kindContentLength);
        g_free(kindContent);
        if (parseErr) {
            database->fail("could not read " + kindPath);
            return;
        }

        MojErr err = database->databaseClient().putKind(put_slot, kindObj);
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<PutKindCommand> put_slot;
    MojDbClient::Signal::Slot<PutKindCommand> put_permissions_slot;

    MojErr PutResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        gchar *permissionsContent = 0;
        gsize permissionsContentLength = 0;

        std::string permissionsPath = DB_PERMISSION_DIRECTORY;
        permissionsPath += kindName;

        if (!g_file_test(permissionsPath.c_str(), G_FILE_TEST_IS_REGULAR) ||
            !g_file_get_contents(permissionsPath.c_str(), &permissionsContent, &permissionsContentLength, NULL) ||
            permissionsContent == 0 ||
            permissionsContentLength == 0) {

            g_free(permissionsContent);
            database->finish();
            return MojErrNone;
        }

        MojObject permissions;
        MojErr parseErr = permissions.fromJson(permissionsContent, permissionsContentLength);
        g_free(permissionsContent);
        if (parseErr) {
            database->fail("could not read " + permissionsPath);
            return MojErrNone;
        }

        // The file holds an array of them
        if (permissions.type() != MojObject::TypeArray) {
            database->fail(permissionsPath + " holds no array of permissions");
            return MojErrNone;
        }
        MojObject::ObjectVec permissionsVec;
        for (MojObject::ConstArrayIterator it = permissions.arrayBegin(); it != permissions.arrayEnd(); ++it) {
            MojErr err = permissionsVec.push(*it);
            FailOnError(err);
        }

        MojErr err = database->databaseClient().putPermissions(put_permissions_slot, permissionsVec.begin(), permissionsVec.end());
        FailOnError(err);

        return MojErrNone;
    }

    MojErr PutPermissionsResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        database->finish();

        return MojErrNone;
    }

private:
    std::string kindName;
};

class RemoveKindCommand : public BaseCommand
{
public:
    RemoveKindCommand(MojoMediaDatabase *database, const char* kindName) :
        BaseCommand("RemoveKind", database),
        kindName(kindName),
        remove_slot(this, &RemoveKindCommand::RemoveResponse)
    {
    }

    void execute()
    {
        std::string kindWithVersion = kindName;
        kindWithVersion += ":1";
        MojErr err = database->databaseClient().delKind(remove_slot, kindWithVersion.c_str());
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<RemoveKindCommand> remove_slot;

    MojErr RemoveResponse(MojObject &response, MojErr responseErr)
    {
        IgnoreIfAbandoned();

        database->finish();

        return MojErrNone;
    }

private:
    std::string kindName;
};

MojoMediaDatabase::MojoMediaDatabase(MojDbClient& dbclient) :
    dbclient(dbclient),
    currentCommand(0),
    previousCommand(0),
    restart_timeout(0),
    command_timeout(0)
{
}

MojoMediaDatabase::~MojoMediaDatabase()
{
    resetQueue();
    if (previousCommand)
        delete previousCommand;
    if (currentCommand)
        delete currentCommand;
}

MojDbClient& MojoMediaDatabase::databaseClient() const
{
    return dbclient;
}

gboolean MojoMediaDatabase::checkQueue(gpointer user_data)
{
    MojoMediaDatabase *database = static_cast<MojoMediaDatabase*>(user_data);
    database->checkRestarting();
    return FALSE;
}

void MojoMediaDatabase::finish()
{
    if (command_timeout) {
        g_source_remove(command_timeout);
        command_timeout = 0;
    }

    if (previousCommand)
        previousCommand->release();
    previousCommand = currentCommand;
    currentCommand = 0;

    // Don't directly restart to break through the cycle
    g_timeout_add(0, &MojoMediaDatabase::checkQueue, this);
}

void MojoMediaDatabase::enqueue(BaseCommand *command, bool restart)
{
    command->retain();
    commandQueue.push_back(command);
    if (restart)
        checkRestarting();
}

gboolean MojoMediaDatabase::restartQueue(gpointer user_data)
{
    MojoMediaDatabase *database = static_cast<MojoMediaDatabase*>(user_data);
    database->executeNextCommand();
    return FALSE;
}

void MojoMediaDatabase::checkRestarting()
{
    if (!currentCommand && commandQueue.size() > 0 && restart_timeout == 0)
        restart_timeout = g_timeout_add(0, &MojoMediaDatabase::restartQueue, this);
}

void MojoMediaDatabase::executeNextCommand()
{
    if (previousCommand) {
        previousCommand->release();
        previousCommand = 0;
    }

    if (commandQueue.size() == 0 || currentCommand) {
        restart_timeout = 0;
        return;
    }

    currentCommand = commandQueue.front();
    commandQueue.pop_front();

    restart_timeout = 0;

    try {
        currentCommand->execute();
    }
    catch(std::runtime_error& err) {
        // Without a request sent, no reply will come to finish it
        fail(std::string(currentCommand->name()) + ": " + err.what());
    }

    // A request whose reply never comes (db8 restarted while it was out)
    // would hold up every command after it
    if (currentCommand && !command_timeout)
        command_timeout = g_timeout_add_seconds(CommandTimeoutSeconds, &MojoMediaDatabase::commandTimedOut, this);
}

gboolean MojoMediaDatabase::commandTimedOut(gpointer user_data)
{
    MojoMediaDatabase *database = static_cast<MojoMediaDatabase*>(user_data);
    database->command_timeout = 0;

    BaseCommand *command = database->currentCommand;
    if (!command)
        return FALSE;

    BaseCommand *retry = command->again();
    g_warning("%s: no reply from db8 in %d s; going on%s", command->name().c_str(), CommandTimeoutSeconds,
              retry ? ", and trying it again later" : "");
    command->abandon();
    database->finish();
    if (retry)
        database->enqueue(retry);
    return FALSE;
}

std::string MojoMediaDatabase::currentCommandName() const
{
    return currentCommand ? currentCommand->name() : "";
}

void MojoMediaDatabase::fail(const std::string &message)
{
    // An insert or removal is tried once more, from the back of the queue:
    // a reply fails when db8 restarts ("Message not processed"), and by then
    // it is back
    BaseCommand *retry = currentCommand ? currentCommand->again() : nullptr;
    g_warning("%s: %s%s", currentCommand ? currentCommand->name().c_str() : "(no command)", message.c_str(),
              retry ? "; trying it again later" : "");
    finish();
    if (retry)
        enqueue(retry);
}

void MojoMediaDatabase::enqueueFront(BaseCommand *command)
{
    command->retain();
    commandQueue.push_front(command);
}

void MojoMediaDatabase::reconcile(std::function<bool(const std::string&)> stillIndexed)
{
    enqueue(new ReconcileCommand(this, stillIndexed));
}

void MojoMediaDatabase::recountCollections()
{
    enqueue(new RecountCollectionsCommand(this));
}

void MojoMediaDatabase::insert(const MediaFile &file, std::function<void()> stored)
{
    enqueue(new InsertCommand(this, file, stored));
}

void MojoMediaDatabase::remove(const std::string &filename)
{
    enqueue(new RemoveCommand(this, filename));
}

void MojoMediaDatabase::resetQueue()
{
    while(commandQueue.size() > 0) {
        BaseCommand *command = commandQueue.front();
        commandQueue.pop_front();
        delete command;
    }
}

void MojoMediaDatabase::prepareForRebuild(bool withSchemaRebuild)
{
    resetQueue();
    enqueue(new RemoveAllCommand(this));

    if (withSchemaRebuild) {
        /* Remove all kinds and register them again */
        enqueue(new RemoveKindCommand(this, "com.palm.media.audio.file"));
        enqueue(new RemoveKindCommand(this, "com.palm.media.misc.file"));
        enqueue(new RemoveKindCommand(this, "com.palm.media.video.file"));
        enqueue(new RemoveKindCommand(this, "com.palm.media.playlist.file"));
        enqueue(new RemoveKindCommand(this, "com.palm.media.image.file"));
        enqueue(new RemoveKindCommand(this, "com.palm.media.audio.album"));
        enqueue(new RemoveKindCommand(this, "com.palm.media.audio.genre"));
        enqueue(new RemoveKindCommand(this, "com.palm.media.audio.artist"));
        enqueue(new RemoveKindCommand(this, "com.palm.media.image.album"));
        enqueue(new RemoveKindCommand(this, "com.palm.media.file"));
        enqueue(new RemoveKindCommand(this, "com.palm.media.types"));

        updateKinds();
    }
}

void MojoMediaDatabase::updateKinds()
{
    // The base kinds first: the others extend them
    static const char* const kinds[] = {
        "com.palm.media.types", "com.palm.media.file", "com.palm.media.audio.file",
        "com.palm.media.misc.file", "com.palm.media.video.file", "com.palm.media.playlist.file",
        "com.palm.media.image.file", "com.palm.media.audio.album", "com.palm.media.audio.genre",
        "com.palm.media.audio.artist", "com.palm.media.image.album"
    };
    for (const char* kind : kinds)
        enqueue(new PutKindCommand(this, kind));
}

} // namespace mediascanner
