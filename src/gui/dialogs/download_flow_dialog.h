#pragma once

#include "../services/book_details_service.h"

#include <QDialog>

class QLabel;
class QListWidget;
class QPushButton;
class QProgressBar;
class QSaveFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace bookhub::gui {

class LibraryService;
class QueryWorker;
class StepIndicatorWidget;
class DownloadFlowDialogTest;

class DownloadFlowDialog : public QDialog {
    Q_OBJECT
public:
    explicit DownloadFlowDialog(LibraryService *libraryService,
                                QueryWorker    *worker,
                                QWidget        *parent = nullptr);

    void startForBook(const QString &bookId);

private slots:
    void onDetailsCompleted(quint64 requestId, const bookhub::gui::BookDetails &details);
    void onFormatsCompleted(quint64 requestId,
                            QList<bookhub::gui::BookFormatEntry> formats);
    void onLanguageSelectionChanged();
    void onFormatSelectionChanged();
    void onSourceSelectionChanged();
    void onBackClicked();
    void onNextOrDownloadClicked();
    void onCancelDownloadClicked();
    void onOpenFileClicked();
    void onRetryClicked();
    void onDownloadReadyRead();
    void onDownloadProgress(qint64 received, qint64 total);
    void onDownloadFinished();

private:
    void buildUi();
    void resetState();
    void updateStepUi();
    void populateLanguageList();
    void populateFormatList();
    void populateSourceList();
    void requestFormatsForSelectedLanguage();
    bool beginDownload();
    QString suggestedFilePath() const;
    QString sanitizeFileName(const QString &name) const;
    void showErrorState(const QString &message);
    void setLibraryStatus(const QString &status);
    BookSourceEntry selectedSource() const;

    friend class ::bookhub::gui::DownloadFlowDialogTest;

    LibraryService        *m_libraryService{};
    BookDetailsService    *m_detailsService{};
    QNetworkAccessManager *m_network{};
    QNetworkReply         *m_reply{};
    QSaveFile             *m_outputFile{};

    QString m_bookId;
    QString m_bookTitle;
    int     m_libraryItemId{0};
    QList<BookEditionEntry> m_editions;
    QList<BookFormatEntry>  m_formats;
    bool    m_hasLanguageStep{false};

    quint64 m_pendingDetailsId{0};
    quint64 m_pendingFormatsId{0};

    int     m_currentStep{0}; // 0=language, 1=format, 2=source

    QString m_targetFilePath;

    QLabel               *m_titleLabel{};
    QLabel               *m_stepLabel{};
    StepIndicatorWidget  *m_stepIndicator{};
    QListWidget          *m_languageList{};
    QListWidget          *m_formatList{};
    QListWidget          *m_sourceList{};
    QPushButton          *m_backBtn{};
    QPushButton          *m_nextBtn{};

    QWidget      *m_stepsContainer{};
    QWidget      *m_progressContainer{};
    QProgressBar *m_progressBar{};
    QLabel       *m_progressText{};
    QLabel       *m_progressPathLabel{};
    QLabel       *m_resultLabel{};
    QPushButton  *m_cancelBtn{};
    QPushButton  *m_openFileBtn{};
    QPushButton  *m_retryBtn{};
};

} // namespace bookhub::gui
