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

#ifndef MOJOMEDIADATABASE_H
#define MOJOMEDIADATABASE_H

#include <db/MojDbServiceClient.h>
#include "db/MojDb.h"
#include <deque>

namespace mediascanner
{

class MediaFile;
class BaseCommand;

class MojoMediaDatabase
{
public:
    MojoMediaDatabase(MojDbServiceClient& dbclient);
    ~MojoMediaDatabase();

    void insert(const mediascanner::MediaFile& file);
    void remove(const std::string& filename);
    void prepareForRebuild(bool withSchemaRebuild);

    MojDbServiceClient& databaseClient() const;

    void finish();

    void enqueue(BaseCommand *command, bool restart = true);

private:
    void checkRestarting();
    void executeNextCommand();
    void resetQueue();

    static gboolean restartQueue(gpointer user_data);
    static gboolean checkQueue(gpointer user_data);

private:
    MojDbServiceClient& dbclient;
    std::deque<BaseCommand*> commandQueue;
    BaseCommand *currentCommand;
    BaseCommand *previousCommand;
    int restart_timeout;

    friend class BaseCommand;
};

} // namespace mediascanner

#endif // MOJOMEDIADATABASE_H
