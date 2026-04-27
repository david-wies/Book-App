#pragma once

#include "database.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

namespace bookhub::tests {

class TestDatabase
{
public:
    TestDatabase() = default;
    ~TestDatabase() { close(); }

    bool open(const QString &name = QSqlDatabase::defaultConnection)
    {
        close();
        connectionName = name;
        m_filePath = tempDir.filePath(QStringLiteral("bookhub-test.db"));
        QFile::remove(m_filePath);

        if (!db::initializeDatabase(m_filePath, connectionName))
            return false;

        db = QSqlDatabase::database(connectionName);
        return db.isOpen();
    }

    bool createSchema() const
    {
        return db::createSchema(connectionName);
    }

    bool insertSampleData() const
    {
        return db::insertSampleData(connectionName);
    }

    bool exec(const QString &sql) const
    {
        QSqlQuery query(db);
        return query.exec(sql);
    }

    int scalarInt(const QString &sql) const
    {
        QSqlQuery query(db);
        if (!query.exec(sql) || !query.next())
            return -1;
        return query.value(0).toInt();
    }

    QString scalarString(const QString &sql) const
    {
        QSqlQuery query(db);
        if (!query.exec(sql) || !query.next())
            return {};
        return query.value(0).toString();
    }

    void close()
    {
        if (connectionName.isEmpty())
            return;

        const QString name = connectionName;
        if (QSqlDatabase::contains(name)) {
            db.close();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(name);
        }
        connectionName.clear();
        m_filePath.clear();
    }

    QSqlDatabase database() const { return db; }
    QString connection() const { return connectionName; }
    QString filePath() const { return m_filePath; }

private:
    QTemporaryDir tempDir;
    QString connectionName;
    QString m_filePath;
    QSqlDatabase db;
};

inline bool execSql(const QSqlDatabase &db, const QString &sql)
{
    QSqlQuery query(db);
    return query.exec(sql);
}

inline void isolateSettings(const QString &dirName)
{
    const QString root = QDir::temp().filePath(dirName);
    QDir().mkpath(root);
    qputenv("XDG_CONFIG_HOME", root.toUtf8());
}

} // namespace bookhub::tests
