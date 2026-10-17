class CountAlbumImagesCommand : public BaseCommand
{
public:
    CountAlbumImagesCommand(MojoMediaDatabase *database, const MojObject& albumId) :
        BaseCommand("CountAlbumImagesCommand", database),
        query_images_slot(this, &CountAlbumImagesCommand::QueryImagesResponse),
        album_update_slot(this, &CountAlbumImagesCommand::AlbumUpdateResponse),
        albumId(albumId)
    {
    }

    void execute()
    {
        MojDbQuery query;
        query.select("_id");
        query.from("com.palm.media.image.file:1");
        query.where("albumId", MojDbQuery::OpEq, albumId);

        MojErr err = findCount(database, query_images_slot, query);
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<CountAlbumImagesCommand> query_images_slot;
    MojDbClient::Signal::Slot<CountAlbumImagesCommand> album_update_slot;

    MojErr QueryImagesResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        MojInt64 images = 0;
        MojErr err = response.getRequired("count", images);
        FailOnError(err);

        MojObject totalObj;
        totalObj.putInt("images", images);

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
};

class AssignImageToAlbumCommand : public BaseCommand
{
public:
    AssignImageToAlbumCommand(MojoMediaDatabase *database, const MojObject& imageId, const MojObject& albumId) :
        BaseCommand("AssignImageToAlbumCommand", database),
        image_update_slot(this, &AssignImageToAlbumCommand::ImageUpdateResponse),
        imageId(imageId),
        albumId(albumId)
    {
    }

    void execute()
    {
        MojObject toMerge;
        toMerge.put("_id", imageId);
        toMerge.put("albumId", albumId);

        MojObject::ObjectVec objects;
        objects.push(toMerge);

        MojErr err = database->databaseClient().merge(image_update_slot, objects.begin(), objects.end());
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<AssignImageToAlbumCommand> image_update_slot;

    MojErr ImageUpdateResponse(MojObject &response, MojErr responseErr)
    {
        IgnoreIfAbandoned();

        database->enqueue(new CountAlbumImagesCommand(database, albumId), false);

        database->finish();

        return MojErrNone;
    }

private:
    MojObject imageId;
    MojObject albumId;
};

class AddAlbumForImageCommand : public BaseCommand
{
public:
    AddAlbumForImageCommand(MojoMediaDatabase *database, MediaFile file, const MojObject& idToUpdate) :
        BaseCommand("AddAlbumForImageCommand", database),
        query_album_slot(this, &AddAlbumForImageCommand::QueryForAlbumResponse),
        insert_album_slot(this, &AddAlbumForImageCommand::InsertAlbumResponse),
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
    MojDbClient::Signal::Slot<AddAlbumForImageCommand> query_album_slot;
    MojDbClient::Signal::Slot<AddAlbumForImageCommand> insert_album_slot;

    MojErr QueryForAlbumResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        FailOnError(err);

        // if we already have an album with the same path we can use it
        if (results.size() > 0)
            return updateImage(response);

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
        // image count update will be triggered by the AssignImageToAlbumCommand later
        totalObj.putInt("images", 0);
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

        return updateImage(response);
    }

    MojErr updateImage(MojObject &response)
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

        database->enqueue(new AssignImageToAlbumCommand(database, idToUpdate, albumId), false);
        database->enqueue(new GenerateAlbumThumbnailsCommand(database, albumId), false);

        database->finish();

        return MojErrNone;
    }

private:
    MediaFile file;
    MojObject idToUpdate;
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
