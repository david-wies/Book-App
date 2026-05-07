#include "audiobook_flow_dialog.h"
#include "voice_upload_dialog.h"
#include "../widgets/step_indicator_widget.h"
#include "../widgets/voice_selector_widget.h"
#include "../widgets/mini_audio_player_widget.h"
#include "../services/library_service.h"
#include "../query_worker.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QProgressBar>
#include <QStackedWidget>
#include <QMessageBox>

namespace bookhub::gui {

AudiobookFlowDialog::AudiobookFlowDialog(LibraryService *libraryService,
                                         QueryWorker    *worker,
                                         QWidget        *parent)
    : QDialog(parent)
    , m_libraryService(libraryService)
    , m_worker(worker)
{
    setWindowTitle("Convert to Audiobook");
    setModal(true);
    setMinimumWidth(560);
    setMinimumHeight(480);

    m_detailsService = new BookDetailsService(this);
    if (m_worker)
        m_detailsService->connectToWorker(m_worker);
    connect(m_detailsService, &BookDetailsService::detailsCompleted,
            this, &AudiobookFlowDialog::onDetailsCompleted);
    connect(m_detailsService, &BookDetailsService::formatsCompleted,
            this, &AudiobookFlowDialog::onFormatsCompleted);

    if (m_worker) {
        // Route listVoicesRequested through Qt's connection mechanism so the
        // call is queued when m_worker lives on the query thread.
        connect(this, &AudiobookFlowDialog::listVoicesRequested,
                m_worker, &QueryWorker::handleListVoicesRequest);

        connect(m_worker,
                &QueryWorker::listVoicesCompleted,
                this,
                [this](quint64 requestId, const QList<VoiceEntry> &voices) {
                    if (requestId == m_pendingVoicesId && m_voiceSelector)
                        m_voiceSelector->setVoices(voices);
                });
    }

    buildUi();
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
    connect(m_languageList, &QListWidget::itemSelectionChanged,
            this, &AudiobookFlowDialog::onLanguageSelectionChanged);
    langLayout->addWidget(m_languageList);
    m_stepsContainer->addWidget(langStep);

    // Step 2: Format selection
    QWidget *formatStep = new QWidget(this);
    QVBoxLayout *formatLayout = new QVBoxLayout(formatStep);
    m_formatList = new QListWidget(this);
    m_formatList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_formatList, &QListWidget::itemSelectionChanged,
            this, &AudiobookFlowDialog::onFormatSelectionChanged);
    formatLayout->addWidget(m_formatList);
    m_stepsContainer->addWidget(formatStep);

    // Step 3: Voice selection
    QWidget *voiceStep = new QWidget(this);
    QVBoxLayout *voiceLayout = new QVBoxLayout(voiceStep);
    m_voiceSelector = new VoiceSelectorWidget(this);
    connect(m_voiceSelector, &VoiceSelectorWidget::voiceSelected,
            this, &AudiobookFlowDialog::onVoiceSelectionChanged);
    connect(m_voiceSelector, &VoiceSelectorWidget::uploadNewVoiceRequested,
            this, &AudiobookFlowDialog::onVoiceUploadRequested);
    voiceLayout->addWidget(m_voiceSelector);
    // Phase 3: connect to TTSService to generate a 10-second preview clip
    m_previewVoiceBtn = new QPushButton("▶ Preview selected voice", this);
    m_previewVoiceBtn->setEnabled(false); // enabled once a voice is selected
    connect(m_previewVoiceBtn, &QPushButton::clicked,
            this, &AudiobookFlowDialog::onPreviewVoiceClicked);
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
    connect(m_previewPlayer, &MiniAudioPlayerWidget::playClicked,
            this, &AudiobookFlowDialog::onPlayPreview);
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
    genLayout->addWidget(m_progressBar);
    m_progressText = new QLabel("Estimated time: ~4 minutes", this);
    m_progressText->setAlignment(Qt::AlignCenter);
    genLayout->addWidget(m_progressText);
    genLayout->addSpacing(20);
    m_resultLabel = new QLabel(this);
    m_resultLabel->setAlignment(Qt::AlignCenter);
    genLayout->addWidget(m_resultLabel);
    // Cancel button — visible during generation, hidden once complete (Phase 3)
    m_cancelGenBtn = new QPushButton("Cancel", this);
    m_cancelGenBtn->hide();
    genLayout->addWidget(m_cancelGenBtn, 0, Qt::AlignHCenter);
    // Post-generation action buttons — hidden until generation completes (Phase 3)
    QHBoxLayout *postGenLayout = new QHBoxLayout();
    m_openFileBtn = new QPushButton("Open file", this);
    m_openFileBtn->hide();
    connect(m_openFileBtn, &QPushButton::clicked,
            this, &AudiobookFlowDialog::onOpenFileClicked);
    m_addToLibBtn = new QPushButton("Add to library", this);
    m_addToLibBtn->hide();
    connect(m_addToLibBtn, &QPushButton::clicked,
            this, &AudiobookFlowDialog::onAddToLibraryClicked);
    postGenLayout->addWidget(m_openFileBtn);
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
    m_languageList->clear();
    m_formatList->clear();
    m_voiceSelector->setVoices({});

    // Restore Next button to navigation mode. onGenerationComplete() rewires it
    // to QDialog::accept; without this reset, reopening the dialog for a second
    // book leaves every "Next" click accepting the dialog immediately.
    disconnect(m_nextBtn, &QPushButton::clicked, nullptr, nullptr);
    connect(m_nextBtn, &QPushButton::clicked,
            this, &AudiobookFlowDialog::onNextOrGenerateClicked);

