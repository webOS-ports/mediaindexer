class CountAlbumAudiosCommand : public BaseCommand
{
public:
    CountAlbumAudiosCommand(MojoMediaDatabase *database, const MojObject& albumIdToMerge, const MojObject& albumName,
                            const MojObject& albumArtist) :
        BaseCommand("CountAlbumAudiosCommand", database),
        query_audios_slot(this, &CountAlbumAudiosCommand::QueryAudiosResponse),
        album_update_slot(this, &CountAlbumAudiosCommand::AlbumUpdateResponse),
        albumIdToMerge(albumIdToMerge),
        albumName(albumName),
        albumArtist(albumArtist)
    {
    }

    void execute()
    {
        MojDbQuery query;
        query.select("_id");
        query.from("com.palm.media.audio.file:1");
        query.where("isRingtone", MojDbQuery::OpEq, MojObject(false));
        query.where("album", MojDbQuery::OpEq, albumName, MojDbCollationPrimary);
        query.where("albumArtist", MojDbQuery::OpEq, albumArtist);

        MojErr err = findCount(database, query_audios_slot, query);
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<CountAlbumAudiosCommand> query_audios_slot;
    MojDbClient::Signal::Slot<CountAlbumAudiosCommand> album_update_slot;

    MojErr QueryAudiosResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        MojInt64 tracks = 0;
        MojErr err = response.getRequired("count", tracks);
        FailOnError(err);

        if (tracks == 0) {
            err = deleteEntry(database, album_update_slot, albumIdToMerge);
            FailOnError(err);
            return MojErrNone;
        }

        MojObject totalObj;
        totalObj.putInt("tracks", tracks);

        err = mergeTotal(database, album_update_slot, albumIdToMerge, totalObj);
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
    MojObject albumIdToMerge;
    MojObject albumName;
    MojObject albumArtist;
};

class AddAlbumForAudioCommand : public BaseCommand
{
public:
    AddAlbumForAudioCommand(MojoMediaDatabase *database, MediaFile file, const MojObject& idToUpdate) :
        BaseCommand("AddAlbumForAudioCommand", database),
        query_album_slot(this, &AddAlbumForAudioCommand::QueryForAlbumResponse),
        insert_album_slot(this, &AddAlbumForAudioCommand::InsertAlbumResponse),
        file(file),
        idToUpdate(idToUpdate)
    {
    }

    void execute()
    {
        MojDbQuery query;
        query.select("_id");
        query.from("com.palm.media.audio.album:1");

        // An album is its name and its album artist: "Greatest Hits" by two
        // artists is two albums
        MojString albumStr;
        albumStr.assign(file.album().c_str());
        albumName = MojObject(albumStr);
        query.where("name", MojDbQuery::OpEq, albumName, MojDbCollationPrimary);

        MojString albumArtistStr;
        albumArtistStr.assign(file.albumArtist().c_str());
        albumArtist = MojObject(albumArtistStr);
        query.where("artist", MojDbQuery::OpEq, albumArtist);

        MojErr err = database->databaseClient().find(query_album_slot, query);
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<AddAlbumForAudioCommand> query_album_slot;
    MojDbClient::Signal::Slot<AddAlbumForAudioCommand> insert_album_slot;

    MojErr QueryForAlbumResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        FailOnError(err);

        // if we already have this album we can use it
        if (results.size() > 0)
            return updateAudio(response);

        MojObject::ObjectVec objectsToInsert;

        MojObject albumObj;
        albumObj.putString("_kind", "com.palm.media.audio.album:1");
        albumObj.putString("artist", file.albumArtist().c_str());
        albumObj.putString("genre", file.genre().c_str());
        albumObj.putBool("hasResizedThumbnails", false);
        albumObj.putString("name", file.album().c_str());
        albumObj.putBool("serviced", false);

        MojObject totalObj;
        totalObj.putInt("tracks", 0);
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

        return updateAudio(response);
    }

    MojErr updateAudio(MojObject &response)
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

        database->enqueue(new CountAlbumAudiosCommand(database, albumId, albumName, albumArtist), false);

        // FIXME: generate thumbnails
        database->enqueue(new GenerateAlbumThumbnailsCommand(database, albumId), false);

        database->finish();

        return MojErrNone;
    }

private:
    MediaFile file;
    MojObject idToUpdate;
    MojObject albumName;
    MojObject albumArtist;
};

