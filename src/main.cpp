#include <QApplication>
#include <QMainWindow>
#include <QLabel>
#include <QIcon>
#include <QDir>
#include "database.h"
#include "collector/collector_worker.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    if (classic_books::db::initializeDatabase(classic_books::db::databaseFilePath())) {
        classic_books::db::createSchema();
        classic_books::db::insertSampleData();
    }

    // Start background collector thread
    classic_books::collector::CollectorWorker collector;
    collector.start();

    const QString iconPath = QDir(QCoreApplication::applicationDirPath()).filePath("book_reader_icon.jpg");
    QIcon appIcon(iconPath);
    app.setWindowIcon(appIcon);

    QMainWindow window;
    window.setWindowTitle("Classic Books Audiobook Hub");
    window.setWindowIcon(appIcon);
    window.resize(1024, 768);

    auto *label = new QLabel("Welcome to Classic Books Audiobook Hub", &window);
    label->setAlignment(Qt::AlignCenter);
    window.setCentralWidget(label);

    window.show();
    
    int result = app.exec();
    
    // Stop background thread cleanly
    collector.quit();
    collector.wait();
    
    return result;
}
