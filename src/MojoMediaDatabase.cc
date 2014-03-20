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
    BaseCommand(MojoMediaDatabase *database) :
        database(database)
    {
    }

    virtual void execute() = 0;

protected:
    MojoMediaDatabase *database;
};

class InsertCommand : public BaseCommand
{
public:
    InsertCommand(MojoMediaDatabase *database, MediaFile file) :
        BaseCommand(database),
        file(file),
        insert_slot(this, &InsertCommand::InsertResponse)
    {
    }

    void execute()
    {
        MojObject::ObjectVec objectsToInsert;
        MojObject mediaObj;
        MojoMediaObjectSerializer::SerializeToDatabaseObject(file, mediaObj);
        objectsToInsert.push(mediaObj);

        MojErr err = database->databaseClient().put(insert_slot, objectsToInsert.begin(), objectsToInsert.end());
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<InsertCommand> insert_slot;

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
        BaseCommand(database),
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

        if (results.size() == 0)
            return MojErrNone;

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

MojoMediaDatabase::MojoMediaDatabase(MojDbClient& dbclient) :
    dbclient(dbclient),
    currentCommand(0),
    previousCommand(0)
{
}

MojDbClient& MojoMediaDatabase::databaseClient() const
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
    commandQueue.push(command);
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
    if (!currentCommand && commandQueue.size() > 0)
        g_timeout_add(0, &MojoMediaDatabase::restartQueue, this);
}

void MojoMediaDatabase::executeNextCommand()
{
    currentCommand = commandQueue.front();
    commandQueue.pop();
    currentCommand->execute();

    if (previousCommand) {
        previousCommand->release();
        previousCommand = 0;
    }
}

void MojoMediaDatabase::insert(const MediaFile &file)
{
    enqueue(new InsertCommand(this, file));
}

void MojoMediaDatabase::remove(const std::string &filename)
{
    enqueue(new RemoveCommand(this, filename));
}

} // namespace mediascanner