/*
 * Counts the tracks and the albums of a genre or an artist and stores them in
 * its entry. One request at a time: with both sent at once, two failing
 * replies finished the command twice, the second time ending whichever
 * command ran next.
 */
class CountCollectionAudiosCommand : public BaseCommand
{
public:
    CountCollectionAudiosCommand(MojoMediaDatabase *database, const char *name, const char *property,
                                 const MojObject& idToMerge, const MojObject& value) :
        BaseCommand(name, database),
        query_audios_file_slot(this, &CountCollectionAudiosCommand::QueryAudiosFileResponse),
        query_audios_album_slot(this, &CountCollectionAudiosCommand::QueryAudiosAlbumResponse),
        update_slot(this, &CountCollectionAudiosCommand::UpdateResponse),
        property(property),
        idToMerge(idToMerge),
        value(value),
        total_tracks(0)
    {
    }

    void execute()
    {
        MojDbQuery query;
        query.select("_id");
        query.from("com.palm.media.audio.file:1");
        query.where("isRingtone", MojDbQuery::OpEq, MojObject(false));
        // As the entry was found by: "rock" and "Rock" are one genre
        query.where(property, MojDbQuery::OpEq, value, MojDbCollationPrimary);

        MojErr err = findCount(database, query_audios_file_slot, query);
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<CountCollectionAudiosCommand> query_audios_file_slot;
    MojDbClient::Signal::Slot<CountCollectionAudiosCommand> query_audios_album_slot;
    MojDbClient::Signal::Slot<CountCollectionAudiosCommand> update_slot;

    MojErr QueryAudiosFileResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        MojErr err = response.getRequired("count", total_tracks);
        FailOnError(err);

        if (total_tracks == 0) {
            err = deleteEntry(database, update_slot, idToMerge);
            FailOnError(err);
            return MojErrNone;
        }

        MojDbQuery query_album;
        query_album.select("_id");
        query_album.from("com.palm.media.audio.album:1");
        query_album.where(property, MojDbQuery::OpEq, value, MojDbCollationPrimary);

        err = findCount(database, query_audios_album_slot, query_album);
        FailOnError(err);

        return MojErrNone;
    }

    MojErr QueryAudiosAlbumResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        MojInt64 total_albums = 0;
        MojErr err = response.getRequired("count", total_albums);
        FailOnError(err);

        MojObject totalObj;
        totalObj.putInt("tracks", total_tracks);
        totalObj.putInt("albums", total_albums);

        err = mergeTotal(database, update_slot, idToMerge, totalObj);
        FailOnError(err);

        return MojErrNone;
    }

    MojErr UpdateResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        database->finish();

        return MojErrNone;
    }

private:
    const char *property;
    MojObject idToMerge;
    MojObject value;
    MojInt64 total_tracks;
};

class CountGenreAudiosCommand : public CountCollectionAudiosCommand
{
public:
    CountGenreAudiosCommand(MojoMediaDatabase *database, const MojObject& genreIdToMerge, const MojObject& genreName) :
        CountCollectionAudiosCommand(database, "CountGenreAudiosCommand", "genre", genreIdToMerge, genreName)
    {
    }
};

class AddGenreForAudioCommand : public BaseCommand
{
public:
    AddGenreForAudioCommand(MojoMediaDatabase *database, MediaFile file, const MojObject& idToUpdate) :
        BaseCommand("AddGenreForAudioCommand", database),
        query_genre_slot(this, &AddGenreForAudioCommand::QueryForGenreResponse),
        insert_genre_slot(this, &AddGenreForAudioCommand::InsertGenreResponse),
        file(file),
        idToUpdate(idToUpdate)
    {
    }

