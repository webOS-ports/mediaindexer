class CountAlbumAudiosCommand : public BaseCommand
{
public:
    CountAlbumAudiosCommand(MojoMediaDatabase *database, const MojObject& albumIdToMerge, const MojObject& albumName) :
        BaseCommand("CountAlbumAudiosCommand", database),
        query_audios_slot(this, &CountAlbumAudiosCommand::QueryAudiosResponse),
        album_update_slot(this, &CountAlbumAudiosCommand::AlbumUpdateResponse),
        albumIdToMerge(albumIdToMerge),
        albumName(albumName)
    {
    }

    void execute()
    {
        MojDbQuery query;
        query.select("_id");
        query.from("com.palm.media.audio.file:1");
        query.where("isRingtone", MojDbQuery::OpEq, MojObject(false));
        query.where("album", MojDbQuery::OpEq, albumName, MojDbCollationPrimary);

        MojErr err = database->databaseClient().find(query_audios_slot, query);
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<CountAlbumAudiosCommand> query_audios_slot;
    MojDbClient::Signal::Slot<CountAlbumAudiosCommand> album_update_slot;

    MojErr QueryAudiosResponse(MojObject &response, MojErr responseErr)
    {
        ResponseToException(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        if (err != MojErrNone) {
            database->finish();
            return MojErrNone;
        }

        MojObject toMerge;
        toMerge.put("_id", albumIdToMerge);

        MojObject totalObj;
        totalObj.putInt("tracks", results.size());
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
    MojObject albumIdToMerge;
    MojObject albumName;
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

        MojString albumStr;
        albumStr.assign(file.album().c_str());
        albumName = MojObject(albumStr);
        query.where("name", MojDbQuery::OpEq, albumName, MojDbCollationPrimary);

        MojErr err = database->databaseClient().find(query_album_slot, query);
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<AddAlbumForAudioCommand> query_album_slot;
    MojDbClient::Signal::Slot<AddAlbumForAudioCommand> insert_album_slot;

    MojErr QueryForAlbumResponse(MojObject &response, MojErr responseErr)
    {
        ResponseToException(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        ErrorToException(err);

        // if we already have an album with the same path we can use it
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
        ErrorToException(err);

        return MojErrNone;
    }

    MojErr InsertAlbumResponse(MojObject &response, MojErr responseErr)
    {
        ResponseToException(response, responseErr);

        return updateAudio(response);
    }

    MojErr updateAudio(MojObject &response)
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

        database->enqueue(new CountAlbumAudiosCommand(database, albumId, albumName), false);

        // FIXME: generate thumbnails
        database->enqueue(new GenerateAlbumThumbnailsCommand(database, albumId), false);

        database->finish();

        return MojErrNone;
    }

private:
    MediaFile file;
    MojObject idToUpdate;
    MojObject albumName;
};

class CountGenreAudiosCommand : public BaseCommand
{
public:
    CountGenreAudiosCommand(MojoMediaDatabase *database, const MojObject& genreIdToMerge, const MojObject& genreName) :
        BaseCommand("CountGenreAudiosCommand", database),
        query_audios_file_slot(this, &CountGenreAudiosCommand::QueryAudiosFileResponse),
        query_audios_album_slot(this, &CountGenreAudiosCommand::QueryAudiosAlbumResponse),
        genre_update_slot(this, &CountGenreAudiosCommand::GenreUpdateResponse),
        genreIdToMerge(genreIdToMerge),
        genreName(genreName),
        total_tracks(-1),
        total_albums(-1)
    {
    }

    void execute()
    {
        MojDbQuery query;
        query.select("_id");
        query.from("com.palm.media.audio.file:1");
        query.where("isRingtone", MojDbQuery::OpEq, MojObject(false));
        query.where("genre", MojDbQuery::OpEq, genreName);

        MojErr err = database->databaseClient().find(query_audios_file_slot, query);
        ErrorToException(err);

        MojDbQuery query_album;
        query_album.select("_id");
        query_album.from("com.palm.media.audio.album:1");
        query_album.where("genre", MojDbQuery::OpEq, genreName);

        err = database->databaseClient().find(query_audios_album_slot, query_album);
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<CountGenreAudiosCommand> query_audios_file_slot;
    MojDbClient::Signal::Slot<CountGenreAudiosCommand> query_audios_album_slot;
    MojDbClient::Signal::Slot<CountGenreAudiosCommand> genre_update_slot;

    MojErr QueryAudiosFileResponse(MojObject &response, MojErr responseErr)
    {
        ResponseToException(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        if (err != MojErrNone) {
            database->finish();
            return MojErrNone;
        }

        total_tracks = results.size();
        return updateAudio();
    }

    MojErr QueryAudiosAlbumResponse(MojObject &response, MojErr responseErr)
    {
        ResponseToException(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        if (err != MojErrNone) {
            database->finish();
            return MojErrNone;
        }

        total_albums = results.size();
        return updateAudio();
    }

    MojErr updateAudio()
    {
        if ((total_tracks == -1) || (total_albums == -1))
            return MojErrNone;

        MojObject toMerge;
        toMerge.put("_id", genreIdToMerge);

        MojObject totalObj;
        totalObj.putInt("tracks", total_tracks);
        totalObj.putInt("albums", total_albums);
        toMerge.put("total", totalObj);

        MojObject::ObjectVec objects;
        objects.push(toMerge);

        MojErr err = database->databaseClient().merge(genre_update_slot, objects.begin(), objects.end());
        if (err != MojErrNone) {
            database->finish();
            return MojErrNone;
        }

        return MojErrNone;
    }

    MojErr GenreUpdateResponse(MojObject &response, MojErr responseErr)
    {
        ResponseToException(response, responseErr);

        database->finish();

        return MojErrNone;
    }

private:
    MojObject genreIdToMerge;
    MojObject genreName;
    int total_tracks;
    int total_albums;
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
        ResponseToException(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        ErrorToException(err);

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
        ErrorToException(err);

        return MojErrNone;
    }

    MojErr InsertGenreResponse(MojObject &response, MojErr responseErr)
    {
        ResponseToException(response, responseErr);

        return updateAudio(response);
    }

    MojErr updateAudio(MojObject &response)
    {
        MojObject results;
        MojErr err = response.getRequired("results", results);
        ErrorToException(err);

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
            ErrorToException(err);
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

class CountArtistAudiosCommand : public BaseCommand
{
public:
    CountArtistAudiosCommand(MojoMediaDatabase *database, const MojObject& artistIdToMerge, const MojObject& artistName) :
        BaseCommand("CountArtistAudiosCommand", database),
        query_audios_file_slot(this, &CountArtistAudiosCommand::QueryAudiosFileResponse),
        query_audios_album_slot(this, &CountArtistAudiosCommand::QueryAudiosAlbumResponse),
        artist_update_slot(this, &CountArtistAudiosCommand::ArtistUpdateResponse),
        artistIdToMerge(artistIdToMerge),
        artistName(artistName),
        total_tracks(-1),
        total_albums(-1)
    {
    }

    void execute()
    {
        MojDbQuery query;
        query.select("_id");
        query.from("com.palm.media.audio.file:1");
        query.where("isRingtone", MojDbQuery::OpEq, MojObject(false));
        query.where("artist", MojDbQuery::OpEq, artistName);

        MojErr err = database->databaseClient().find(query_audios_file_slot, query);
        ErrorToException(err);

        MojDbQuery query_album;
        query_album.select("_id");
        query_album.from("com.palm.media.audio.album:1");
        query_album.where("artist", MojDbQuery::OpEq, artistName);

        err = database->databaseClient().find(query_audios_album_slot, query_album);
        ErrorToException(err);
    }

protected:
    MojDbClient::Signal::Slot<CountArtistAudiosCommand> query_audios_file_slot;
    MojDbClient::Signal::Slot<CountArtistAudiosCommand> query_audios_album_slot;
    MojDbClient::Signal::Slot<CountArtistAudiosCommand> artist_update_slot;

    MojErr QueryAudiosFileResponse(MojObject &response, MojErr responseErr)
    {
        ResponseToException(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        if (err != MojErrNone) {
            database->finish();
            return MojErrNone;
        }

        total_tracks = results.size();
        return updateAudio();
    }

    MojErr QueryAudiosAlbumResponse(MojObject &response, MojErr responseErr)
    {
        ResponseToException(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        if (err != MojErrNone) {
            database->finish();
            return MojErrNone;
        }

        total_albums = results.size();
        return updateAudio();
    }

    MojErr updateAudio()
    {
        if ((total_tracks == -1) || (total_albums == -1))
            return MojErrNone;

        MojObject toMerge;
        toMerge.put("_id", artistIdToMerge);

        MojObject totalObj;
        totalObj.putInt("tracks", total_tracks);
        totalObj.putInt("albums", total_albums);
        toMerge.put("total", totalObj);

        MojObject::ObjectVec objects;
        objects.push(toMerge);

        MojErr err = database->databaseClient().merge(artist_update_slot, objects.begin(), objects.end());
        if (err != MojErrNone) {
            database->finish();
            return MojErrNone;
        }

        return MojErrNone;
    }

    MojErr ArtistUpdateResponse(MojObject &response, MojErr responseErr)
    {
        ResponseToException(response, responseErr);

        database->finish();

        return MojErrNone;
    }

private:
    MojObject artistIdToMerge;
    MojObject artistName;
    int total_tracks;
    int total_albums;
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
        ResponseToException(response, responseErr);

        MojObject results;
        MojErr err = response.getRequired("results", results);
        ErrorToException(err);

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
        ErrorToException(err);

        return MojErrNone;
    }

    MojErr InsertArtistResponse(MojObject &response, MojErr responseErr)
    {
        ResponseToException(response, responseErr);

        return updateAudio(response);
    }

    MojErr updateAudio(MojObject &response)
    {
        MojObject results;
        MojErr err = response.getRequired("results", results);
        ErrorToException(err);

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
            ErrorToException(err);
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
