#include "download_flow_dialog.h"

#include "../query_worker.h"
#include "../services/library_service.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>

namespace bookhub::gui {

namespace {
constexpr auto kLastDownloadDirKey = "download/lastDirectory";
constexpr int kSourceNameRole = Qt::UserRole;
constexpr int kSourceUrlRole  = Qt::UserRole + 1;
constexpr auto kStatusDownloading = "downloading";
constexpr auto kStatusDownloaded = "downloaded";
constexpr auto kStatusError = "error";

QString formatSize(qint64 bytes) {
    if (bytes >= qint64{1024} * 1024)
        return QStringLiteral("%1 MB").arg(static_cast<double>(bytes) / (1024.0 * 1024), 0, 'f', 1);
    if (bytes >= 1024)
        return QStringLiteral("%1 KB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 bytes").arg(bytes);
}
}

DownloadFlowDialog::DownloadFlowDialog(LibraryService *libraryService,
                                       QueryWorker    *worker,
                                       QWidget        *parent)
    : QDialog(parent)
    , m_libraryService(libraryService)
{
    setModal(true);
    setWindowTitle(QStringLiteral("Download"));
    resize(520, 420);

    m_network = new QNetworkAccessManager(this);
    m_detailsService = new BookDetailsService(this);
    m_detailsService->connectToWorker(worker);

    buildUi();
    resetState();

    connect(m_detailsService, &BookDetailsService::detailsCompleted,
            this, &DownloadFlowDialog::onDetailsCompleted);
    connect(m_detailsService, &BookDetailsService::formatsCompleted,
            this, &DownloadFlowDialog::onFormatsCompleted);
}

void DownloadFlowDialog::startForBook(const QString &bookId)
{
    resetState();
    m_bookId = bookId;
    m_pendingDetailsId = m_detailsService->peekNextId();
    m_detailsService->requestBookDetails(bookId);
    exec();
}

void DownloadFlowDialog::onDetailsCompleted(quint64 requestId, const BookDetails &details)
{
    if (requestId != m_pendingDetailsId)
        return;

    m_bookTitle = details.title;
    m_libraryItemId = details.libraryItemId;
    m_editions = details.editions;
    m_hasLanguageStep = m_editions.size() > 1;
    m_titleLabel->setText(QStringLiteral("Download - %1").arg(m_bookTitle));

    populateLanguageList();

    if (m_editions.isEmpty()) {
        showErrorState(QStringLiteral("No language editions are available for this book."));
        return;
    }

    if (!m_hasLanguageStep) {
        m_currentStep = 1;
    } else {
        m_currentStep = 0;
    }
    updateStepUi();
}

void DownloadFlowDialog::onFormatsCompleted(quint64 requestId, QList<BookFormatEntry> formats)
{
    if (requestId != m_pendingFormatsId)
        return;
    m_formats = std::move(formats);
    populateFormatList();
    if (!m_hasLanguageStep && m_currentStep < 1) {
        m_currentStep = 1;
    }
    updateStepUi();
}

void DownloadFlowDialog::onLanguageSelectionChanged()
{
    if (m_languageList->currentRow() < 0)
        return;
    m_formats.clear();
    m_formatList->clear();
    m_sourceList->clear();
    requestFormatsForSelectedLanguage();
    updateStepUi();
}

void DownloadFlowDialog::onFormatSelectionChanged()
{
    populateSourceList();
    updateStepUi();
}

void DownloadFlowDialog::onSourceSelectionChanged()
{
    updateStepUi();
}

void DownloadFlowDialog::onBackClicked()
{
    if (m_currentStep <= (m_hasLanguageStep ? 0 : 1))
        return;
    --m_currentStep;
    updateStepUi();
}

void DownloadFlowDialog::onNextOrDownloadClicked()
{
    if (m_currentStep == 0) {
        if (m_languageList->currentRow() < 0)
            return;
        m_currentStep = 1;
    } else if (m_currentStep == 1) {
        if (m_formatList->currentRow() < 0)
            return;
        m_currentStep = 2;
    } else if (m_currentStep == 2) {
        if (m_sourceList->currentRow() < 0)
            return;
        beginDownload();
        return;
    }
    updateStepUi();
}

void DownloadFlowDialog::onCancelDownloadClicked()
{
    if (m_reply) {
        m_reply->abort();
        return;
    }
    reject();
}

void DownloadFlowDialog::onOpenFileClicked()
{
    if (!m_targetFilePath.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_targetFilePath));
}

