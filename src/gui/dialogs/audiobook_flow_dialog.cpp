#include "audiobook_flow_dialog.h"

#include "../query_worker.h"
#include "../services/library_service.h"
#include "../services/tts_service.h"
#include "../widgets/mini_audio_player_widget.h"
#include "../widgets/step_indicator_widget.h"
#include "../widgets/voice_selector_widget.h"
#include "voice_upload_dialog.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#ifdef BOOKHUB_HAVE_MULTIMEDIA
#    include <QAudioOutput>
#    include <QMediaPlayer>
#endif

namespace bookhub::gui {

namespace {

// Parse duration from a standard PCM WAV byte array (44-byte header assumed).
// Returns 0 on malformed or missing data.
qint64 wavDurationMsFromBytes(const QByteArray &wav)
{
    if (wav.size() < 44)
        return 0;
    if (wav.mid(0, 4) != "RIFF" || wav.mid(8, 4) != "WAVE")
        return 0;

    auto u16 = [&wav](int off) -> quint32 {
        return static_cast<quint32>(static_cast<uchar>(wav[off])) |
               (static_cast<quint32>(static_cast<uchar>(wav[off + 1])) << 8);
    };
    auto u32 = [&wav](int off) -> quint32 {
        return static_cast<quint32>(static_cast<uchar>(wav[off])) |
               (static_cast<quint32>(static_cast<uchar>(wav[off + 1])) << 8) |
               (static_cast<quint32>(static_cast<uchar>(wav[off + 2])) << 16) |
               (static_cast<quint32>(static_cast<uchar>(wav[off + 3])) << 24);
    };

    const quint32 sampleRate = u32(24);
    const quint32 channels = u16(22);
    const quint32 bitsPerSample = u16(34);
    const quint32 dataBytes = u32(40);

    const quint64 bytesPerSecond = static_cast<quint64>(sampleRate) * channels * bitsPerSample / 8;
    if (bytesPerSecond == 0)
        return 0;
    return static_cast<qint64>(static_cast<quint64>(dataBytes) * 1000 / bytesPerSecond);
}

} // namespace

AudiobookFlowDialog::AudiobookFlowDialog(LibraryService *libraryService,
                                         QueryWorker *worker,
                                         TTSService *ttsService,
                                         QWidget *parent)
    : QDialog(parent),
      m_libraryService(libraryService),
      m_worker(worker),
      m_ttsService(ttsService ? ttsService : new NativeTTSService(this))
{
    setWindowTitle("Convert to Audiobook");
    setModal(true);
    setMinimumWidth(560);
    setMinimumHeight(480);

    m_detailsService = new BookDetailsService(this);
    if (m_worker)
        m_detailsService->connectToWorker(m_worker);
    connect(m_detailsService,
            &BookDetailsService::detailsCompleted,
            this,
            &AudiobookFlowDialog::onDetailsCompleted);
    connect(m_detailsService,
            &BookDetailsService::formatsCompleted,
            this,
            &AudiobookFlowDialog::onFormatsCompleted);
    connect(m_ttsService,
            &TTSService::previewGenerated,
            this,
            &AudiobookFlowDialog::onPreviewGenerated);
    connect(m_ttsService,
            &TTSService::generationProgress,
            this,
            &AudiobookFlowDialog::onGenerationProgress);
    connect(m_ttsService,
            &TTSService::generationCompleted,
            this,
            &AudiobookFlowDialog::onGenerationFinished);

    if (m_worker) {
        // Route listVoicesRequested through Qt's connection mechanism so the
        // call is queued when m_worker lives on the query thread.
        connect(this,
                &AudiobookFlowDialog::listVoicesRequested,
                m_worker,
                &QueryWorker::handleListVoicesRequest);

        connect(m_worker,
                &QueryWorker::listVoicesCompleted,
                this,
                [this](quint64 requestId, const QList<VoiceEntry> &voices) {
                    if (requestId == m_pendingVoicesId && m_voiceSelector) {
                        m_voiceSelector->setVoices(voices);
                        m_voicesLoaded = true;
                    }
                });
    }

    buildUi();

#ifdef BOOKHUB_HAVE_MULTIMEDIA
    m_audioOutput = new QAudioOutput(this);
    m_mediaPlayer = new QMediaPlayer(this);
    m_mediaPlayer->setAudioOutput(m_audioOutput);

    connect(m_mediaPlayer, &QMediaPlayer::positionChanged, this, [this](qint64 posMs) {
        m_previewPlayer->setCurrentTime(posMs);
        m_previewElapsedMs = posMs;
        if (!m_previewListened && posMs >= 3000) {
            m_previewListened = true;
            updateNextButtonEnabled();
        }
    });
    connect(m_mediaPlayer,
            &QMediaPlayer::playbackStateChanged,
            this,
            [this](QMediaPlayer::PlaybackState state) {
                const bool playing = (state == QMediaPlayer::PlayingState);
                m_previewPlayer->setPlaying(playing);
                if (playing) {
                    m_previewVoiceBtn->setText(QStringLiteral("■ Stop"));
                } else if (!m_previewAudioData.isEmpty()) {
                    m_previewVoiceBtn->setText(QStringLiteral("▶ Preview selected voice"));
                }
                if (state == QMediaPlayer::StoppedState)
                    m_previewPlayer->setCurrentTime(0);
            });
#endif

    resetState();
}

void AudiobookFlowDialog::buildUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    mainLayout->addWidget(m_titleLabel);

