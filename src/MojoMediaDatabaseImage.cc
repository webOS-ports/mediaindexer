// Counts the pictures and the videos in a folder album: the file picker
// lists an album for its pictures when total.images is above 0, and for its
// videos when total.videos is. An album with neither goes.
class CountAlbumMediaCommand : public BaseCommand
{
public:
    CountAlbumMediaCommand(MojoMediaDatabase *database, const MojObject& albumId) :
        BaseCommand("CountAlbumMediaCommand", database),
        query_images_slot(this, &CountAlbumMediaCommand::QueryImagesResponse),
        query_videos_slot(this, &CountAlbumMediaCommand::QueryVideosResponse),
        album_update_slot(this, &CountAlbumMediaCommand::AlbumUpdateResponse),
        albumId(albumId)
    {
    }

    void execute()
    {
        MojErr err = findCount(database, query_images_slot, query("com.palm.media.image.file:1"));
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<CountAlbumMediaCommand> query_images_slot;
    MojDbClient::Signal::Slot<CountAlbumMediaCommand> query_videos_slot;
    MojDbClient::Signal::Slot<CountAlbumMediaCommand> album_update_slot;

    MojDbQuery query(const char *kind) const
    {
        MojDbQuery query;
        query.select("_id");
        query.from(kind);
        query.where("albumId", MojDbQuery::OpEq, albumId);
        return query;
    }

    MojErr QueryImagesResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        MojErr err = response.getRequired("count", images);
        FailOnError(err);

        err = findCount(database, query_videos_slot, query("com.palm.media.video.file:1"));
        FailOnError(err);

        return MojErrNone;
    }

    MojErr QueryVideosResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        MojInt64 videos = 0;
        MojErr err = response.getRequired("count", videos);
        FailOnError(err);

        if (folderalbum::isEmpty(images, videos)) {
            err = deleteEntry(database, album_update_slot, albumId);
            FailOnError(err);
            return MojErrNone;
        }

        MojObject totalObj;
        totalObj.putInt("images", images);
        totalObj.putInt("videos", videos);

        err = mergeTotal(database, album_update_slot, albumId, totalObj);
        FailOnError(err);

        return MojErrNone;
    }

    MojErr AlbumUpdateResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        database->finish();

        return MojErrNone;
    }

private:
    MojObject albumId;
    MojInt64 images = 0;
};

class AddFolderAlbumCommand : public BaseCommand
{
public:
    AddFolderAlbumCommand(MojoMediaDatabase *database, MediaFile file, const MojObject& idToUpdate) :
        BaseCommand("AddFolderAlbumCommand", database),
        query_album_slot(this, &AddFolderAlbumCommand::QueryForAlbumResponse),
        insert_album_slot(this, &AddFolderAlbumCommand::InsertAlbumResponse),
        file_update_slot(this, &AddFolderAlbumCommand::FileUpdateResponse),
        file(file),
        idToUpdate(idToUpdate)
    {
    }

    void execute()
    {
        MojDbQuery query;
        query.select("_id");
        query.from("com.palm.media.image.album:1");

        MojString albumPathStr;
        albumPathStr.assign(file.albumPath().c_str());
        MojObject albumPathObj(albumPathStr);
        query.where("path", MojDbQuery::OpEq, albumPathObj);

        MojErr err = database->databaseClient().find(query_album_slot, query);
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<AddFolderAlbumCommand> query_album_slot;
    MojDbClient::Signal::Slot<AddFolderAlbumCommand> insert_album_slot;
    MojDbClient::Signal::Slot<AddFolderAlbumCommand> file_update_slot;

    MojErr QueryForAlbumResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        FailOnError(err);

        // if we already have an album with the same path we can use it
        if (results.size() > 0)
            return assignFile(response);

        MojObject::ObjectVec objectsToInsert;

        MojObject albumObj;
        albumObj.putString("_kind", "com.palm.media.image.album:1");
        albumObj.putInt("modifiedTime", file.createdTime());
        //FIXME eventually we'd need to have an accountId in here once we have a working photo service
        albumObj.putString("accountId", "");
        albumObj.putString("name", file.album().c_str());
        albumObj.putString("path", file.albumPath().c_str());
        albumObj.putString("searchKey", file.album().c_str());
        //FIXME we'd need to set this value dynamically sometime
        albumObj.putBool("showAlbum", true);
        // FIXME what are the rules to generate a correct sort key? It should be both a numericvalue_searchKey
        albumObj.putString("sortKey", "");
        //FIXME we'd need to set this value dynamically sometime once we have a working photo service
        albumObj.putBool("toBeDeleted", false);

        MojObject totalObj;
        // counted once the picture or video is assigned to it
        totalObj.putInt("images", 0);
        totalObj.putInt("videos", 0);
        albumObj.put("total", totalObj);

        // we will enqueue a command once the album is created to generate the
        // thumbnails
        MojObject thumbnails(MojObject::TypeArray);
        albumObj.put("thumbnails", thumbnails);

        objectsToInsert.push(albumObj);

        err = database->databaseClient().put(insert_album_slot, objectsToInsert.begin(), objectsToInsert.end());
        FailOnError(err);

        return MojErrNone;
    }