void DownloadFlowDialog::onDownloadReadyRead()
{
    if (!m_reply || !m_outputFile)
        return;
    const QByteArray data = m_reply->readAll();
    if (m_outputFile->write(data) != data.size()) {
        showErrorState(QStringLiteral("Failed to write downloaded data to disk."));
        m_reply->abort();
    }
}

void DownloadFlowDialog::onDownloadProgress(qint64 received, qint64 total)
{
    if (total > 0) {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(static_cast<int>((received * 100) / total));
        m_progressText->setText(QStringLiteral("%1 / %2")
                                    .arg(formatSize(received))
                                    .arg(formatSize(total)));
    } else {
        m_progressBar->setRange(0, 0);
        m_progressText->setText(formatSize(received));
    }
}

void DownloadFlowDialog::onDownloadFinished()
{
    if (!m_reply)
        return;

    const bool hadError = m_reply->error() != QNetworkReply::NoError;
    const QString err = m_reply->errorString();
    m_reply->deleteLater();
    m_reply = nullptr;

    m_cancelBtn->setEnabled(false);

    if (hadError) {
        if (m_outputFile) {
            m_outputFile->cancelWriting();
            delete m_outputFile;
            m_outputFile = nullptr;
        }
        setLibraryStatus(kStatusError);
        showErrorState(QStringLiteral("Download failed: %1").arg(err));
        return;
    }

    if (!m_outputFile || !m_outputFile->commit()) {
        if (m_outputFile) {
            delete m_outputFile;
            m_outputFile = nullptr;
        }
        setLibraryStatus(kStatusError);
        showErrorState(QStringLiteral("Failed to finalize downloaded file."));
        return;
    }

    delete m_outputFile;
    m_outputFile = nullptr;

    QFileInfo info(m_targetFilePath);
    QSettings settings;
    settings.setValue(QString::fromLatin1(kLastDownloadDirKey), info.absolutePath());

    setLibraryStatus(kStatusDownloaded);
    m_resultLabel->setText(QStringLiteral("Download complete."));
    m_openFileBtn->setVisible(true);
    m_nextBtn->setText(QStringLiteral("Close"));
    disconnect(m_nextBtn, &QPushButton::clicked,
               this, &DownloadFlowDialog::onNextOrDownloadClicked);
    connect(m_nextBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void DownloadFlowDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    m_titleLabel = new QLabel(this);
    root->addWidget(m_titleLabel);

    m_stepLabel = new QLabel(this);
    root->addWidget(m_stepLabel);

    auto *stackHost = new QWidget(this);
    auto *stackLayout = new QVBoxLayout(stackHost);
    stackLayout->setContentsMargins(0, 0, 0, 0);
    stackLayout->setSpacing(0);
    root->addWidget(stackHost, 1);

    m_stepsContainer = new QWidget(stackHost);
    auto *stepsLayout = new QVBoxLayout(m_stepsContainer);

    m_languageList = new QListWidget(m_stepsContainer);
    m_languageList->setSelectionMode(QAbstractItemView::SingleSelection);
    stepsLayout->addWidget(m_languageList);
    connect(m_languageList, &QListWidget::itemSelectionChanged,
            this, &DownloadFlowDialog::onLanguageSelectionChanged);

    m_formatList = new QListWidget(m_stepsContainer);
    m_formatList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_formatList->hide();
    stepsLayout->addWidget(m_formatList);
    connect(m_formatList, &QListWidget::itemSelectionChanged,
            this, &DownloadFlowDialog::onFormatSelectionChanged);

    m_sourceList = new QListWidget(m_stepsContainer);
    m_sourceList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sourceList->hide();
    stepsLayout->addWidget(m_sourceList);
    connect(m_sourceList, &QListWidget::itemSelectionChanged,
            this, &DownloadFlowDialog::onSourceSelectionChanged);

    m_progressContainer = new QWidget(stackHost);
    auto *progressLayout = new QVBoxLayout(m_progressContainer);
    m_progressBar = new QProgressBar(m_progressContainer);
    m_progressText = new QLabel(m_progressContainer);
    m_progressPathLabel = new QLabel(m_progressContainer);
    m_resultLabel = new QLabel(m_progressContainer);
    m_resultLabel->setWordWrap(true);
    m_openFileBtn = new QPushButton(QStringLiteral("Open file"), m_progressContainer);
    m_openFileBtn->hide();
    connect(m_openFileBtn, &QPushButton::clicked, this, &DownloadFlowDialog::onOpenFileClicked);
    progressLayout->addWidget(m_progressBar);
    progressLayout->addWidget(m_progressText);
    progressLayout->addWidget(m_progressPathLabel);
    progressLayout->addWidget(m_resultLabel);
    progressLayout->addWidget(m_openFileBtn, 0, Qt::AlignLeft);
    progressLayout->addStretch();

    stackLayout->addWidget(m_stepsContainer);
    stackLayout->addWidget(m_progressContainer);

    auto *buttons = new QHBoxLayout();
    root->addLayout(buttons);

    m_backBtn = new QPushButton(QStringLiteral("Back"), this);
    m_nextBtn = new QPushButton(QStringLiteral("Next"), this);
    m_cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
    buttons->addWidget(m_backBtn);
    buttons->addStretch();
    buttons->addWidget(m_cancelBtn);
    buttons->addWidget(m_nextBtn);

    connect(m_backBtn, &QPushButton::clicked, this, &DownloadFlowDialog::onBackClicked);
    connect(m_nextBtn, &QPushButton::clicked, this, &DownloadFlowDialog::onNextOrDownloadClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &DownloadFlowDialog::onCancelDownloadClicked);
}

void DownloadFlowDialog::resetState()
{
    m_bookId.clear();
    m_bookTitle.clear();
    m_libraryItemId = 0;
    m_editions.clear();
    m_formats.clear();
    m_hasLanguageStep = false;
    m_currentStep = 0;
    m_targetFilePath.clear();
    m_languageList->clear();
    m_formatList->clear();
    m_sourceList->clear();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressText->setText(QStringLiteral("Waiting to start..."));
    m_progressPathLabel->clear();
    m_resultLabel->clear();
    m_openFileBtn->hide();
    m_cancelBtn->setEnabled(true);
    m_stepsContainer->show();
    m_progressContainer->hide();
    m_titleLabel->setText(QStringLiteral("Loading..."));
    m_nextBtn->setText(QStringLiteral("Next"));
    m_backBtn->setVisible(false);
    disconnect(m_nextBtn, &QPushButton::clicked, this, &QDialog::accept);
    disconnect(m_nextBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_nextBtn, &QPushButton::clicked, this, &DownloadFlowDialog::onNextOrDownloadClicked,
            Qt::UniqueConnection);
}

void DownloadFlowDialog::updateStepUi()
{
    m_stepsContainer->setVisible(true);
    m_progressContainer->setVisible(false);

    m_languageList->setVisible(m_currentStep == 0);
    m_formatList->setVisible(m_currentStep == 1);
    m_sourceList->setVisible(m_currentStep == 2);

    if (m_hasLanguageStep) {
        if (m_currentStep == 0) {
            m_stepLabel->setText(QStringLiteral("Step 1 of 3: Language"));
            m_nextBtn->setText(QStringLiteral("Next"));
            m_nextBtn->setEnabled(m_languageList->currentRow() >= 0);
        } else if (m_currentStep == 1) {
            m_stepLabel->setText(QStringLiteral("Step 2 of 3: Format"));
            m_nextBtn->setText(QStringLiteral("Next"));
            m_nextBtn->setEnabled(m_formatList->currentRow() >= 0);
        } else {
            m_stepLabel->setText(QStringLiteral("Step 3 of 3: Source"));
            m_nextBtn->setText(QStringLiteral("Download"));
            m_nextBtn->setEnabled(m_sourceList->currentRow() >= 0);
        }
    } else {
        if (m_currentStep == 1) {
            m_stepLabel->setText(QStringLiteral("Step 1 of 2: Format"));
            m_nextBtn->setText(QStringLiteral("Next"));
            m_nextBtn->setEnabled(m_formatList->currentRow() >= 0);
        } else {
            m_stepLabel->setText(QStringLiteral("Step 2 of 2: Source"));
            m_nextBtn->setText(QStringLiteral("Download"));
            m_nextBtn->setEnabled(m_sourceList->currentRow() >= 0);
        }
    }

    const int minStep = m_hasLanguageStep ? 0 : 1;
    m_backBtn->setVisible(m_currentStep > minStep);
}

void DownloadFlowDialog::populateLanguageList()
{
    m_languageList->clear();
    for (const auto &edition : m_editions) {
        auto *item = new QListWidgetItem(edition.language, m_languageList);
        item->setData(Qt::UserRole, edition.editionId);
    }
    if (!m_editions.isEmpty())
        m_languageList->setCurrentRow(0);
}

void DownloadFlowDialog::populateFormatList()
{
    m_formatList->clear();
    for (const auto &format : m_formats)
        m_formatList->addItem(format.formatType);
    if (m_formatList->count() > 0)
        m_formatList->setCurrentRow(0);
}

void DownloadFlowDialog::populateSourceList()
{
    m_sourceList->clear();
    if (m_formatList->currentRow() < 0 || m_formatList->currentRow() >= m_formats.size())
        return;
    const auto &format = m_formats.at(m_formatList->currentRow());
    for (const auto &source : format.sources) {
        auto *item = new QListWidgetItem(source.sourceName, m_sourceList);
        item->setData(kSourceNameRole, source.sourceName);
        item->setData(kSourceUrlRole, source.downloadLink);
    }
    if (m_sourceList->count() > 0)
        m_sourceList->setCurrentRow(0);
}

void DownloadFlowDialog::requestFormatsForSelectedLanguage()
{
    const auto *item = m_languageList->currentItem();
    if (!item)
        return;
    const int editionId = item->data(Qt::UserRole).toInt();
    m_pendingFormatsId = m_detailsService->peekNextId();
    m_detailsService->requestFormatsForEdition(editionId);
}

bool DownloadFlowDialog::beginDownload()
{
    const auto source = selectedSource();
    if (source.downloadLink.isEmpty())
        return false;

    QUrl downloadUrl(source.downloadLink);
    if (!downloadUrl.isValid() || downloadUrl.scheme() != QStringLiteral("https")) {
        showErrorState(QStringLiteral("Invalid or unsupported download URL."));
        return false;
    }

    const QString savePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Save file"),
        suggestedFilePath());
    if (savePath.isEmpty())
        return false;

    m_targetFilePath = savePath;
    m_outputFile = new QSaveFile(m_targetFilePath);
    if (!m_outputFile->open(QIODevice::WriteOnly)) {
        delete m_outputFile;
        m_outputFile = nullptr;
        showErrorState(QStringLiteral("Cannot open target file for writing."));
        return false;
    }

    m_stepsContainer->hide();
    m_progressContainer->show();
    m_stepLabel->setText(QStringLiteral("Downloading..."));
    m_progressPathLabel->setText(QStringLiteral("Saving to: %1").arg(m_targetFilePath));
    m_backBtn->setVisible(false);
    m_nextBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_openFileBtn->hide();
    m_resultLabel->clear();

    setLibraryStatus(kStatusDownloading);

    QNetworkRequest request(downloadUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &DownloadFlowDialog::onDownloadReadyRead);
    connect(m_reply, &QNetworkReply::downloadProgress, this, &DownloadFlowDialog::onDownloadProgress);
    connect(m_reply, &QNetworkReply::finished, this, &DownloadFlowDialog::onDownloadFinished);
    return true;
}