    m_stepIndicator = new StepIndicatorWidget(5, this);
    m_stepIndicator->setStepLabels({"Lang", "Format", "Voice", "Preview", "Generate"});
    mainLayout->addWidget(m_stepIndicator);

    m_stepsContainer = new QStackedWidget(this);
    mainLayout->addWidget(m_stepsContainer);

    // Step 1: Language selection
    QWidget *langStep = new QWidget(this);
    QVBoxLayout *langLayout = new QVBoxLayout(langStep);
    m_languageList = new QListWidget(this);
    m_languageList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_languageList,
            &QListWidget::itemSelectionChanged,
            this,
            &AudiobookFlowDialog::onLanguageSelectionChanged);
    langLayout->addWidget(m_languageList);
    m_stepsContainer->addWidget(langStep);

    // Step 2: Format selection
    QWidget *formatStep = new QWidget(this);
    QVBoxLayout *formatLayout = new QVBoxLayout(formatStep);
    m_formatList = new QListWidget(this);
    m_formatList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_formatList,
            &QListWidget::itemSelectionChanged,
            this,
            &AudiobookFlowDialog::onFormatSelectionChanged);
    formatLayout->addWidget(m_formatList);
    m_stepsContainer->addWidget(formatStep);

    // Step 3: Voice selection
    QWidget *voiceStep = new QWidget(this);
    QVBoxLayout *voiceLayout = new QVBoxLayout(voiceStep);
    m_voiceSelector = new VoiceSelectorWidget(this);
    connect(m_voiceSelector,
            &VoiceSelectorWidget::voiceSelected,
            this,
            &AudiobookFlowDialog::onVoiceSelectionChanged);
    connect(m_voiceSelector,
            &VoiceSelectorWidget::uploadNewVoiceRequested,
            this,
            &AudiobookFlowDialog::onVoiceUploadRequested);
    voiceLayout->addWidget(m_voiceSelector);
    // Phase 3: connect to TTSService to generate a 10-second preview clip
    m_previewVoiceBtn = new QPushButton("▶ Preview selected voice", this);
    m_previewVoiceBtn->setEnabled(false); // enabled once a voice is selected
    connect(m_previewVoiceBtn,
            &QPushButton::clicked,
            this,
            &AudiobookFlowDialog::onPreviewVoiceClicked);
    voiceLayout->addWidget(m_previewVoiceBtn);
    m_stepsContainer->addWidget(voiceStep);

    // Step 4: Preview
    QWidget *previewStep = new QWidget(this);
    QVBoxLayout *previewLayout = new QVBoxLayout(previewStep);
    m_previewVoiceLabel = new QLabel("Listening to: —", this);
    previewLayout->addWidget(m_previewVoiceLabel);
    m_previewText = new QLabel(this);
    m_previewText->setWordWrap(true);
    m_previewText->setMaximumHeight(80);
    previewLayout->addWidget(m_previewText);
    m_previewPlayer = new MiniAudioPlayerWidget(this);
    connect(m_previewPlayer,
            &MiniAudioPlayerWidget::playClicked,
            this,
            &AudiobookFlowDialog::onPlayPreview);
    connect(m_previewPlayer,
            &MiniAudioPlayerWidget::pauseClicked,
            this,
            &AudiobookFlowDialog::onStopPreview);
    previewLayout->addWidget(m_previewPlayer);
    previewLayout->addStretch();
    m_stepsContainer->addWidget(previewStep);

    // Step 5: Generation — all progress widgets live here, no double-parenting
    QWidget *genStep = new QWidget(this);
    QVBoxLayout *genLayout = new QVBoxLayout(genStep);
    genLayout->addSpacing(20);
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setAccessibleName(QStringLiteral("Audiobook generation progress"));
    m_progressBar->setAccessibleDescription(
        QStringLiteral("Generating audiobook, 0 percent complete"));
    genLayout->addWidget(m_progressBar);
    m_progressText = new QLabel("Estimated time: ~4 minutes", this);
    m_progressText->setAlignment(Qt::AlignCenter);
    genLayout->addWidget(m_progressText);
    genLayout->addSpacing(20);
    m_resultLabel = new QLabel(this);
    m_resultLabel->setAlignment(Qt::AlignCenter);
    genLayout->addWidget(m_resultLabel);
    m_cancelGenBtn = new QPushButton("Cancel", this);
    m_cancelGenBtn->hide();
    connect(m_cancelGenBtn,
            &QPushButton::clicked,
            this,
            &AudiobookFlowDialog::onCancelGenerationClicked);
    genLayout->addWidget(m_cancelGenBtn, 0, Qt::AlignHCenter);
    // Post-generation action buttons — hidden until generation completes
    QHBoxLayout *postGenLayout = new QHBoxLayout();
    m_openFileBtn = new QPushButton("Open file", this);
    m_openFileBtn->hide();
    connect(m_openFileBtn, &QPushButton::clicked, this, &AudiobookFlowDialog::onOpenFileClicked);
    m_saveAsBtn = new QPushButton("Save as…", this);
    m_saveAsBtn->hide();
    connect(m_saveAsBtn, &QPushButton::clicked, this, &AudiobookFlowDialog::onSaveAsClicked);
    m_addToLibBtn = new QPushButton("Add to library", this);
    m_addToLibBtn->hide();
    connect(
        m_addToLibBtn, &QPushButton::clicked, this, &AudiobookFlowDialog::onAddToLibraryClicked);
    postGenLayout->addWidget(m_openFileBtn);
    postGenLayout->addWidget(m_saveAsBtn);
    postGenLayout->addWidget(m_addToLibBtn);
    genLayout->addLayout(postGenLayout);
    genLayout->addStretch();
    m_stepsContainer->addWidget(genStep);

    // Navigation
    QHBoxLayout *navLayout = new QHBoxLayout();
    m_backBtn = new QPushButton("← Back", this);
    m_backBtn->setMaximumWidth(100);
    connect(m_backBtn, &QPushButton::clicked, this, &AudiobookFlowDialog::onBackClicked);
    navLayout->addWidget(m_backBtn);
    navLayout->addStretch();

    m_nextBtn = new QPushButton("Next →", this);
    m_nextBtn->setMaximumWidth(100);
    navLayout->addWidget(m_nextBtn);
    mainLayout->addLayout(navLayout);

    setLayout(mainLayout);
}

