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

class InsertCommand : public BaseCommand
{
public:
    InsertCommand(MojoMediaDatabase *database, MediaFile file) :
        BaseCommand("Insert", database),
        file(file),
        find_existing_slot(this, &InsertCommand::FindExistingResponse),
        insert_slot(this, &InsertCommand::InsertResponse)
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
        query.where("path", MojDbQuery::CompOp::OpEq, filenameObj);

        MojErr err = database->databaseClient().find(find_existing_slot, query);
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<InsertCommand> find_existing_slot;
    MojDbClient::Signal::Slot<InsertCommand> insert_slot;

    MojErr FindExistingResponse(MojObject &response, MojErr responseErr)
    {
        ResponseToException(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        ErrorToException(err);

        if (results.size() > 0) {
            // FIXME update it with new meta data
            database->finish();
            return MojErrNone;
        }

        MojObject::ObjectVec objectsToInsert;
        MojObject mediaObj;
        MojoMediaObjectSerializer::SerializeToDatabaseObject(file, mediaObj);
        objectsToInsert.push(mediaObj);

        err = database->databaseClient().put(insert_slot, objectsToInsert.begin(), objectsToInsert.end());
        ErrorToException(err);

        return MojErrNone;
    }

    MojErr InsertResponse(MojObject &response, MojErr err)
    {
        ResponseToException(response, err);

        database->finish();

        return MojErrNone;
    }

private:
    MediaFile file;
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
        query.where("path", MojDbQuery::CompOp::OpEq, filenameObj);

        MojErr err = database->databaseClient().find(query_removal_slot, query);
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<RemoveCommand> query_removal_slot;
    MojDbClient::Signal::Slot<RemoveCommand> remove_slot;

    MojErr QueryForRemovalResponse(MojObject &response, MojErr responseErr)
    {
        ResponseToException(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        ErrorToException(err);

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
            ErrorToException(err);

            err = removeIds.push(idToRemove);
            ErrorToException(err);
        }

        err = database->databaseClient().del(remove_slot, removeIds.begin(), removeIds.end(), MojDb::FlagPurge);
        ErrorToException(err);

        return MojErrNone;
    }

    MojErr RemoveResponse(MojObject &response, MojErr err)
    {
        ResponseToException(response, err);

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
        query.from("com.palm.media.file:1");

        MojErr err = database->databaseClient().find(query_removal_slot, query);
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<RemoveAllCommand> query_removal_slot;
    MojDbClient::Signal::Slot<RemoveAllCommand> remove_slot;

    MojErr QueryForRemovalResponse(MojObject &response, MojErr responseErr)
    {
        ResponseToException(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        ErrorToException(err);

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
            ErrorToException(err);

            err = removeIds.push(idToRemove);
            ErrorToException(err);
        }

        err = database->databaseClient().del(remove_slot, removeIds.begin(), removeIds.end(), MojDb::FlagPurge);
        ErrorToException(err);

        return MojErrNone;
    }

    MojErr RemoveResponse(MojObject &response, MojErr err)
    {
        ResponseToException(response, err);

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
        char *kindContent = 0;
        unsigned int kindContentLength = 0;
        std::string kindPath = DB_KIND_DIRECTORY;
        kindPath += kindName;

        if (!g_file_test(kindPath.c_str(), G_FILE_TEST_IS_REGULAR) ||
            !g_file_get_contents(kindPath.c_str(), &kindContent, &kindContentLength, NULL) ||
            kindContent == 0 ||
            kindContentLength == 0) {

            database->finish();
            return;
        }

        MojObject kindObj;
        kindObj.fromJson(kindContent, kindContentLength);

        MojErr err = database->databaseClient().putKind(put_slot, kindObj);
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<PutKindCommand> put_slot;
    MojDbClient::Signal::Slot<PutKindCommand> put_permissions_slot;

    MojErr PutResponse(MojObject &response, MojErr responseErr)
    {
        ResponseToException(response, responseErr);

        char *permissionsContent = 0;
        unsigned int permissionsContentLength = 0;

        std::string permissionsPath = DB_PERMISSION_DIRECTORY;
        permissionsPath += kindName;

        if (!g_file_test(permissionsPath.c_str(), G_FILE_TEST_IS_REGULAR) ||
            !g_file_get_contents(permissionsPath.c_str(), &permissionsContent, &permissionsContentLength, NULL) ||
            permissionsContent == 0 ||
            permissionsContentLength == 0) {

            database->finish();
            return MojErrNone;
        }

        MojObject permissions;
        permissions.fromJson(permissionsContent, permissionsContentLength);

        MojObject perms;
        perms.put("permissions", permissions);

        MojRefCountedPtr<MojServiceRequest> request;
        database->databaseClient().service()->createRequest(request);

        MojErr err = request->send(put_permissions_slot, "com.palm.db", "putPermissions", perms);
        ErrorToException(err);

        return MojErrNone;
    }

    MojErr PutPermissionsResponse(MojObject &response, MojErr responseErr)
    {
        ResponseToException(response, responseErr);

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
        ResponseToException(response, responseErr);

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

MojDbServiceClient& MojoMediaDatabase::databaseClient() const
{
    return dbclient;
}

void MojoMediaDatabase::finish()
{
    previousCommand = currentCommand;
    currentCommand = 0;

    checkRestarting();
}

void MojoMediaDatabase::enqueue(BaseCommand *command)
{
    command->retain();
    commandQueue.push_back(command);
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
    currentCommand->execute();

    restart_timeout = 0;
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
