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
                QOverload<quint64, QList<VoiceEntry>>::of(&QueryWorker::listVoicesCompleted),
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
    m_voiceSelector = new VoiceSelectorWidget(this);
    connect(m_voiceSelector, &VoiceSelectorWidget::voiceSelected,
            this, &AudiobookFlowDialog::onVoiceSelectionChanged);
    connect(m_voiceSelector, &VoiceSelectorWidget::uploadNewVoiceRequested,
            this, &AudiobookFlowDialog::onVoiceUploadRequested);
    m_stepsContainer->addWidget(m_voiceSelector);

    // Step 4: Preview
    QWidget *previewStep = new QWidget(this);
    QVBoxLayout *previewLayout = new QVBoxLayout(previewStep);
    QLabel *previewLabel = new QLabel("Listening to: <voice name>", this);
    previewLayout->addWidget(previewLabel);
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
    connect(m_nextBtn, &QPushButton::clicked,
            this, &AudiobookFlowDialog::onNextOrGenerateClicked);
    navLayout->addWidget(m_nextBtn);
    mainLayout->addLayout(navLayout);

    setLayout(mainLayout);
}

void AudiobookFlowDialog::startForBook(const QString &bookId)
{
    m_bookId = bookId;
    resetState();
    m_pendingDetailsId = m_detailsService->requestBookDetails(bookId);
}

void AudiobookFlowDialog::resetState()
{
    m_currentStep = 0;
    m_selectedLanguage.clear();
    m_selectedFormat.clear();
    m_selectedVoiceId = -1;
    m_selectedVoiceName.clear();
    m_languageList->clear();
    m_formatList->clear();
    m_voiceSelector->setVoices({});
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
        m_pendingFormatsId = m_detailsService->requestFormatsForEdition(m_editions.first().editionId);
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
            m_pendingFormatsId = m_detailsService->requestFormatsForEdition(edition.editionId);
            break;
        }
    }
}

void AudiobookFlowDialog::onFormatSelectionChanged()
{
    QListWidgetItem *item = m_formatList->currentItem();
    if (!item)
        return;

    m_selectedFormat = item->text().split(" (")[0]; // strip "(recommended)" suffix
}

void AudiobookFlowDialog::onVoiceSelectionChanged(int voiceId, const QString &voiceName)
{
    m_selectedVoiceId = voiceId;
    m_selectedVoiceName = voiceName;
}

void AudiobookFlowDialog::onVoiceUploadRequested()
{
    m_uploadDialog = new VoiceUploadDialog(this);
    if (m_uploadDialog->exec() == QDialog::Accepted)
        populateVoiceList(); // refresh list after upload (DB insertion deferred to Phase 3)
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
    m_nextBtn->setText("Close");
    disconnect(m_nextBtn, &QPushButton::clicked,
               this, &AudiobookFlowDialog::onNextOrGenerateClicked);
    connect(m_nextBtn, &QPushButton::clicked, this, &QDialog::accept);
    setLibraryStatus(QStringLiteral("audiobook_ready"));
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
    for (const auto &format : m_formats) {
        QString label = format.formatType;
        if (format.formatType == QLatin1String("epub"))
            label += " (recommended)";
        m_formatList->addItem(new QListWidgetItem(label));
    }
    if (!m_formats.isEmpty()) {
        m_formatList->setCurrentRow(0);
        m_selectedFormat = m_formats.first().formatType;
    }
}

void AudiobookFlowDialog::populateVoiceList()
{
    if (m_worker) {
        m_pendingVoicesId = m_requestCounter.fetch_add(1);
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
}

bool AudiobookFlowDialog::startGeneration()
{
    if (m_selectedLanguage.isEmpty() || m_selectedFormat.isEmpty() || m_selectedVoiceId < 0) {
        showErrorState("Please select language, format, and voice.");
        return false;
    }
    return true;
}

void AudiobookFlowDialog::setLibraryStatus(const QString &)
{
    if (m_libraryItemId > 0 && m_libraryService)
        m_libraryService->requestSetAudiobookReady(m_bookId);
}

BookSourceEntry AudiobookFlowDialog::selectedSource() const
{
    if (!m_formats.isEmpty() && !m_formats.first().sources.isEmpty())
        return m_formats.first().sources.first();
    return BookSourceEntry{};
}

void AudiobookFlowDialog::showErrorState(const QString &message)
{
    QMessageBox::warning(this, "Error", message);
}

} // namespace bookhub::gui
