#pragma once

#include "../services/book_details_service.h"
#include <QDialog>

class QLabel;
class QListWidget;
class QPushButton;
class QProgressBar;
class QStackedWidget;

namespace bookhub::gui {

class LibraryService;
class QueryWorker;
class StepIndicatorWidget;
class VoiceSelectorWidget;
class MiniAudioPlayerWidget;
class VoiceUploadDialog;

class AudiobookFlowDialog : public QDialog {
    Q_OBJECT

public:
    explicit AudiobookFlowDialog(LibraryService *libraryService,
                                 QueryWorker    *worker,
                                 QWidget        *parent = nullptr);

    void startForBook(const QString &bookId);

private slots:
    void onDetailsCompleted(quint64 requestId, const bookhub::gui::BookDetails &details);
    void onFormatsCompleted(quint64 requestId,
                            QList<bookhub::gui::BookFormatEntry> formats);
    void onLanguageSelectionChanged();
    void onFormatSelectionChanged();
    void onVoiceSelectionChanged(int voiceId, const QString &voiceName);
    void onPreviewRequested(int voiceId, const QString &voiceName);
    void onVoiceUploadRequested();
    void onBackClicked();
    void onNextOrGenerateClicked();
    void onPlayPreview();
    void onGenerationProgress(int percent);
    void onGenerationComplete();
    void onOpenFileClicked();
    void onAddToLibraryClicked();

private:
    void buildUi();
    void resetState();
    void updateStepUi();
    void populateLanguageList();
    void populateFormatList();
    void populateVoiceList();
    void requestFormatsForSelectedLanguage();
    bool startGeneration();
    void setLibraryStatus(const QString &status);
    BookSourceEntry selectedSource() const;
    void showErrorState(const QString &message);

    LibraryService        *m_libraryService{};
    BookDetailsService    *m_detailsService{};
    QueryWorker           *m_worker{};

    QString m_bookId;
    QString m_bookTitle;
    int     m_libraryItemId{0};
    QList<BookEditionEntry> m_editions;
    QList<BookFormatEntry>  m_formats;
    bool    m_hasLanguageStep{false};

    quint64 m_pendingDetailsId{0};
    quint64 m_pendingFormatsId{0};
    quint64 m_pendingVoicesId{0};

    int     m_currentStep{0}; // 0=language, 1=format, 2=voice, 3=preview, 4=generate

    QString m_selectedLanguage;
    QString m_selectedFormat;
    int     m_selectedVoiceId{-1};
    QString m_selectedVoiceName;

    QLabel               *m_titleLabel{};
    QLabel               *m_stepLabel{};
    StepIndicatorWidget  *m_stepIndicator{};
    QStackedWidget       *m_stepsContainer{};
    QWidget              *m_progressContainer{};
    QProgressBar         *m_progressBar{};
    QLabel               *m_progressText{};
    QLabel               *m_resultLabel{};
    QPushButton          *m_backBtn{};
    QPushButton          *m_nextBtn{};

    // Step widgets (will be allocated in buildUi)
    QListWidget *m_languageList{};
    QListWidget *m_formatList{};
    VoiceSelectorWidget *m_voiceSelector{};
    MiniAudioPlayerWidget *m_previewPlayer{};
    QLabel *m_previewText{};

    VoiceUploadDialog *m_uploadDialog{};
};

} // namespace bookhub::gui
