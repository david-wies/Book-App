#include "voice_selector_widget.h"
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>

namespace bookhub::gui {

VoiceSelectorWidget::VoiceSelectorWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void VoiceSelectorWidget::buildUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QLabel *presetLabel = new QLabel("Preset voices", this);
    presetLabel->setStyleSheet("QLabel { font-weight: bold; font-size: 12px; margin-top: 8px; }");
    mainLayout->addWidget(presetLabel);

    m_presetList = new QListWidget(this);
    m_presetList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_presetList->setMaximumHeight(120);
    connect(m_presetList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) {
                m_customList->clearSelection();
                m_selectedVoiceId = item->data(Qt::UserRole).toInt();
                emit voiceSelected(m_selectedVoiceId, item->text());
            });
    mainLayout->addWidget(m_presetList);

    QLabel *customLabel = new QLabel("Custom voices", this);
    customLabel->setStyleSheet("QLabel { font-weight: bold; font-size: 12px; margin-top: 12px; }");
    mainLayout->addWidget(customLabel);

    m_customList = new QListWidget(this);
    m_customList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_customList->setMaximumHeight(100);
    connect(m_customList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) {
                m_presetList->clearSelection();
                m_selectedVoiceId = item->data(Qt::UserRole).toInt();
                emit voiceSelected(m_selectedVoiceId, item->text());
            });
    mainLayout->addWidget(m_customList);

    m_uploadBtn = new QPushButton("+ Upload new voice sample", this);
    m_uploadBtn->setMaximumWidth(200);
    connect(m_uploadBtn, &QPushButton::clicked, this, &VoiceSelectorWidget::onUploadClicked);
    mainLayout->addWidget(m_uploadBtn);

    mainLayout->addStretch();
    setMaximumHeight(400);
}

void VoiceSelectorWidget::setVoices(const QList<VoiceEntry> &voices)
{
    m_voices = voices;
    populateVoiceList();
}

QString VoiceSelectorWidget::selectedVoiceName() const
{
    for (const auto &voice : m_voices) {
        if (voice.id == m_selectedVoiceId)
            return voice.name;
    }
    return QString();
}

int VoiceSelectorWidget::selectedVoiceId() const
{
    return m_selectedVoiceId;
}

void VoiceSelectorWidget::onUploadClicked()
{
    emit uploadNewVoiceRequested();
}

void VoiceSelectorWidget::populateVoiceList()
{
    m_presetList->clear();
    m_customList->clear();

    for (const auto &voice : m_voices) {
        QListWidgetItem *item = new QListWidgetItem(voice.name);
        item->setData(Qt::UserRole, voice.id);

        if (voice.isPreset)
            m_presetList->addItem(item);
        else
            m_customList->addItem(item);
    }
}

} // namespace bookhub::gui
