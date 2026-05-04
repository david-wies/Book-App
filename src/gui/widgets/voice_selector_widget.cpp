#include "voice_selector_widget.h"
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
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

    // Preset voices section
    QLabel *presetLabel = new QLabel("Preset voices", this);
    presetLabel->setStyleSheet("QLabel { font-weight: bold; font-size: 12px; margin-top: 8px; }");
    mainLayout->addWidget(presetLabel);

    m_presetList = new QListWidget(this);
    m_presetList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_presetList->setMaximumHeight(120);
    connect(m_presetList, &QListWidget::itemClicked, this, &VoiceSelectorWidget::onVoiceItemClicked);
    mainLayout->addWidget(m_presetList);

    // Custom voices section
    QLabel *customLabel = new QLabel("Custom voices", this);
    customLabel->setStyleSheet("QLabel { font-weight: bold; font-size: 12px; margin-top: 12px; }");
    mainLayout->addWidget(customLabel);

    m_customList = new QListWidget(this);
    m_customList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_customList->setMaximumHeight(100);
    connect(m_customList, &QListWidget::itemClicked, this, &VoiceSelectorWidget::onVoiceItemClicked);
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
        if (voice.id == m_selectedVoiceId) {
            return voice.name;
        }
    }
    return QString();
}

int VoiceSelectorWidget::selectedVoiceId() const
{
    return m_selectedVoiceId;
}

void VoiceSelectorWidget::onVoiceItemClicked()
{
    QListWidget *senderList = qobject_cast<QListWidget *>(sender());
    if (!senderList)
        senderList = m_presetList;

    // Clear selection on the other list
    if (senderList == m_presetList) {
        m_customList->clearSelection();
    } else {
        m_presetList->clearSelection();
    }

    QListWidgetItem *item = senderList->currentItem();
    if (!item)
        return;

    m_selectedVoiceId = item->data(Qt::UserRole).toInt();
    QString voiceName = item->text().split(" [")[0];
    emit voiceSelected(m_selectedVoiceId, voiceName);
}

void VoiceSelectorWidget::onPreviewButtonClicked()
{
    // This would be called by a button in the item widget
    // For now, placeholder
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
        QString displayText = voice.name + " [▶ Preview]";
        QListWidgetItem *item = new QListWidgetItem(displayText);
        item->setData(Qt::UserRole, voice.id);

        if (voice.isPreset) {
            m_presetList->addItem(item);
        } else {
            m_customList->addItem(item);
        }
    }
}

void VoiceSelectorWidget::updatePresetSection()
{
}

void VoiceSelectorWidget::updateCustomSection()
{
}

} // namespace bookhub::gui