void AudiobookFlowDialog::startForBook(const QString &bookId)
{
    m_bookId = bookId;
    resetState();
    // Capture the id before emit so a same-thread (synchronous) reply from the
    // worker still matches m_pendingDetailsId in the completion slot.
    m_pendingDetailsId = m_detailsService->peekNextId();
    m_detailsService->requestBookDetails(bookId);
}

void AudiobookFlowDialog::resetState()
{
    m_currentStep = 0;
    m_hasLanguageStep = false;
    m_selectedLanguage.clear();
    m_selectedFormat.clear();
    m_selectedVoiceId = -1;
    m_selectedVoiceName.clear();
    m_previewAudioData.clear();
    m_previewListened = false;
    m_previewElapsedMs = 0;
    if (!m_previewTempPath.isEmpty()) {
        QFile::remove(m_previewTempPath);
        m_previewTempPath.clear();
    }
#ifdef BOOKHUB_HAVE_MULTIMEDIA
    if (m_mediaPlayer)
        m_mediaPlayer->stop();
#endif
    m_generatedOutputPath.clear();
    m_pendingDetailsId = 0;
    m_pendingFormatsId = 0;
    m_pendingVoicesId = 0;
    m_voicesLoaded = false;
    m_languageList->clear();
    m_formatList->clear();
    m_voiceSelector->setVoices({});

    // Restore Next button to navigation mode. onGenerationComplete() rewires it
    // to QDialog::accept; without this reset, reopening the dialog for a second
    // book leaves every "Next" click accepting the dialog immediately.
    disconnect(m_nextBtn, &QPushButton::clicked, nullptr, nullptr);
    connect(m_nextBtn, &QPushButton::clicked, this, &AudiobookFlowDialog::onNextOrGenerateClicked);

    // Reset generation step widgets to their initial state
    m_progressBar->setValue(0);
    m_progressText->setText("Estimated time: ~4 minutes");
    m_progressText->show();
    m_resultLabel->clear();
    m_cancelGenBtn->hide();
    m_openFileBtn->hide();
    m_saveAsBtn->hide();
    m_addToLibBtn->hide();
    m_nextBtn->setEnabled(true);

    updateStepUi();
}