    void execute()
    {
        MojDbQuery query;
        query.select("_id");
        query.from("com.palm.media.audio.genre:1");

        MojString genreStr;
        genreStr.assign(file.genre().c_str());
        genreName = MojObject(genreStr);
        query.where("name", MojDbQuery::OpEq, genreName, MojDbCollationPrimary);

        MojErr err = database->databaseClient().find(query_genre_slot, query);
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<AddGenreForAudioCommand> query_genre_slot;
    MojDbClient::Signal::Slot<AddGenreForAudioCommand> insert_genre_slot;

    MojErr QueryForGenreResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        FailOnError(err);

        // if we already have a genre with the same path we can use it
        if (results.size() > 0)
            return updateAudio(response);

        MojObject::ObjectVec objectsToInsert;

        MojObject genreObj;
        genreObj.putString("_kind", "com.palm.media.audio.genre:1");
        genreObj.putBool("hasResizedThumbnails", false);
        genreObj.putString("name", file.genre().c_str());
        genreObj.putBool("serviced", false);

        MojObject totalObj;
        totalObj.putInt("tracks", 0);
        totalObj.putInt("albums", 0);
        genreObj.put("total", totalObj);

        // we will enqueue a command once the genre is created to generate the
        // thumbnails
        MojObject thumbnails(MojObject::TypeArray);
        genreObj.put("thumbnails", thumbnails);

        objectsToInsert.push(genreObj);

        err = database->databaseClient().put(insert_genre_slot, objectsToInsert.begin(), objectsToInsert.end());
        FailOnError(err);

        return MojErrNone;
    }

    MojErr InsertGenreResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        return updateAudio(response);
    }

    MojErr updateAudio(MojObject &response)
    {
        MojObject results;
        MojErr err = response.getRequired("results", results);
        FailOnError(err);

        MojObject item;
        if(!results.at(0, item)) {
            // FIXME failed to create genre. what now?
            database->finish();
            return MojErrNone;
        }

        MojObject genreId;
        err = item.getRequired("id", genreId);
        if (err != MojErrNone) {
            err = item.getRequired("_id", genreId);
            FailOnError(err);
        }

        database->enqueue(new CountGenreAudiosCommand(database, genreId, genreName), false);

        // FIXME: generate thumbnails
        database->enqueue(new GenerateAlbumThumbnailsCommand(database, genreId), false);

        database->finish();

        return MojErrNone;
    }

private:
    MediaFile file;
    MojObject idToUpdate;
    MojObject genreName;
};

class CountArtistAudiosCommand : public CountCollectionAudiosCommand
{
public:
    CountArtistAudiosCommand(MojoMediaDatabase *database, const MojObject& artistIdToMerge, const MojObject& artistName) :
        CountCollectionAudiosCommand(database, "CountArtistAudiosCommand", "artist", artistIdToMerge, artistName)
    {
    }
};

class AddArtistForAudioCommand : public BaseCommand
{
public:
    AddArtistForAudioCommand(MojoMediaDatabase *database, MediaFile file, const MojObject& idToUpdate) :
        BaseCommand("AddArtistForAudioCommand", database),
        query_artist_slot(this, &AddArtistForAudioCommand::QueryForArtistResponse),
        insert_artist_slot(this, &AddArtistForAudioCommand::InsertArtistResponse),
        file(file),
        idToUpdate(idToUpdate)
    {
    }