QString DownloadFlowDialog::suggestedFilePath() const
{
    QSettings settings;
    const QString dir = settings.value(QString::fromLatin1(kLastDownloadDirKey), QDir::homePath())
                            .toString();
    QString extension = QStringLiteral("bin");
    if (m_formatList->currentItem())
        extension = m_formatList->currentItem()->text().toLower();

    const QString fileName = QStringLiteral("%1.%2")
                                 .arg(sanitizeFileName(m_bookTitle))
                                 .arg(extension);
    return QDir(dir).filePath(fileName);
}

QString DownloadFlowDialog::sanitizeFileName(const QString &name) const
{
    QString cleaned = name.trimmed();
    cleaned.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")),
                    QStringLiteral("_"));
    if (cleaned.size() > 200)
        cleaned = cleaned.left(200);
    if (cleaned.isEmpty())
        cleaned = QStringLiteral("book");
    return cleaned;
}

void DownloadFlowDialog::showErrorState(const QString &message)
{
    m_stepsContainer->hide();
    m_progressContainer->show();
    m_stepLabel->setText(QStringLiteral("Error"));
    m_backBtn->setVisible(false);
    m_resultLabel->setText(message);
    m_nextBtn->setText(QStringLiteral("Close"));
    m_nextBtn->setEnabled(true);
    disconnect(m_nextBtn, &QPushButton::clicked,
               this, &DownloadFlowDialog::onNextOrDownloadClicked);
    connect(m_nextBtn, &QPushButton::clicked, this, &QDialog::reject, Qt::UniqueConnection);
}

void DownloadFlowDialog::setLibraryStatus(const QString &status)
{
    if (m_libraryItemId > 0)
        m_libraryService->requestUpdateStatus(m_libraryItemId, status);
}

BookSourceEntry DownloadFlowDialog::selectedSource() const
{
    const auto *item = m_sourceList->currentItem();
    if (!item)
        return {};
    BookSourceEntry src;
    src.sourceName = item->data(kSourceNameRole).toString();
    src.downloadLink = item->data(kSourceUrlRole).toString();
    return src;
}

} // namespace bookhub::gui
