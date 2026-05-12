#pragma once

#include <QString>

#include <QSqlDatabase>

namespace bookhub::db {

inline constexpr int kSchemaVersion = 6;

/// Returns the absolute path to the database file, located next to the executable.
QString databaseFilePath();

bool initializeDatabase(const QString &filePath, const QString &connectionName = QSqlDatabase::defaultConnection);
bool createSchema(const QString &connectionName = QSqlDatabase::defaultConnection);
bool verifySchemaVersion(const QString &connectionName = QSqlDatabase::defaultConnection);
bool insertSampleData(const QString &connectionName = QSqlDatabase::defaultConnection);

} // namespace bookhub::db
