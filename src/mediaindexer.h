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

#ifndef MEDIAINDEXER_H
#define MEDIAINDEXER_H

#include <QCoreApplication>
#include <QList>

#include "basesource.h"
#include "filesystemsource.h"
#include "mediaindexdatabase.h"

class MediaIndexer : public QCoreApplication
{
    Q_OBJECT

public:
    MediaIndexer(int &argc, char **argv);

    void removeFilesInDirectory(const QString &path);
    void processFile(const QString &path);

private:
    QList<BaseSource*> mSources;
    MediaIndexDatabase mDatabase;
};

#endif // MEDIAINDEXER_H