    void execute()
    {
        MojDbQuery query;
        query.select("_id");
        query.from("com.palm.media.audio.artist:1");

        MojString artistStr;
        artistStr.assign(file.artist().c_str());
        artistName = MojObject(artistStr);
        query.where("name", MojDbQuery::OpEq, artistName, MojDbCollationPrimary);

        MojErr err = database->databaseClient().find(query_artist_slot, query);
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<AddArtistForAudioCommand> query_artist_slot;
    MojDbClient::Signal::Slot<AddArtistForAudioCommand> insert_artist_slot;

    MojErr QueryForArtistResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        FailOnError(err);

        // if we already have an artist with the same path we can use it
        if (results.size() > 0)
            return updateAudio(response);

        MojObject::ObjectVec objectsToInsert;

        MojObject artistObj;
        artistObj.putString("_kind", "com.palm.media.audio.artist:1");
        artistObj.putBool("hasResizedThumbnails", false);
        artistObj.putString("name", file.artist().c_str());
        artistObj.putBool("serviced", false);

        MojObject totalObj;
        totalObj.putInt("tracks", 0);
        totalObj.putInt("albums", 0);
        artistObj.put("total", totalObj);

        // we will enqueue a command once the artist is created to generate the
        // thumbnails
        MojObject thumbnails(MojObject::TypeArray);
        artistObj.put("thumbnails", thumbnails);

        objectsToInsert.push(artistObj);

        err = database->databaseClient().put(insert_artist_slot, objectsToInsert.begin(), objectsToInsert.end());
        FailOnError(err);

        return MojErrNone;
    }

    MojErr InsertArtistResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        return updateAudio(response);
    }

    MojErr updateAudio(MojObject &response)
    {
        MojObject results;
        MojErr err = response.getRequired("results", results);
        FailOnError(err);

        MojObject item;
        if(!results.at(0, item)) {
            // FIXME failed to create artist. what now?
            database->finish();
            return MojErrNone;
        }

        MojObject artistId;
        err = item.getRequired("id", artistId);
        if (err != MojErrNone) {
            err = item.getRequired("_id", artistId);
            FailOnError(err);
        }

        database->enqueue(new CountArtistAudiosCommand(database, artistId, artistName), false);

        // FIXME: generate thumbnails
        database->enqueue(new GenerateAlbumThumbnailsCommand(database, artistId), false);

        database->finish();

        return MojErrNone;
    }

private:
    MediaFile file;
    MojObject idToUpdate;
    MojObject artistName;
};

/*
 * Counts an album, genre or artist again after a file left it: removed, or
 * tagged differently. Nothing to do when its entry is not there.
 */
class RecountAudioCollectionCommand : public BaseCommand
{
public:
    enum Collection { Album, Genre, Artist };

    RecountAudioCollectionCommand(MojoMediaDatabase *database, Collection collection, const MojObject& name,
                                  const MojObject& albumArtist) :
        BaseCommand("RecountAudioCollectionCommand", database),
        query_slot(this, &RecountAudioCollectionCommand::QueryResponse),
        collection(collection),
        name(name),
        albumArtist(albumArtist)
    {
    }

    void execute()
    {
        static const char* const kinds[] = {
            "com.palm.media.audio.album:1", "com.palm.media.audio.genre:1", "com.palm.media.audio.artist:1"
        };

        MojDbQuery query;
        query.select("_id");
        query.from(kinds[collection]);
        query.where("name", MojDbQuery::OpEq, name, MojDbCollationPrimary);
        if (collection == Album)
            query.where("artist", MojDbQuery::OpEq, albumArtist);

        MojErr err = database->databaseClient().find(query_slot, query);
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<RecountAudioCollectionCommand> query_slot;

    MojErr QueryResponse(MojObject &response, MojErr responseErr)
    {
        FailOnResponseError(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        FailOnError(err);

        MojObject item;
        if (results.at(0, item)) {
            MojObject id;
            err = item.getRequired("_id", id);
            FailOnError(err);

            if (collection == Album)
                database->enqueue(new CountAlbumAudiosCommand(database, id, name, albumArtist), false);
            else if (collection == Genre)
                database->enqueue(new CountGenreAudiosCommand(database, id, name), false);
            else
                database->enqueue(new CountArtistAudiosCommand(database, id, name), false);
        }

        database->finish();

        return MojErrNone;
    }

private:
    Collection collection;
    MojObject name;
    MojObject albumArtist;
};
