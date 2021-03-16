#include "imagemetadata.h"

#include <sqlite3pp/sqlite3pp.h>

namespace ImageMetaData
{

constexpr char sql_get_key_id[] = "SELECT rowid FROM metadata WHERE name=? AND val NOT NULL";

ImageBlobDevice::ImageBlobDevice(sqlite3 *db, qint64 rowId, QObject *parent) :
    QIODevice(parent),
    mRowId(rowId),
    mSize(0),
    mDb(db),
    mBlob(nullptr)
{

}

ImageBlobDevice::~ImageBlobDevice()
{
    close();
}

bool ImageBlobDevice::open(QIODevice::OpenMode mode)
{
    mode |= QIODevice::ReadOnly;
    int rc = sqlite3_blob_open(mDb, "main", "metadata", "val", mRowId, (mode & QIODevice::WriteOnly) != 0, &mBlob);
    if(rc != SQLITE_OK || !mBlob)
    {
        mBlob = nullptr;
        setErrorString(sqlite3_errmsg(mDb));
        return false;
    }

    QIODevice::open(mode);

    mSize = sqlite3_blob_bytes(mBlob);

    return true;
}

void ImageBlobDevice::close()
{
    if(mBlob)
    {
        sqlite3_blob_close(mBlob);
        mBlob = nullptr;
        mSize = 0;
    }

    QIODevice::close();
}

qint64 ImageBlobDevice::size() const
{
    return mSize;
}

qint64 ImageBlobDevice::writeData(const char *data, qint64 len)
{
    if(!mBlob)
        return -1;

    int offset = int(pos());
    if(len + offset >= mSize)
        len = mSize - offset;

    if(!len)
        return -1;

    int rc = sqlite3_blob_write(mBlob, data, int(len), offset);
    if(rc == SQLITE_OK)
        return len;

    if(rc == SQLITE_READONLY)
        return -1;

    setErrorString(sqlite3_errmsg(mDb));
    return -1;
}

qint64 ImageBlobDevice::readData(char *data, qint64 maxlen)
{
    if(!mBlob)
        return -1;

    int offset = int(pos());
    if(maxlen + offset >= mSize)
        maxlen = mSize - offset;

    if(!maxlen)
        return -1;

    int rc = sqlite3_blob_read(mBlob, data, int(maxlen), offset);
    if(rc == SQLITE_OK)
        return maxlen;

    setErrorString(sqlite3_errmsg(mDb));
    return -1;
}

ImageBlobDevice* getImage(sqlite3pp::database& db, const MetaDataManager::Key &key)
{
    if(!db.db())
        return nullptr;

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db.db(), sql_get_key_id, sizeof (sql_get_key_id) - 1, &stmt, nullptr);
    if(rc != SQLITE_OK)
        return nullptr;

    rc = sqlite3_bind_text(stmt, 1, key.str, key.len, SQLITE_STATIC);
    if(rc != SQLITE_OK)
    {
        sqlite3_finalize(stmt);
        return nullptr;
    }

    rc = sqlite3_step(stmt);

    qint64 rowId = 0;
    if(rc != SQLITE_ROW)
    {
        sqlite3_finalize(stmt);
        return nullptr;
    }

    rowId = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);

    if(!rowId)
        return nullptr;

    return new ImageBlobDevice(db.db(), rowId);
}

void setImage(sqlite3pp::database& db, const MetaDataManager::Key &key, const void *data, int size)
{
    sqlite3pp::command cmd(db, "REPLACE INTO metadata(name, val) VALUES(?, ?)");
    sqlite3_bind_text(cmd.stmt(), 1, key.str, key.len, SQLITE_STATIC);
    if(data)
        sqlite3_bind_blob(cmd.stmt(), 2, data, size, SQLITE_STATIC);
    else
        sqlite3_bind_null(cmd.stmt(), 2);
    cmd.execute();
}

} // namespace ImageMetaData