void AudiobookFlowDialog::onDetailsCompleted(quint64 requestId, const BookDetails &details)
{
    if (requestId != m_pendingDetailsId)
        return;

    m_bookTitle = details.title;
    m_editions = details.editions;
    m_libraryItemId = details.libraryItemId;
    m_titleLabel->setText(QString("Convert to Audiobook — %1").arg(m_bookTitle));

    m_hasLanguageStep = m_editions.size() > 1;
    if (m_hasLanguageStep)
        populateLanguageList();

    if (!m_hasLanguageStep && !m_editions.isEmpty()) {
        m_selectedLanguage = m_editions.first().language;
        m_pendingFormatsId = m_detailsService->peekNextId();
        m_detailsService->requestFormatsForEdition(m_editions.first().editionId);
        // Skip the pointless single-language step and open directly on format selection.
        m_currentStep = 1;
        updateStepUi();
    }
}

void AudiobookFlowDialog::onFormatsCompleted(quint64 requestId,
                                             const QList<BookFormatEntry> &formats)
{
    if (requestId != m_pendingFormatsId)
        return;

    m_formats = formats;
    populateFormatList();
}

void AudiobookFlowDialog::onLanguageSelectionChanged()
{
    QListWidgetItem *item = m_languageList->currentItem();
    if (!item)
        return;

    m_selectedLanguage = item->text();
    for (const auto &edition : std::as_const(m_editions)) {
        if (edition.language == m_selectedLanguage) {
            m_pendingFormatsId = m_detailsService->peekNextId();
            m_detailsService->requestFormatsForEdition(edition.editionId);
            break;
        }
    }
    updateNextButtonEnabled();
}

void AudiobookFlowDialog::onFormatSelectionChanged()
{
    QListWidgetItem *item = m_formatList->currentItem();
    if (!item)
        return;

    m_selectedFormat = item->data(Qt::UserRole).toString();
    updateNextButtonEnabled();
}

