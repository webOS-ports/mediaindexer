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

#include <QDebug>
#include <QDir>
#include <QFileInfo>

#include "filesystemsource.h"
#include "mediaindexer.h"

FilesystemSource::FilesystemSource(const QString &basePath, MediaIndexer *parent) :
    BaseSource("FileSystem", parent),
    mBasePath(basePath),
    mIndexer(parent)
{
    qDebug() << "Starting to watch" << mBasePath;

    connect(&mWatcher, &QFileSystemWatcher::directoryChanged, [=](const QString& path) {
        scanDirectory(path, true);
    });

    scanDirectory(mBasePath, true);
}

void FilesystemSource::scanDirectory(const QString &path, bool recursive)
{
    if (path == "." || path == "..")
        return;

    QDir baseDirectory(path);

    if (!baseDirectory.exists()) {
        qDebug() << "Directory" << path << "was removed";
        mWatcher.removePath(path);
        mIndexer->removeFilesForDirectory(path);
        return;
    }

    qDebug() << "Scanning directory" << path;

    if (!mWatcher.directories().contains(path))
        mWatcher.addPath(path);

    QFileInfoList files = baseDirectory.entryInfoList(QDir::Files | QDir::NoSymLinks);
    foreach(QFileInfo fileInfo, files)
        mIndexer->processFile(fileInfo.absoluteFilePath());

    if (recursive) {
        QFileInfoList childs = baseDirectory.entryInfoList(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        foreach (QFileInfo child, childs)
            scanDirectory(child.absoluteFilePath(), false);
    }
}