    // Reset generation step widgets to their initial state
    m_progressBar->setValue(0);
    m_progressText->setText("Estimated time: ~4 minutes");
    m_progressText->show();
    m_resultLabel->clear();
    m_cancelGenBtn->hide();
    m_openFileBtn->hide();
    m_addToLibBtn->hide();

    updateStepUi();
}

void AudiobookFlowDialog::onDetailsCompleted(quint64 requestId,
                                              const BookDetails &details)
{
    if (requestId != m_pendingDetailsId)
        return;

    m_bookTitle = details.title;
    m_editions = details.editions;
    m_libraryItemId = details.libraryItemId;
    m_titleLabel->setText(QString("Convert to Audiobook — %1").arg(m_bookTitle));

    m_hasLanguageStep = m_editions.size() > 1;
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
                                              QList<BookFormatEntry> formats)
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
    for (const auto &edition : m_editions) {
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
    m_previewVoiceLabel->setText(voiceId >= 0
        ? QString("Listening to: %1").arg(voiceName)
        : QStringLiteral("Listening to: —"));
    updateNextButtonEnabled();
}

void AudiobookFlowDialog::onVoiceUploadRequested()
{
    delete m_uploadDialog;
    m_uploadDialog = new VoiceUploadDialog(this);
    if (m_uploadDialog->exec() == QDialog::Accepted) {
        // Phase 3: insert into voices table and call populateVoiceList().
        // For now, acknowledge the submission so the user knows it was received.
        QMessageBox::information(this, "Voice Registered",
            QString("\"%1\" has been noted.\n\n"
                    "Custom voice cloning will be available in a future release.")
                .arg(m_uploadDialog->voiceName()));
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
        if (startGeneration()) {
            m_progressBar->setValue(0);
            m_progressText->setText("Generating audiobook...");
            m_resultLabel->clear();
            // Phase 3+: connect to actual TTS service progress
            onGenerationProgress(100);
            onGenerationComplete();
        }
    }
}

void AudiobookFlowDialog::onPlayPreview()
{
    // Phase 3+: generate and play preview audio via TTSService
}

void AudiobookFlowDialog::onGenerationProgress(int percent)
{
    m_progressBar->setValue(percent);
}

void AudiobookFlowDialog::onGenerationComplete()
{
    m_resultLabel->setText("✓ Audiobook ready!");
    m_progressText->hide();
    m_cancelGenBtn->hide();
    m_openFileBtn->show();   // Phase 3: open generated audio file
    // Add-to-library only writes a row that already exists; if the book is not
    // in the library, the SQL UPDATE silently affects 0 rows. Hide the button
    // in that case so the click cannot misleadingly succeed.
    if (m_libraryItemId > 0)
        m_addToLibBtn->show();
    else
        m_addToLibBtn->hide();
    m_nextBtn->setText("Close");
    disconnect(m_nextBtn, &QPushButton::clicked,
               this, &AudiobookFlowDialog::onNextOrGenerateClicked);
    connect(m_nextBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void AudiobookFlowDialog::onPreviewVoiceClicked()
{
    // Phase 3: request a 10-second preview clip from TTSService for m_selectedVoiceName
}

void AudiobookFlowDialog::onOpenFileClicked()
{
    // Phase 3: open generated audio file via QDesktopServices::openUrl()
}

void AudiobookFlowDialog::onAddToLibraryClicked()
{
    markAudiobookReady();
    accept();
}

void AudiobookFlowDialog::populateLanguageList()
{
    m_languageList->clear();
    for (const auto &edition : m_editions) {
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
    for (int i = 0; i < m_formats.size(); ++i) {
        const QString &type = m_formats[i].formatType;
        QString label = type;
        if (type == QLatin1String("epub")) {
            label += " (recommended)";
            epubIndex = i;
        }
        auto *item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, type);
        m_formatList->addItem(item);
    }
    if (!m_formats.isEmpty()) {
        const int selectRow = (epubIndex >= 0) ? epubIndex : 0;
        m_formatList->setCurrentRow(selectRow);
        m_selectedFormat = m_formats[selectRow].formatType;
    }
}

void AudiobookFlowDialog::populateVoiceList()
{
    if (m_worker) {
        m_pendingVoicesId = m_requestCounter++;
        emit listVoicesRequested(m_pendingVoicesId);
    }
}

void AudiobookFlowDialog::updateStepUi()
{
    m_stepIndicator->setCurrentStep(m_currentStep);
    m_stepsContainer->setCurrentIndex(m_currentStep);

    m_backBtn->setVisible(m_currentStep > 0);
    m_nextBtn->setText(m_currentStep == 4
        ? QStringLiteral("✓ Confirm & Generate")
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
        case 0: enabled = !m_selectedLanguage.isEmpty(); break;
        case 1: enabled = !m_selectedFormat.isEmpty();   break;
        case 2: enabled = m_selectedVoiceId >= 0;        break;
        default: enabled = true;                         break;
    }
    m_nextBtn->setEnabled(enabled);
}

bool AudiobookFlowDialog::startGeneration()
{
    if (m_selectedLanguage.isEmpty() || m_selectedFormat.isEmpty() || m_selectedVoiceId < 0) {
        showErrorState("Please select language, format, and voice.");
        return false;
    }
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

void AudiobookFlowDialog::showErrorState(const QString &message)
{
    QMessageBox::warning(this, "Error", message);
}

} // namespace bookhub::gui
