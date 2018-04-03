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

        MojErr err = database->databaseClient().find(query_images_slot, query);
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<CountAlbumImagesCommand> query_images_slot;
    MojDbClient::Signal::Slot<CountAlbumImagesCommand> album_update_slot;

    MojErr QueryImagesResponse(MojObject &response, MojErr responseErr)
    {
        ResponseToException(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        if (err != MojErrNone) {
            database->finish();
            return MojErrNone;
        }

        MojObject toMerge;
        toMerge.put("_id", albumId);

        MojObject totalObj;
        totalObj.putInt("images", results.size());
        toMerge.put("total", totalObj);

        MojObject::ObjectVec objects;
        objects.push(toMerge);

        err = database->databaseClient().merge(album_update_slot, objects.begin(), objects.end());
        if (err != MojErrNone) {
            database->finish();
            return MojErrNone;
        }

        return MojErrNone;
    }

    MojErr AlbumUpdateResponse(MojObject &response, MojErr responseErr)
    {
        ResponseToException(response, responseErr);

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
        ResponseToException(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        ErrorToException(err);

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
        ErrorToException(err);

        return MojErrNone;
    }

    MojErr InsertAlbumResponse(MojObject &response, MojErr responseErr)
    {
        ResponseToException(response, responseErr);

        return updateImage(response);
    }

    MojErr updateImage(MojObject &response)
    {
        MojObject results;
        MojErr err = response.getRequired("results", results);
        ErrorToException(err);

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
            ErrorToException(err);
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
    GenerateImageThumbnailCommand(MojoMediaDatabase *database, const MojObject& imageId, const std::string& imagePath, const std::string& extension) :
        BaseCommand("GenerateImageThumbnailCommand", database),
        update_image_slot(this, &GenerateImageThumbnailCommand::UpdateImageResponse),
        imageId(imageId),
        imagePath(imagePath),
        extension(extension)
    {
    }

    void execute()
    {
        // path for thumbnails /media/internal/.thumbnails/<some path>

        GChecksum *checksum;
        checksum = g_checksum_new(G_CHECKSUM_MD5);
        g_checksum_update(checksum, (const guchar *) imagePath.c_str(), imagePath.length());

        guint8 digest[16];
        gsize digest_len = sizeof (digest);
        g_checksum_get_digest(checksum, digest, &digest_len);

        char *file = g_strconcat(g_checksum_get_string(checksum), ".", extension.c_str(), NULL);
        char *thumbnailPath = g_build_filename(THUMBNAIL_DIR, file, NULL);
        g_checksum_free(checksum);

        if (!g_file_test(THUMBNAIL_DIR, G_FILE_TEST_IS_DIR))
            g_mkdir_with_parents(THUMBNAIL_DIR, 0755);

        int gridUnit = Settings::LunaSettings()->gridUnit;
        QImage origImage(QString::fromStdString(imagePath));
        QImage thumbnailImage = origImage.scaledToWidth((gridUnit * 8), Qt::SmoothTransformation);
        thumbnailImage.save(QString(thumbnailPath));

        MojObject toMerge;
        toMerge.put("_id", imageId);


        MojObject appGridThumbnail;
        appGridThumbnail.putString("path", thumbnailPath);
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

        g_free(file);
        g_free(thumbnailPath);
    }

protected:
    MojDbClient::Signal::Slot<GenerateImageThumbnailCommand> update_image_slot;

    MojErr UpdateImageResponse(MojObject &response, MojErr responseErr)
    {
        ResponseToException(response, responseErr);
        database->finish();
        return MojErrNone;
    }

private:
    MojObject imageId;
    std::string imagePath;
    std::string extension;
};