    MojErr InsertAlbumResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        return assignFile(response);
    }

    MojErr assignFile(MojObject &response)
    {
        MojObject results;
        MojErr err = response.getRequired("results", results);
        FailOnError(err);

        MojObject item;
        if(!results.at(0, item)) {
            // FIXME failed to create album. what now?
            database->finish();
            return MojErrNone;
        }

        MojObject albumId;
        err = item.getRequired("id", albumId);
        if (err != MojErrNone) {
            err = item.getRequired("_id", albumId);
            FailOnError(err);
        }

        // The file is assigned in this same command: as a command of its
        // own it came after whatever was queued meanwhile, such as the
        // recount after the removal of the album's last other file, which
        // found the album empty and deleted it
        this->albumId = albumId;
        MojObject toMerge;
        toMerge.put("_id", idToUpdate);
        toMerge.put("albumId", albumId);

        MojObject::ObjectVec objects;
        objects.push(toMerge);

        err = database->databaseClient().merge(file_update_slot, objects.begin(), objects.end());
        FailOnError(err);

        return MojErrNone;
    }

    MojErr FileUpdateResponse(MojObject &response, MojErr responseErr)
    {
        IgnoreIfAbandoned();

        // The file's entry went meanwhile (the file was removed): nothing to
        // assign, but the album may be empty now
        if (responseErr == MojErrDbKindNotSpecified) {
            database->enqueue(new CountAlbumMediaCommand(database, albumId), false);
            database->finish();
            return MojErrNone;
        }
        FailOnResponseError(response, responseErr);

        database->enqueue(new CountAlbumMediaCommand(database, albumId), false);
        database->enqueue(new GenerateAlbumThumbnailsCommand(database, albumId), false);

        database->finish();

        return MojErrNone;
    }

private:
    MediaFile file;
    MojObject idToUpdate;
    MojObject albumId;
};

class GenerateImageThumbnailCommand : public BaseCommand
{
public:
    GenerateImageThumbnailCommand(MojoMediaDatabase *database, const MojObject& imageId, const std::string& imagePath) :
        BaseCommand("GenerateImageThumbnailCommand", database),
        update_image_slot(this, &GenerateImageThumbnailCommand::UpdateImageResponse),
        imageId(imageId),
        imagePath(imagePath)
    {
    }

    void execute()
    {
        // Runs long after the file was found: opened without following
        // links, and read only if it is still a regular file
        QImage origImage;
        int imageFd = open(imagePath.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        struct stat st;
        if (imageFd >= 0 && fstat(imageFd, &st) == 0 && S_ISREG(st.st_mode)) {
            QFile imageFile;
            if (imageFile.open(imageFd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
                origImage.load(&imageFile, nullptr);
                imageFile.close();
                imageFd = -1;
            }
        }
        if (imageFd >= 0)
            close(imageFd);
        if (origImage.isNull()) {
            database->fail("could not read " + imagePath);
            return;
        }

        // Only ever smaller
        int width = Settings::LunaSettings()->gridUnit * 8;
        QImage thumbnailImage = origImage.width() > width ?
            origImage.scaledToWidth(width, Qt::SmoothTransformation) : origImage;

        // Named after the image's path, in /media/internal/.thumbnails; PNG
        // where the image has transparency, JPEG otherwise
        const char *format = thumbnailImage.hasAlphaChannel() ? "png" : "jpg";
        gchar *digest = g_compute_checksum_for_string(G_CHECKSUM_MD5, imagePath.c_str(), imagePath.length());
        fileName = std::string(digest) + "." + format;
        g_free(digest);
        std::string thumbnailPath = std::string(THUMBNAIL_DIR) + "/" + fileName;

        // Thumbnails are written as root below /media/internal, where users
        // can write: see writeFileInOwnDirectory()
        bool written = writeFileInOwnDirectory(THUMBNAIL_DIR, fileName, [&thumbnailImage, format](int fd) {
            QFile file;
            if (!file.open(fd, QIODevice::WriteOnly, QFileDevice::DontCloseHandle))
                return false;
            bool saved = thumbnailImage.save(&file, format);
            file.close();
            return saved;
        });
        if (!written) {
            database->fail("could not write " + thumbnailPath + ": " + strerror(errno));
            return;
        }

        MojObject toMerge;
        toMerge.put("_id", imageId);


        MojObject appGridThumbnail;
        appGridThumbnail.putString("path", thumbnailPath.c_str());
        appGridThumbnail.putBool("cached", true);

        MojObject dimensions;
        dimensions.putInt("original-height", origImage.height());
        dimensions.putInt("original-width", origImage.width());
        dimensions.putInt("output-height", thumbnailImage.height());
        dimensions.putInt("output-width", thumbnailImage.width());
        appGridThumbnail.put("dimensions", dimensions);

        toMerge.put("appGridThumbnail", appGridThumbnail);

        MojObject thumbnails(MojObject::TypeArray);
        toMerge.put("thumbnails", thumbnails);

        MojObject::ObjectVec objects;
        objects.push(toMerge);

        MojErr err = database->databaseClient().merge(update_image_slot, objects.begin(), objects.end());
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<GenerateImageThumbnailCommand> update_image_slot;

    MojErr UpdateImageResponse(MojObject &response, MojErr responseErr)
    {
        IgnoreIfAbandoned();

        // The image's entry went (the file was removed) while this waited in
        // the queue: so does its thumbnail
        if (responseErr == MojErrDbKindNotSpecified) {
            int dirFd = openOwnDirectory(THUMBNAIL_DIR);
            if (dirFd >= 0) {
                unlinkat(dirFd, fileName.c_str(), 0);
                close(dirFd);
            }
            database->finish();
            return MojErrNone;
        }

        FailOnResponseError(response, responseErr);
        database->finish();
        return MojErrNone;
    }

private:
    MojObject imageId;
    std::string imagePath;
    std::string fileName;
};
