#pragma once

#include "../services/book_details_service.h"

#include <QByteArray>
#include <QDialog>

class QLabel;
class QListWidget;
class QPushButton;
class QProgressBar;
class QStackedWidget;
#ifdef BOOKHUB_HAVE_MULTIMEDIA
class QMediaPlayer;
class QAudioOutput;
#endif

namespace bookhub::gui {

class LibraryService;
class QueryWorker;
class TTSService;
class StepIndicatorWidget;
class VoiceSelectorWidget;
class MiniAudioPlayerWidget;
class VoiceUploadDialog;
class AudiobookFlowDialogTest;

class AudiobookFlowDialog : public QDialog
{
    Q_OBJECT

public:
    // ttsService: caller retains ownership. Null → a NativeTTSService is created
    // and owned by this dialog.
    explicit AudiobookFlowDialog(LibraryService *libraryService,
                                 QueryWorker *worker,
                                 TTSService *ttsService = nullptr,
                                 QWidget *parent = nullptr);
    ~AudiobookFlowDialog() override;

    void startForBook(const QString &bookId);

signals:
    void listVoicesRequested(quint64 requestId);

private slots:
    void onDetailsCompleted(quint64 requestId, const bookhub::gui::BookDetails &details);
    void onFormatsCompleted(quint64 requestId, const QList<bookhub::gui::BookFormatEntry> &formats);
    void onLanguageSelectionChanged();
    void onFormatSelectionChanged();
    void onVoiceSelectionChanged(int voiceId, const QString &voiceName);
    void onVoiceUploadRequested();
    void onBackClicked();
    void onNextOrGenerateClicked();
    void onPlayPreview();
    void onStopPreview();
    void onGenerationProgress(int percent);
    void onGenerationComplete();
    void onPreviewGenerated(int voiceId, const QByteArray &audioData);
    void onGenerationFinished(bool success, const QString &outputPath);
    void onPreviewVoiceClicked();
    void onOpenFileClicked();
    void onSaveAsClicked();
    void onAddToLibraryClicked();
    void onCancelGenerationClicked();

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
    bool isTextCompatibleFormat(const QString &formatType) const;
    QString previewScript() const;
    QString generationScript() const;
    QString defaultOutputPath() const;
    void showErrorState(const QString &message);

    friend class ::bookhub::gui::AudiobookFlowDialogTest;

    LibraryService *m_libraryService{};
    BookDetailsService *m_detailsService{};
    QueryWorker *m_worker{};
    TTSService *m_ttsService{};

    QString m_bookId;
    QString m_bookTitle;
    int m_libraryItemId{0};
    QList<BookEditionEntry> m_editions;
    QList<BookFormatEntry> m_formats;
    bool m_hasLanguageStep{false};

    quint64 m_requestCounter{1};

    quint64 m_pendingDetailsId{0};
    quint64 m_pendingFormatsId{0};
    quint64 m_pendingVoicesId{0};
    // Set once voices have been loaded so back/forward navigation through
    // step 2 doesn't re-fire the query and clobber the user's selection.
    bool m_voicesLoaded{false};

    int m_currentStep{0}; // 0=language, 1=format, 2=voice, 3=preview, 4=generate

    QString m_selectedLanguage;
    QString m_selectedFormat;
    int m_selectedVoiceId{-1};
    QString m_selectedVoiceName;
    QByteArray m_previewAudioData;
    QString m_previewTempPath;
    bool m_previewListened{false};
    bool m_previewPlaying{false}; // true while QMediaPlayer is in PlayingState
    QString m_generatedOutputPath;
#ifdef BOOKHUB_HAVE_MULTIMEDIA
    QMediaPlayer *m_mediaPlayer{};
    QAudioOutput *m_audioOutput{};
#endif

    QLabel *m_titleLabel{};
    StepIndicatorWidget *m_stepIndicator{};
    QStackedWidget *m_stepsContainer{};
    QProgressBar *m_progressBar{};
    QLabel *m_progressText{};
    QLabel *m_resultLabel{};
    QPushButton *m_cancelGenBtn{}; // visible during generation
    QPushButton *m_openFileBtn{};  // visible after completion
    QPushButton *m_saveAsBtn{};    // visible after completion
    QPushButton *m_addToLibBtn{};  // visible after completion
    QPushButton *m_backBtn{};
    QPushButton *m_nextBtn{};

    QListWidget *m_languageList{};
    QListWidget *m_formatList{};
    VoiceSelectorWidget *m_voiceSelector{};
    QPushButton *m_previewVoiceBtn{};
    QLabel *m_previewVoiceLabel{}; // shows selected voice name in step 4
    MiniAudioPlayerWidget *m_previewPlayer{};
    QLabel *m_previewText{};
};

} // namespace bookhub::gui
