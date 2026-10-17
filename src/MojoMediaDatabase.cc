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

#include <QImage>

#include <Settings.h>

#include "config.h"
#include "MojoMediaDatabase.hh"
#include "MojoMediaObjectSerializer.hh"
#include "internal/utils.hh"
#include "MediaFile.hh"

#define DB_KIND_DIRECTORY "/etc/palm/db/kinds/"
#define DB_PERMISSION_DIRECTORY "/etc/palm/db/permissions/"

namespace mediascanner
{

void CheckErr(MojErr err, const char* filename, int line)
{
    if(err)
        throw std::runtime_error(string_format("%s: %d: err %d", filename, line, err));
}

void CheckErr(MojObject& response, MojErr err, const char* filename, int line)
{
    if(err) {
        bool hasErrorText = false;
        MojString errorText;
        if(response.get("errorText", errorText, hasErrorText) == MojErrNone && hasErrorText) {
            throw std::runtime_error(string_format("%s: %d: err %d message %s", filename, line, err,
                                                   errorText.data()));
        } else {
            throw std::runtime_error(string_format("%s: %d: err %d", filename, line, errorText.data()));
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

#define FailOnResponseError(response, err) \
    do { \
        if(err) { \
            database->fail(DescribeErr(response, err, __FILE__, __LINE__)); \
            return MojErrNone; \
        } \
    } while(0)

class BaseCommand : public MojSignalHandler
{
public:
    BaseCommand(const std::string& name, MojoMediaDatabase *database) :
        database(database),
        _name(name)
    {
    }

    virtual void execute() = 0;

    std::string name() const { return _name; }

protected:
    MojoMediaDatabase *database;
    std::string _name;
};

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

class InsertCommand : public BaseCommand
{
public:
    InsertCommand(MojoMediaDatabase *database, MediaFile file) :
        BaseCommand("Insert", database),
        file(file),
        find_existing_slot(this, &InsertCommand::FindExistingResponse),
        insert_slot(this, &InsertCommand::InsertResponse),
        update_slot(this, &InsertCommand::UpdateResponse)
    {
    }

    void execute()
    {
        MojDbQuery query;
        query.select("_id");

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

        MojObject existing;
        if (results.size() > 0 && results.at(0, existing)) {
            // A file written again: its size, times and tags change. What
            // apps keep in the entry (a bookmark, a play position, thumbnails)
            // and when it was first found stay as they are.
            static const char* const keptFields[] = {
                "bookmark", "playbackPosition", "lastPlayTime", "thumbnails", "serviced",
                "hasResizedThumbnails", "appCacheCompleted", "appCacheComplete", "createdTime"
            };
            for (const char* field : keptFields) {
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

        if (file.type() == ImageMedia && file.albumPath().size() > 0) {

            database->enqueue(new GenerateImageThumbnailCommand(database, idToUpdate, file.path(), file.extension()), false);
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

        database->finish();

        return MojErrNone;
    }

    MojErr UpdateResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);
        database->finish();
        return MojErrNone;
    }

private:
    MediaFile file;
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
    unsigned long removed;
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

    void execute()
    {
        MojDbQuery query;
        query.select("_id");

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
        }

        err = database->databaseClient().del(remove_slot, removeIds.begin(), removeIds.end(), MojDbFlagPurge);
        FailOnError(err);

        return MojErrNone;
    }

    MojErr RemoveResponse(MojObject &response, MojErr err)
    {
        FailOnResponseError(response, err);

        database->finish();

        return MojErrNone;
    }

private:
    std::string filename;
};

class RemoveAllCommand : public BaseCommand
{
public:
    RemoveAllCommand(MojoMediaDatabase *database) :
        BaseCommand("RemoveAll", database),
        query_removal_slot(this, &RemoveAllCommand::QueryForRemovalResponse),
        remove_slot(this, &RemoveAllCommand::RemoveResponse)
    {
    }

    void execute()
    {
        MojDbQuery query;
        query.select("_id");

        // We're querying for the base type of all stored files here to avoid
        // querying each type separately
        query.from("com.palm.media.types:1");

        MojErr err = database->databaseClient().find(query_removal_slot, query);
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<RemoveAllCommand> query_removal_slot;
    MojDbClient::Signal::Slot<RemoveAllCommand> remove_slot;

    MojErr QueryForRemovalResponse(MojObject &response, MojErr responseErr)
    {
        if (responseErr != MojErrNone) {
            database->finish();
            return MojErrNone;
        }

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
        }

        err = database->databaseClient().del(remove_slot, removeIds.begin(), removeIds.end(), MojDbFlagPurge);
        FailOnError(err);

        return MojErrNone;
    }

    MojErr RemoveResponse(MojObject &response, MojErr err)
    {
        FailOnResponseError(response, err);

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

        MojObject perms;
        perms.put("permissions", permissions);

        MojRefCountedPtr<MojServiceRequest> request;
        database->databaseClient().service()->createRequest(request);

        MojErr err = request->send(put_permissions_slot, "com.palm.db", "putPermissions", perms);
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
        database->finish();

        return MojErrNone;
    }

private:
    std::string kindName;
};

MojoMediaDatabase::MojoMediaDatabase(MojDbServiceClient& dbclient) :
    dbclient(dbclient),
    currentCommand(0),
    previousCommand(0),
    restart_timeout(0)
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

MojDbServiceClient& MojoMediaDatabase::databaseClient() const
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
}

void MojoMediaDatabase::fail(const std::string &message)
{
    g_warning("%s: %s", currentCommand ? currentCommand->name().c_str() : "(no command)", message.c_str());
    finish();
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

void MojoMediaDatabase::insert(const MediaFile &file)
{
    enqueue(new InsertCommand(this, file));
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

        enqueue(new PutKindCommand(this, "com.palm.media.types"));
        enqueue(new PutKindCommand(this, "com.palm.media.audio.file"));
        enqueue(new PutKindCommand(this, "com.palm.media.misc.file"));
        enqueue(new PutKindCommand(this, "com.palm.media.video.file"));
        enqueue(new PutKindCommand(this, "com.palm.media.playlist.file"));
        enqueue(new PutKindCommand(this, "com.palm.media.image.file"));
        enqueue(new PutKindCommand(this, "com.palm.media.audio.album"));
        enqueue(new PutKindCommand(this, "com.palm.media.audio.genre"));
        enqueue(new PutKindCommand(this, "com.palm.media.audio.artist"));
        enqueue(new PutKindCommand(this, "com.palm.media.image.album"));
        enqueue(new PutKindCommand(this, "com.palm.media.file"));
    }
}

} // namespace mediascanner
