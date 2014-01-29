/*
 * Copyright (C) 2014 Simon Busch <morphis@gravedo.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include <QStringList>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>

#include <QSqlQuery>
#include <QSqlError>

#include "mediaindexdatabase.h"

#define DATABASE_NAME       "/var/luna/data/mediaindex.db"

MediaIndexDatabase::MediaIndexDatabase(QObject *parent) :
    QObject(parent)
{
    mDb = QSqlDatabase::addDatabase("QSQLITE");
    mDb.setDatabaseName("/tmp/luna/mediaindex.db");
    mDb.open();

    checkDatabase();
}

void MediaIndexDatabase::checkDatabase()
{
    if (!mDb.tables().contains("files") || !mDb.tables().contains("actions"))
        createSchema();
}

void MediaIndexDatabase::createSchema()
{
    QSqlQuery query;
    query = mDb.exec("DELETE TABLE actions;");
    query = mDb.exec("DELETE TABLE files;");
    query = mDb.exec("CREATE TABLE actions (revision INTEGER PRIMARY KEY AUTOINCREMENT, action_type INTEGER, size INTEGER, modified INTEGER, path TEXT, new_path TEXT);");
    query = mDb.exec("CREATE TABLE files (path TEXT UNIQUE ON CONFLICT REPLACE, size INTEGER, modified INTEGER);");
}

void MediaIndexDatabase::processFile(const QString &path)
{
    QFileInfo fi(path);

    qDebug() << "Processing file" << path;

    QSqlQuery query = mDb.exec(QString("SELECT * FROM files WHERE path = '%1';").arg(path));
    qDebug() << query.size() << query.isActive();
    if (query.size() == 1 && !fi.exists()) {
        qDebug() << path << "was deleted";
        query = mDb.exec(QString("DELETE FROM files WHERE path = '%1';")
                         .arg(path));
    }
    else if (query.size() == 0 && fi.exists()) {
        qDebug() << path << "was added";
        query = mDb.exec(QString("INSERT INTO files (path, size, modified) VALUES ('%1','%2','%3');")
                         .arg(path)
                         .arg(fi.size())
                         // we only want seconds not miliseconds
                         .arg(fi.lastModified().toMSecsSinceEpoch() / 1000));
    }
    else {
        qDebug() << path << "was modified";
        query = mDb.exec(QString("UPDATE files SET size = '%1', modified = '%2' WHERE path = '%3';")
                         .arg(fi.size())
                         // we only want seconds not miliseconds
                         .arg(fi.lastModified().toMSecsSinceEpoch() / 1000)
                         .arg(path));
    }
}

void MediaIndexDatabase::removeFilesInDirectory(const QString &path)
{
    qDebug() << "Removing files in directory" << path;
    QSqlQuery query = mDb.exec(QString("DELETE FROM files WHERE path like '%1%';").arg(path));
}