void AudiobookFlowDialog::onVoiceSelectionChanged(int voiceId, const QString &voiceName)
{
    m_selectedVoiceId = voiceId;
    m_selectedVoiceName = voiceName;
    m_previewVoiceBtn->setEnabled(voiceId >= 0);
    m_previewVoiceBtn->setText(QStringLiteral("▶ Preview selected voice"));
    m_previewVoiceLabel->setText(voiceId >= 0 ? QString("Listening to: %1").arg(voiceName)
                                              : QStringLiteral("Listening to: —"));
    m_previewText->setText(previewScript());
    m_previewAudioData.clear();
    m_previewListened = false;
    m_previewElapsedMs = 0;
    if (!m_previewTempPath.isEmpty()) {
        QFile::remove(m_previewTempPath);
        m_previewTempPath.clear();
    }
#ifdef BOOKHUB_HAVE_MULTIMEDIA
    if (m_mediaPlayer)
        m_mediaPlayer->stop();
#endif
    m_previewPlayer->setPlaying(false);
    m_previewPlayer->setDuration(0);
    m_previewPlayer->setCurrentTime(0);
    updateNextButtonEnabled();
}

void AudiobookFlowDialog::onVoiceUploadRequested()
{
    VoiceUploadDialog uploadDialog(this);
    if (uploadDialog.exec() == QDialog::Accepted) {
        // Phase 3: insert into voices table and call populateVoiceList().
        // For now, acknowledge the submission so the user knows it was received.
        QMessageBox::information(
            this,
            "Voice Registered",
            QString("\"%1\" has been noted.\n\n"
                    "Custom voice cloning will be available in a future release.")
                .arg(uploadDialog.voiceName()));
    }
}

void AudiobookFlowDialog::onBackClicked()
{
    if (m_currentStep > 0) {
        m_currentStep--;
        updateStepUi();
    }
}

void AudiobookFlowDialog::onNextOrGenerateClicked()
{
    if (m_currentStep < 4) {
        m_currentStep++;
        updateStepUi();
    } else {
        startGeneration();
    }
}

void AudiobookFlowDialog::onPlayPreview()
{
    if (m_previewAudioData.isEmpty()) {
        onPreviewVoiceClicked();
        return;
    }
    if (m_previewTempPath.isEmpty())
        return;

#ifdef BOOKHUB_HAVE_MULTIMEDIA
    if (m_mediaPlayer) {
        m_mediaPlayer->setSource(QUrl::fromLocalFile(m_previewTempPath));
        m_mediaPlayer->play();
        return;
    }
#endif
    // Fallback: open in system audio player; treat as listened immediately.
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_previewTempPath));
    m_previewListened = true;
    updateNextButtonEnabled();
}

void AudiobookFlowDialog::onStopPreview()
{
#ifdef BOOKHUB_HAVE_MULTIMEDIA
    if (m_mediaPlayer)
        m_mediaPlayer->stop();
#endif
    if (m_playbackTimer) {
        m_playbackTimer->stop();
    }
    m_previewPlayer->setPlaying(false);
    if (!m_previewAudioData.isEmpty())
        m_previewVoiceBtn->setText(QStringLiteral("▶ Preview selected voice"));
}

void AudiobookFlowDialog::onGenerationProgress(int percent)
{
    m_progressBar->setValue(percent);
    m_progressBar->setAccessibleDescription(
        QStringLiteral("Generating audiobook, %1 percent complete").arg(percent));
}

