#pragma once

#include <QString>

#include <QSqlDatabase>

namespace classic_books::db {

bool initializeDatabase(const QString &filePath, const QString &connectionName = QSqlDatabase::defaultConnection);
bool createSchema(const QString &connectionName = QSqlDatabase::defaultConnection);
bool insertSampleData(const QString &connectionName = QSqlDatabase::defaultConnection);

} // namespace classic_books::db
