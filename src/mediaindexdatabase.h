#ifndef SOURCESTATEDATABASE_H
#define SOURCESTATEDATABASE_H

#include <QObject>
#include <QSqlDatabase>

class MediaIndexDatabase : public QObject
{
    Q_OBJECT
public:
    explicit MediaIndexDatabase(QObject *parent = 0);

    void removeFilesInDirectory(const QString &path);
    void processFile(const QString &path);

private:
    QSqlDatabase mDb;

private:
    void checkDatabase();
    void createSchema();
};

#endif // SOURCESTATEDATABASE_H