void AudiobookFlowDialog::onGenerationComplete()
{
    m_resultLabel->setText("✓ Audiobook ready!");
    m_progressText->hide();
    m_cancelGenBtn->hide();
    m_openFileBtn->show();
    m_saveAsBtn->show();
    // Add-to-library only writes a row that already exists; if the book is not
    // in the library, the SQL UPDATE silently affects 0 rows. Hide the button
    // in that case so the click cannot misleadingly succeed.
    if (m_libraryItemId > 0)
        m_addToLibBtn->show();
    else
        m_addToLibBtn->hide();
    m_nextBtn->setText("Close");
    disconnect(
        m_nextBtn, &QPushButton::clicked, this, &AudiobookFlowDialog::onNextOrGenerateClicked);
    connect(m_nextBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void AudiobookFlowDialog::onPreviewVoiceClicked()
{
    if (m_selectedVoiceId < 0 || !m_ttsService)
        return;

    // If the button shows "■ Stop" (audio is playing), stop playback.
    if (m_previewVoiceBtn->text() == QStringLiteral("■ Stop")) {
        onStopPreview();
        return;
    }

    m_previewVoiceBtn->setEnabled(false);
    m_previewVoiceBtn->setText(QStringLiteral("Generating preview…"));
    m_previewText->setText(previewScript());
    m_ttsService->generatePreview(m_selectedVoiceId, m_selectedVoiceName, m_previewText->text());
}

void AudiobookFlowDialog::onPreviewGenerated(int voiceId, const QByteArray &audioData)
{
    if (voiceId != m_selectedVoiceId)
        return;

    m_previewAudioData = audioData;

    // Write to a temp file so the audio player (in-app or system) can open it.
    if (!m_previewTempPath.isEmpty())
        QFile::remove(m_previewTempPath);
    m_previewTempPath =
        QDir::tempPath() +
        QStringLiteral("/bookhub-preview-%1.wav").arg(QDateTime::currentMSecsSinceEpoch());
    QFile previewFile(m_previewTempPath);
    if (audioData.isEmpty() || !previewFile.open(QIODevice::WriteOnly) ||
        previewFile.write(audioData) != audioData.size()) {
        m_previewTempPath.clear();
    }

    m_previewVoiceBtn->setEnabled(true);
    m_previewVoiceBtn->setText(QStringLiteral("▶ Preview selected voice"));

    const qint64 durationMs = wavDurationMsFromBytes(audioData);
    m_previewPlayer->setDuration(durationMs);
    m_previewPlayer->setCurrentTime(0);

#ifdef BOOKHUB_HAVE_MULTIMEDIA
    // Auto-play the preview on step 3 so the user hears it immediately.
    if (m_mediaPlayer && !m_previewTempPath.isEmpty()) {
        m_mediaPlayer->setSource(QUrl::fromLocalFile(m_previewTempPath));
        m_mediaPlayer->play();
    }
#endif

    m_resultLabel->clear();
}

void AudiobookFlowDialog::onGenerationFinished(bool success, const QString &outputPath)
{
    if (!success) {
        m_cancelGenBtn->hide();
        m_progressText->setText("Generation failed.");
        m_resultLabel->setText("Could not generate audiobook.");
        m_nextBtn->setText("Retry");
        m_nextBtn->setEnabled(true);
        return;
    }

    m_generatedOutputPath = outputPath;
    onGenerationComplete();
}

void AudiobookFlowDialog::onOpenFileClicked()
{
    if (!m_generatedOutputPath.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_generatedOutputPath));
}

void AudiobookFlowDialog::onSaveAsClicked()
{
    if (m_generatedOutputPath.isEmpty())
        return;
    const QString dest = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Save Audiobook As"),
        QDir::homePath() + QLatin1Char('/') + QFileInfo(m_generatedOutputPath).fileName(),
        QStringLiteral("WAV audio (*.wav);;All files (*)"));
    if (!dest.isEmpty() && !QFile::copy(m_generatedOutputPath, dest))
        showErrorState(QStringLiteral("Could not save the audiobook to \"%1\".\n"
                                      "Check disk space and permissions.")
                           .arg(dest));
}

void AudiobookFlowDialog::onAddToLibraryClicked()
{
    // TODO: Phase 3 — wait for audiobookConversionCompleted signal before
    // closing so a failed DB write surfaces an error rather than silently
    // dropping the status update.
    markAudiobookReady();
    accept();
}

