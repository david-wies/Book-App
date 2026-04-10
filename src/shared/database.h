#pragma once

#include <QString>

class QSqlDatabase;

namespace classic_books::db {

bool initializeDatabase(const QString &filePath);

} // namespace classic_books::db
