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
class AudiobookFlowDialogTest;

class AudiobookFlowDialog : public QDialog {
    Q_OBJECT

public:
    explicit AudiobookFlowDialog(LibraryService *libraryService,
                                 QueryWorker    *worker,
                                 QWidget        *parent = nullptr);

    void startForBook(const QString &bookId);

signals:
    void listVoicesRequested(quint64 requestId);

private slots:
    void onDetailsCompleted(quint64 requestId, const bookhub::gui::BookDetails &details);
    void onFormatsCompleted(quint64 requestId,
                            QList<bookhub::gui::BookFormatEntry> formats);
    void onLanguageSelectionChanged();
    void onFormatSelectionChanged();
    void onVoiceSelectionChanged(int voiceId, const QString &voiceName);
    void onVoiceUploadRequested();
    void onBackClicked();
    void onNextOrGenerateClicked();
    void onPlayPreview();
    void onGenerationProgress(int percent);
    void onGenerationComplete();
    void onPreviewVoiceClicked();   // Phase 3: generate 10-second preview via TTSService
    void onOpenFileClicked();       // Phase 3: open generated file via QDesktopServices
    void onAddToLibraryClicked();   // Phase 3: update library status to audiobook_ready

private:
    void buildUi();
    void resetState();
    void updateStepUi();
    void updateNextButtonEnabled();
    void populateLanguageList();
    void populateFormatList();
    void populateVoiceList();
    bool startGeneration();
    void markAudiobookReady();
    BookSourceEntry selectedSource() const;
    void showErrorState(const QString &message);

    friend class ::bookhub::gui::AudiobookFlowDialogTest;

    LibraryService        *m_libraryService{};
    BookDetailsService    *m_detailsService{};
    QueryWorker           *m_worker{};

    QString m_bookId;
    QString m_bookTitle;
    int     m_libraryItemId{0};
    QList<BookEditionEntry> m_editions;
    QList<BookFormatEntry>  m_formats;
    bool    m_hasLanguageStep{false};

    quint64 m_requestCounter{1};

    quint64 m_pendingDetailsId{0};
    quint64 m_pendingFormatsId{0};
    quint64 m_pendingVoicesId{0};

    int     m_currentStep{0}; // 0=language, 1=format, 2=voice, 3=preview, 4=generate

    QString m_selectedLanguage;
    QString m_selectedFormat;
    int     m_selectedVoiceId{-1};
    QString m_selectedVoiceName;

    QLabel               *m_titleLabel{};
    StepIndicatorWidget  *m_stepIndicator{};
    QStackedWidget       *m_stepsContainer{};
    QProgressBar         *m_progressBar{};
    QLabel               *m_progressText{};
    QLabel               *m_resultLabel{};
    QPushButton          *m_cancelGenBtn{};  // visible during generation
    QPushButton          *m_openFileBtn{};   // visible after completion
    QPushButton          *m_addToLibBtn{};   // visible after completion
    QPushButton          *m_backBtn{};
    QPushButton          *m_nextBtn{};

    QListWidget *m_languageList{};
    QListWidget *m_formatList{};
    VoiceSelectorWidget   *m_voiceSelector{};
    QPushButton           *m_previewVoiceBtn{};  // preview selected voice (Phase 3)
    QLabel                *m_previewVoiceLabel{}; // shows selected voice name in step 4
    MiniAudioPlayerWidget *m_previewPlayer{};
    QLabel                *m_previewText{};
};

} // namespace bookhub::gui