void AudiobookFlowDialog::onCancelGenerationClicked()
{
    if (m_ttsService)
        m_ttsService->cancel();

    m_cancelGenBtn->hide();
    m_progressText->setText("Generation cancelled.");
    m_resultLabel->setText("Audiobook generation was cancelled.");
    m_nextBtn->setText("Retry");
    m_nextBtn->setEnabled(true);
}

void AudiobookFlowDialog::populateLanguageList()
{
    m_languageList->clear();
    for (const auto &edition : std::as_const(m_editions)) {
        QListWidgetItem *item = new QListWidgetItem(edition.language);
        m_languageList->addItem(item);
    }
    if (!m_editions.isEmpty())
        m_languageList->setCurrentRow(0);
}

void AudiobookFlowDialog::populateFormatList()
{
    m_formatList->clear();
    int epubIndex = -1;
    for (const auto &format : std::as_const(m_formats)) {
        const QString &type = format.formatType;
        if (!isTextCompatibleFormat(type))
            continue;
        QString label = type;
        if (type.startsWith(QLatin1String("epub"))) {
            label += " (recommended)";
            epubIndex = m_formatList->count();
        }
        auto *item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, type);
        m_formatList->addItem(item);
    }
    if (m_formatList->count() > 0) {
        const int selectRow = (epubIndex >= 0) ? epubIndex : 0;
        m_formatList->setCurrentRow(selectRow);
        m_selectedFormat = m_formatList->item(selectRow)->data(Qt::UserRole).toString();
    } else {
        m_selectedFormat.clear();
    }
}

void AudiobookFlowDialog::populateVoiceList()
{
    // Voices are global, not per-book — load once per dialog session so
    // navigating back/forward through step 2 doesn't reset the selection.
    if (m_worker && !m_voicesLoaded) {
        m_pendingVoicesId = m_requestCounter++;
        emit listVoicesRequested(m_pendingVoicesId);
    }
}

void AudiobookFlowDialog::updateStepUi()
{
    m_stepIndicator->setCurrentStep(m_currentStep);
    m_stepsContainer->setCurrentIndex(m_currentStep);

    m_backBtn->setVisible(m_currentStep > 0);
    m_nextBtn->setText(m_currentStep == 4 ? QStringLiteral("✓ Confirm & Generate")
                                          : QStringLiteral("Next →"));

    if (m_currentStep == 0 && m_hasLanguageStep)
        populateLanguageList();
    else if (m_currentStep == 1)
        populateFormatList();
    else if (m_currentStep == 2)
        populateVoiceList();

    updateNextButtonEnabled();
}

void AudiobookFlowDialog::updateNextButtonEnabled()
{
    // Each step gates Next on its own selection so the user cannot reach the
    // generate step with empty fields. Steps 3 (preview) and 4 (generate) need
    // no extra input — Next is always enabled there.
    bool enabled = true;
    switch (m_currentStep) {
        case 0:
            enabled = !m_selectedLanguage.isEmpty();
            break;
        case 1:
            enabled = !m_selectedFormat.isEmpty();
            break;
        case 2:
            enabled = m_selectedVoiceId >= 0;
            break;
        case 3:
            enabled = m_previewListened;
            break;
        default:
            enabled = true;
            break;
    }
    m_nextBtn->setEnabled(enabled);
}

bool AudiobookFlowDialog::startGeneration()
{
    if (m_selectedLanguage.isEmpty() || m_selectedFormat.isEmpty() || m_selectedVoiceId < 0) {
        showErrorState("Please select language, format, and voice.");
        return false;
    }
    if (!m_ttsService) {
        showErrorState("The text-to-speech service is unavailable.");
        return false;
    }

    m_generatedOutputPath = defaultOutputPath();
    if (m_generatedOutputPath.isEmpty()) {
        showErrorState("Could not prepare the audiobook output folder.");
        return false;
    }

    m_progressBar->setValue(0);
    m_progressText->setText("Generating audiobook...");
    m_progressText->show();
    m_resultLabel->clear();
    m_cancelGenBtn->show();
    m_openFileBtn->hide();
    m_saveAsBtn->hide();
    m_addToLibBtn->hide();
    m_nextBtn->setEnabled(false);
    if (m_libraryItemId > 0 && m_libraryService)
        m_libraryService->requestUpdateStatus(m_libraryItemId, QStringLiteral("converting"));

    m_ttsService->generateAudiobook(
        m_selectedVoiceId, m_selectedVoiceName, generationScript(), m_generatedOutputPath);
    return true;
}

