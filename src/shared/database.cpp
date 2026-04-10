#include "database.h"
#include <QSqlDatabase>
#include <QSqlError>
#include <QDebug>

namespace classic_books::db {

bool initializeDatabase(const QString &filePath)
{
    auto db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(filePath);

    if (!db.open()) {
        qWarning() << "Failed to open SQLite database:" << db.lastError().text();
        return false;
    }

    qDebug() << "Opened SQLite database at" << filePath;
    return true;
}

} // namespace classic_books::db