void AudiobookFlowDialog::markAudiobookReady()
{
    if (m_libraryItemId > 0 && m_libraryService)
        m_libraryService->requestSetAudiobookReady(m_bookId);
}

BookSourceEntry AudiobookFlowDialog::selectedSource() const
{
    for (const auto &fmt : m_formats) {
        if (fmt.formatType == m_selectedFormat && !fmt.sources.isEmpty())
            return fmt.sources.first();
    }
    return BookSourceEntry{};
}

bool AudiobookFlowDialog::isTextCompatibleFormat(const QString &formatType) const
{
    const QString type = formatType.toLower();
    return type.startsWith(QLatin1String("epub")) || type.startsWith(QLatin1String("txt")) ||
           type.startsWith(QLatin1String("text")) || type.startsWith(QLatin1String("html"));
}

QString AudiobookFlowDialog::previewScript() const
{
    const QString title = m_bookTitle.isEmpty() ? QStringLiteral("this book") : m_bookTitle;
    const QString voice =
        m_selectedVoiceName.isEmpty() ? QStringLiteral("the selected voice") : m_selectedVoiceName;
    return QStringLiteral("%1 reading from %2. This is a short BookHub voice preview.")
        .arg(voice, title);
}

QString AudiobookFlowDialog::generationScript() const
{
    const BookSourceEntry source = selectedSource();
    QStringList lines;
    lines << QStringLiteral("BookHub audiobook")
          << QStringLiteral("Title: %1")
                 .arg(m_bookTitle.isEmpty() ? QStringLiteral("Untitled book") : m_bookTitle)
          << QStringLiteral("Language: %1").arg(m_selectedLanguage)
          << QStringLiteral("Format source: %1")
                 .arg(source.sourceName.isEmpty() ? m_selectedFormat : source.sourceName)
          << QStringLiteral("Voice: %1").arg(m_selectedVoiceName)
          << QStringLiteral("This MVP build creates a local sample narration file. "
                            "Full text extraction from downloaded editions will use the same "
                            "generation service in the packaging phase.");
    return lines.join(QLatin1Char('\n'));
}

QString AudiobookFlowDialog::defaultOutputPath() const
{
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (baseDir.isEmpty())
        baseDir = QDir::homePath() + QStringLiteral("/.bookhub");

    QDir dir(baseDir);
    if (!dir.mkpath(QStringLiteral("audiobooks"))) {
        dir = QDir(QDir::tempPath() + QStringLiteral("/bookhub"));
        if (!dir.mkpath(QStringLiteral("audiobooks")))
            return {};
    }
    dir.cd(QStringLiteral("audiobooks"));

    QString stem = m_bookTitle.simplified().toLower();
    static const QRegularExpression kNonAlnum(QStringLiteral("[^a-z0-9]+"));
    stem.replace(kNonAlnum, QStringLiteral("-"));
    stem = stem.trimmed();
    while (stem.startsWith(QLatin1Char('-')))
        stem.remove(0, 1);
    while (stem.endsWith(QLatin1Char('-')))
        stem.chop(1);
    if (stem.isEmpty())
        stem = QStringLiteral("audiobook");

    const QString stamp =
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmss"));
    return dir.filePath(QStringLiteral("%1-%2.wav").arg(stem, stamp));
}

void AudiobookFlowDialog::showErrorState(const QString &message)
{
    auto *box = new QMessageBox(
        QMessageBox::Warning, QStringLiteral("Error"), message, QMessageBox::Ok, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}

} // namespace bookhub::gui
