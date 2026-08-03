#include "gui/basicchannelsettingsdialog.h"
#include "gui/dialogpositioner.h"
#include "dsp/dspcommands.h"
#include "device/deviceuiset.h"
#include "geoscandecoder.h"
#include "geoscandecoder_gui.h"
#include "geoscandecoderbaseband.h"
#include "maincore.h"
#include "plugin/pluginapi.h"
#include "ui_geoscandecoder.h"
#include "util/db.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QTimeZone>
#include <QDebug>
#include <QFileDialog>
#include <QHeaderView>
#include <QImage>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextStream>
#include <QVBoxLayout>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QBoxLayout>
#include <cstdio>
#include <cstring>

GEOSCANDecoderGUI *GEOSCANDecoderGUI::create(PluginAPI *pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSink *rxChannel)
{
    return new GEOSCANDecoderGUI(pluginAPI, deviceUISet, rxChannel);
}

void GEOSCANDecoderGUI::destroy()
{
    delete this;
}

void GEOSCANDecoderGUI::resetToDefaults()
{
    ui->tleFileEdit->clear();
    ui->satelliteCombo->clear();
    ui->satelliteCombo->addItem("-- выберите спутник --");
    ui->satelliteCombo->setEnabled(false);
    ui->deltaFrequency->setEnabled(true);
    displaySettings();
}

QByteArray GEOSCANDecoderGUI::serialize() const
{
    return m_settings.serialize();
}
bool GEOSCANDecoderGUI::deserialize(const QByteArray &data)
{
    m_settings.deserialize(data);
    displaySettings();
    return true;
}
void GEOSCANDecoderGUI::channelMarkerChangedByCursor()
{
    ui->deltaFrequency->setValue(m_channelMarker.getCenterFrequency());
}

void GEOSCANDecoderGUI::on_deltaFrequency_changed(qint64 value)
{
    qint64 sat = m_satFreq->text().toLongLong();
    qint64 offset = (sat - m_deviceCenterFrequency) + value;
    m_channelMarker.setCenterFrequency(offset);
    m_settings.m_inputFrequencyOffset = offset;
    m_resultFreq->setText(QString::number(sat + value));
    updateAbsoluteCenterFrequency();
    applySettings(QStringList("inputFrequencyOffset"));
}

void GEOSCANDecoderGUI::on_satFreq_editingFinished()
{
    qint64 sat = m_satFreq->text().toLongLong();
    qint64 df = ui->deltaFrequency->getValue();
    qint64 offset = (sat - m_deviceCenterFrequency) + df;
    m_channelMarker.setCenterFrequency(offset);
    m_settings.m_inputFrequencyOffset = offset;
    m_resultFreq->setText(QString::number(sat + df));
    updateAbsoluteCenterFrequency();
    applySettings(QStringList("inputFrequencyOffset"));
}

void GEOSCANDecoderGUI::onMenuDialogCalled(const QPoint &p)
{
    if (m_contextMenuType == ContextMenuType::ContextMenuChannelSettings)
    {
        BasicChannelSettingsDialog dialog(&m_channelMarker, this);
        dialog.move(p);
        new DialogPositioner(&dialog, false);
        dialog.exec();
        m_settings.m_rgbColor = m_channelMarker.getColor().rgb();
        m_settings.m_title = m_channelMarker.getTitle();
        setWindowTitle(m_settings.m_title);
        setTitle(m_channelMarker.getTitle());
        setTitleColor(m_settings.m_rgbColor);
    }
    resetContextMenuType();
}

GEOSCANDecoderGUI::GEOSCANDecoderGUI(PluginAPI *pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSink *rxChannel, QWidget *parent) : ChannelGUI(parent),
                                                                                                                                       ui(new Ui::GEOSCANDecoderGUI),
                                                                                                                                       m_pluginAPI(pluginAPI),
                                                                                                                                       m_deviceUISet(deviceUISet),
                                                                                                                                       m_channelMarker(this),
                                                                                                                                       m_deviceCenterFrequency(0),
                                                                                                                                       m_basebandSampleRate(1),
                                                                                                                                       m_tickCount(0),
                                                                                                                                       m_frameTotal(0),
                                                                                                                                       m_frameCrcOk(0),
                                                                                                                                       m_packetNumber(0),
                                                                                                                                       m_hasTelemetry(false)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    m_helpURL = "plugins/channelrx/geoscandecoder/readme.md";
    RollupContents *rollupContents = getRollupContents();
    ui->setupUi(rollupContents);

    // Удаляем ненужную вкладку AIS
    ui->tabWidget->removeTab(1);
    // Переименовываем ADS-B в Telemetry
    ui->tabWidget->setTabText(0, "Телеметрия");

    // Создаем дерево телеметрии
    m_telemetryTree = new QTreeWidget();
    m_telemetryTree->setHeaderLabels({ "Пакет", "Значение" });
    m_telemetryTree->header()->setSectionResizeMode(QHeaderView::Stretch);
    m_telemetryTree->setAlternatingRowColors(true);
    m_telemetryTree->setAnimated(true);

    // Поиск по телеметрии
    m_telemetrySearch = new QLineEdit();
    m_telemetrySearch->setPlaceholderText("Поиск по телеметрии...");
    m_telemetrySearch->setClearButtonEnabled(true);
    connect(m_telemetrySearch, &QLineEdit::textChanged, this, &GEOSCANDecoderGUI::on_telemetrySearch_textChanged);

    // Вставляем дерево в существующий виджет ADS_B (теперь Telemetry)
    if (!ui->ADS_B->layout())
    {
        QVBoxLayout *layout = new QVBoxLayout(ui->ADS_B);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(m_telemetrySearch);
        layout->addWidget(m_telemetryTree);
    }

    m_geoscandecoder = reinterpret_cast<GEOSCANDecoder *>(rxChannel);
    m_geoscandecoder->setMessageQueueToGUI(getInputMessageQueue());

    m_channelMarker.setColor(Qt::red);
    m_channelMarker.setTitle("GEOSCAN SATELLITES DECODER");
    m_channelMarker.setCenterFrequency(ui->deltaFrequency->getValue());
    m_channelMarker.setVisible(true);
    m_deviceUISet->addChannelMarker(&m_channelMarker);
    connect(&m_channelMarker, SIGNAL(changedByCursor()), this, SLOT(channelMarkerChangedByCursor()));
    connect(&m_channelMarker, SIGNAL(highlightedByCursor()), this, SLOT(channelMarkerHighlightedByCursor()));

    ui->deltaFrequency->setValueRange(false, 7, -9999999, 9999999);
    ui->logEnable->setCheckable(true);
    ui->useFileTime->setCheckable(true);

    // Спутник и результат — программно в powLayout перед Df
    m_satFreq = new QLineEdit();
    m_satFreq->setFixedWidth(100);
    m_satFreq->setPlaceholderText("Частота спутника");
    m_resultFreq = new QLineEdit();
    m_resultFreq->setReadOnly(true);
    m_resultFreq->setFixedWidth(100);

    auto *sep1 = new QFrame();
    sep1->setFrameShape(QFrame::VLine);
    auto *sep2 = new QFrame();
    sep2->setFrameShape(QFrame::VLine);

    auto *satLabel = new QLabel("Спутник");
    auto *eqLabel = new QLabel("=");

    ui->powLayout->insertWidget(0, satLabel);
    ui->powLayout->insertWidget(1, m_satFreq);
    ui->powLayout->insertWidget(2, sep1);
    ui->powLayout->insertWidget(5, eqLabel);
    ui->powLayout->insertWidget(6, m_resultFreq);
    ui->powLayout->insertWidget(7, sep2);

    m_sessionDir = QString("/tmp/geoscandecoder/%1").arg(QCoreApplication::applicationPid());
    QDir().mkpath(m_sessionDir);

    // Observer coordinates + TLE frequency — inserted after tleFileEdit
    {
        QLayout *parentLayout = ui->tleFileEdit->parentWidget()->layout();
        int insertIdx = parentLayout->indexOf(ui->tleFileEdit) + 1;

        auto *obsLayout = new QHBoxLayout();
        auto *latLabel = new QLabel("Lat:");
        m_observerLat = new QDoubleSpinBox();
        m_observerLat->setRange(-90.0, 90.0);
        m_observerLat->setDecimals(4);
        m_observerLat->setSingleStep(0.01);
        m_observerLat->setSuffix("\u00b0");
        auto *lonLabel = new QLabel("Lon:");
        m_observerLon = new QDoubleSpinBox();
        m_observerLon->setRange(-180.0, 180.0);
        m_observerLon->setDecimals(4);
        m_observerLon->setSingleStep(0.01);
        m_observerLon->setSuffix("\u00b0");
        auto *altLabel = new QLabel("Alt:");
        m_observerAlt = new QDoubleSpinBox();
        m_observerAlt->setRange(-500.0, 90000.0);
        m_observerAlt->setDecimals(0);
        m_observerAlt->setSingleStep(10.0);
        m_observerAlt->setSuffix(" m");

        obsLayout->addWidget(latLabel);
        obsLayout->addWidget(m_observerLat);
        obsLayout->addWidget(lonLabel);
        obsLayout->addWidget(m_observerLon);
        obsLayout->addWidget(altLabel);
        obsLayout->addWidget(m_observerAlt);

        if (auto *boxLayout = qobject_cast<QBoxLayout *>(parentLayout))
        {
            boxLayout->insertLayout(insertIdx, obsLayout);
        }
        else
        {
            parentLayout->addItem(obsLayout);
        }
    }

    // Doppler timer
    m_dopplerTimer = new QTimer(this);
    m_dopplerTimer->setInterval(1000);
    connect(m_dopplerTimer, &QTimer::timeout, this, &GEOSCANDecoderGUI::updateDoppler);

    connect(m_observerLat, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        m_settings.m_observerLat = m_observerLat->value();
        applySettings(QStringList("observerLat"));
    });
    connect(m_observerLon, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        m_settings.m_observerLon = m_observerLon->value();
        applySettings(QStringList("observerLon"));
    });
    connect(m_observerAlt, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        m_settings.m_observerAltM = m_observerAlt->value();
        applySettings(QStringList("observerAltM"));
    });

    displaySettings();
    makeUIConnections();

    // DSP settings: extract groupBox_3 from main layout, create dialog
    m_dspSettingsWidget = ui->groupBox_3;
    ui->verticalLayout_3->removeWidget(m_dspSettingsWidget);

    m_dspDialog = new QDialog(this);
    m_dspDialog->setWindowTitle("Настройки ЦОС");
    m_dspDialog->setMinimumSize(300, 400);
    m_dspDialog->hide();

    auto *dspLayout = new QVBoxLayout(m_dspDialog);
    dspLayout->setContentsMargins(4, 4, 4, 4);
    m_dspSettingsWidget->setParent(m_dspDialog);
    dspLayout->addWidget(m_dspSettingsWidget);
    m_dspSettingsWidget->show();

    connect(ui->dspSettingsButton, &QPushButton::clicked, this, &GEOSCANDecoderGUI::onDspSettingsClicked);

    connect(getInputMessageQueue(), &MessageQueue::messageEnqueued, this, &GEOSCANDecoderGUI::handleInputMessages);
    connect(&MainCore::instance()->getMasterTimer(), SIGNAL(timeout()), this, SLOT(tick()));
}

GEOSCANDecoderGUI::~GEOSCANDecoderGUI()
{
    m_geoscandecoder->setMessageQueueToGUI(nullptr);
    delete m_dspDialog;
    for (auto it = m_imageBuffers.begin(); it != m_imageBuffers.end(); ++it)
    {
        if (!it.value().isEmpty())
        {
            uint32_t inst = m_settings.m_mergeMode ? 0 : m_imageInstance.value(it.key()) + 1;
            saveImage(it.key(), it.value(), inst);
        }
    }
    delete ui;
}

void GEOSCANDecoderGUI::displaySettings()
{
    ui->deltaFrequency->blockSignals(true);
    ui->deltaFrequency->setValue(m_channelMarker.getCenterFrequency());
    ui->deltaFrequency->blockSignals(false);
    ui->logFilename->setToolTip(QString(".csv log filename: %1").arg(m_settings.m_csvLogFilename));
    ui->logEnable->setChecked(m_settings.m_csvLogEnabled);
    ui->udpEnabled->setChecked(m_settings.m_udpEnabled);
    ui->udpAddress->setText(m_settings.m_udpAddress);
    ui->udpPort->setText(QString::number(m_settings.m_udpPort));
    ui->udpFormat->setCurrentIndex(m_settings.m_udpFormat);
    ui->useFileTime->setChecked(m_settings.m_useFileTime);
    ui->iqWavRecord->setChecked(m_settings.m_iqWavEnabled);
    ui->filterMMSI->setText(m_settings.m_filterText);
    ui->filterCutoff->setValue(m_settings.m_lpfCutoff);
    ui->filterTransition->setValue(m_settings.m_lpfTransition);
    ui->filterGain->setValue(m_settings.m_lpfGain);
    ui->groupBox->setChecked(m_settings.m_lpfEnabled);
    ui->filterCutoff->setEnabled(m_settings.m_lpfEnabled);
    ui->filterTransition->setEnabled(m_settings.m_lpfEnabled);
    ui->filterGain->setEnabled(m_settings.m_lpfEnabled);
    ui->aaFilterGroup->setChecked(m_settings.m_aaLpfEnabled);
    ui->aaFilterCutoff->setEnabled(m_settings.m_aaLpfEnabled);
    ui->aaFilterTransition->setEnabled(m_settings.m_aaLpfEnabled);
    ui->aaFilterGain->setEnabled(m_settings.m_aaLpfEnabled);
    ui->aaFilterCutoff->setValue(m_settings.m_aaLpfCutoff);
    ui->aaFilterTransition->setValue(m_settings.m_aaLpfTransition);
    ui->aaFilterGain->setValue(m_settings.m_aaLpfGain);
    ui->groupBox_2->setChecked(m_settings.m_resamplerEnabled);
    ui->resamplerInputRate->setValue(m_settings.m_resamplerInputRate);
    ui->resamplerOutputRate->setValue(m_settings.m_resamplerOutputRate);
    ui->symSyncType->setCurrentIndex(m_settings.m_symSyncType);
    ui->symSyncSps->setValue(m_settings.m_symSyncSps);
    ui->symSyncLoopBw->setValue(m_settings.m_symSyncLoopBw);
    ui->symSyncDamping->setValue(m_settings.m_symSyncDamping);
    ui->symSyncTedGain->setValue(m_settings.m_symSyncTedGain);
    ui->symSyncMaxDev->setValue(m_settings.m_symSyncMaxDev);
    ui->mergeMode->setChecked(m_settings.m_mergeMode);
    ui->saveImageBtn->setVisible(m_settings.m_mergeMode);
    ui->threshold->setValue(m_settings.m_corrThreshold);
    ui->thresholdText->setText(QString::number(m_settings.m_corrThreshold));
    if (m_observerLat) m_observerLat->setValue(m_settings.m_observerLat);
    if (m_observerLon) m_observerLon->setValue(m_settings.m_observerLon);
    if (m_observerAlt) m_observerAlt->setValue(m_settings.m_observerAltM);
    updateAbsoluteCenterFrequency();
    highlightSearchMatches();
}

void GEOSCANDecoderGUI::makeUIConnections()
{
    QObject::connect(ui->deltaFrequency, &ValueDialZ::changed, this, &GEOSCANDecoderGUI::on_deltaFrequency_changed);
    QObject::connect(m_satFreq, &QLineEdit::editingFinished, this, &GEOSCANDecoderGUI::on_satFreq_editingFinished);
    QObject::connect(ui->tleButton, &QPushButton::clicked, this, &GEOSCANDecoderGUI::onTleButtonClicked);
    QObject::connect(ui->satelliteCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GEOSCANDecoderGUI::onSatelliteChanged);
    QObject::connect(ui->clearTable, &QPushButton::clicked, this, &GEOSCANDecoderGUI::onClearLog);
    QObject::connect(ui->logFilename, &QToolButton::clicked, this, &GEOSCANDecoderGUI::on_logFilename_clicked);
    QObject::connect(ui->logEnable, &ButtonSwitch::clicked, this, &GEOSCANDecoderGUI::on_logEnable_toggled);
    QObject::connect(ui->logOpen, &QToolButton::clicked, this, &GEOSCANDecoderGUI::on_logOpen_clicked);
    QObject::connect(ui->udpEnabled, &QCheckBox::toggled, this, &GEOSCANDecoderGUI::on_udpEnabled_toggled);
    QObject::connect(ui->udpAddress, &QLineEdit::editingFinished, this, &GEOSCANDecoderGUI::on_udpAddress_editingFinished);
    QObject::connect(ui->udpPort, &QLineEdit::editingFinished, this, &GEOSCANDecoderGUI::on_udpPort_editingFinished);
    QObject::connect(ui->udpFormat, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GEOSCANDecoderGUI::on_udpFormat_currentIndexChanged);
    QObject::connect(ui->filterMMSI, &QLineEdit::textChanged, this, &GEOSCANDecoderGUI::on_filterMMSI_textChanged);
    QObject::connect(ui->useFileTime, &ButtonSwitch::toggled, this, &GEOSCANDecoderGUI::on_useFileTime_toggled);
    QObject::connect(ui->textLogEnable, &ButtonSwitch::toggled, this, &GEOSCANDecoderGUI::on_textLogEnable_toggled);
    QObject::connect(ui->iqRecordEnable, &ButtonSwitch::toggled, this, &GEOSCANDecoderGUI::on_iqRecordEnable_toggled);
    QObject::connect(ui->iqRecordArm, &ButtonSwitch::toggled, this, &GEOSCANDecoderGUI::on_iqRecordArm_toggled);
    QObject::connect(ui->iqWavRecord, &ButtonSwitch::toggled, this, &GEOSCANDecoderGUI::on_iqWavRecord_toggled);
    QObject::connect(ui->filterCutoff, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &GEOSCANDecoderGUI::on_lpfCutoff_changed);
    QObject::connect(ui->filterTransition, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &GEOSCANDecoderGUI::on_lpfTransition_changed);
    QObject::connect(ui->filterGain, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &GEOSCANDecoderGUI::on_lpfGain_changed);
    QObject::connect(ui->groupBox, &QGroupBox::toggled, this, &GEOSCANDecoderGUI::on_lpfEnabled_toggled);
    QObject::connect(ui->aaFilterCutoff, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &GEOSCANDecoderGUI::on_aaLpfCutoff_changed);
    QObject::connect(ui->aaFilterTransition, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &GEOSCANDecoderGUI::on_aaLpfTransition_changed);
    QObject::connect(ui->aaFilterGain, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &GEOSCANDecoderGUI::on_aaLpfGain_changed);
    QObject::connect(ui->aaFilterGroup, &QGroupBox::toggled, this, &GEOSCANDecoderGUI::on_aaLpfEnabled_toggled);
    QObject::connect(ui->groupBox_2, &QGroupBox::toggled, this, &GEOSCANDecoderGUI::on_resamplerEnabled_toggled);
    QObject::connect(ui->resamplerInputRate, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &GEOSCANDecoderGUI::on_resamplerInputRate_changed);
    QObject::connect(ui->resamplerOutputRate, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &GEOSCANDecoderGUI::on_resamplerOutputRate_changed);
    QObject::connect(ui->symSyncType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GEOSCANDecoderGUI::on_symSyncType_changed);
    QObject::connect(ui->symSyncSps, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &GEOSCANDecoderGUI::on_symSyncSps_changed);
    QObject::connect(ui->symSyncLoopBw, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &GEOSCANDecoderGUI::on_symSyncLoopBw_changed);
    QObject::connect(ui->symSyncDamping, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &GEOSCANDecoderGUI::on_symSyncDamping_changed);
    QObject::connect(ui->symSyncTedGain, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &GEOSCANDecoderGUI::on_symSyncTedGain_changed);
    QObject::connect(ui->symSyncMaxDev, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &GEOSCANDecoderGUI::on_symSyncMaxDev_changed);
    QObject::connect(ui->mergeMode, &QCheckBox::toggled, this, &GEOSCANDecoderGUI::on_mergeMode_toggled);
    QObject::connect(ui->saveImageBtn, &QPushButton::clicked, this, &GEOSCANDecoderGUI::on_saveImageBtn_clicked);
    QObject::connect(ui->threshold, &QDial::valueChanged, this, &GEOSCANDecoderGUI::on_threshold_valueChanged);
}

void GEOSCANDecoderGUI::on_lpfCutoff_changed(double value)
{
    m_settings.m_lpfCutoff = value;
    applySettings(QStringList("lpfCutoff"));
}

void GEOSCANDecoderGUI::on_lpfTransition_changed(double value)
{
    m_settings.m_lpfTransition = value;
    applySettings(QStringList("lpfTransition"));
}

void GEOSCANDecoderGUI::on_lpfGain_changed(double value)
{
    m_settings.m_lpfGain = value;
    applySettings(QStringList("lpfGain"));
}

void GEOSCANDecoderGUI::on_lpfEnabled_toggled(bool checked)
{
    m_settings.m_lpfEnabled = checked;
    ui->filterCutoff->setEnabled(checked);
    ui->filterTransition->setEnabled(checked);
    ui->filterGain->setEnabled(checked);
    applySettings(QStringList("lpfEnabled"));
}

void GEOSCANDecoderGUI::on_aaLpfCutoff_changed(double value)
{
    m_settings.m_aaLpfCutoff = value;
    m_aaLpfFollowsDevice = (value == m_basebandSampleRate / 8.0);
    applySettings(QStringList("aaLpfCutoff"));
}

void GEOSCANDecoderGUI::on_aaLpfTransition_changed(double value)
{
    m_settings.m_aaLpfTransition = value;
    m_aaLpfFollowsDevice = (value == m_basebandSampleRate / 8.0);
    applySettings(QStringList("aaLpfTransition"));
}

void GEOSCANDecoderGUI::on_aaLpfGain_changed(double value)
{
    m_settings.m_aaLpfGain = value;
    applySettings(QStringList("aaLpfGain"));
}

void GEOSCANDecoderGUI::on_aaLpfEnabled_toggled(bool checked)
{
    m_settings.m_aaLpfEnabled = checked;
    ui->aaFilterCutoff->setEnabled(checked);
    ui->aaFilterTransition->setEnabled(checked);
    ui->aaFilterGain->setEnabled(checked);
    applySettings(QStringList("aaLpfEnabled"));
}

void GEOSCANDecoderGUI::updateAbsoluteCenterFrequency()
{
    setStatusFrequency(m_deviceCenterFrequency + m_settings.m_inputFrequencyOffset);
}
void GEOSCANDecoderGUI::onTleButtonClicked()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("\u0412\u044b\u0431\u0435\u0440\u0438\u0442\u0435 TLE \u0444\u0430\u0439\u043b"), "", tr("TLE files (*.txt *.tle);;All files (*)"));
    if (fileName.isEmpty())
        return;

    ui->tleFileEdit->setText(fileName);

    std::vector<TleData> sats = tle_load_file(fileName.toStdString());
    if (sats.empty())
    {
        QMessageBox::warning(this, tr("TLE"), tr("\u0424\u0430\u0439\u043b \u043d\u0435 \u0441\u043e\u0434\u0435\u0440\u0436\u0430\u0442 TLE \u0434\u0430\u043d\u043d\u044b\u0435"));
        return;
    }

    m_tleSatellites = sats;
    ui->satelliteCombo->blockSignals(true);
    ui->satelliteCombo->clear();
    ui->satelliteCombo->addItem("-- \u0432\u044b\u0431\u0435\u0440\u0438\u0442\u0435 \u0441\u043f\u0443\u0442\u043d\u0438\u043a --");
    for (const auto& sat : m_tleSatellites)
    {
        ui->satelliteCombo->addItem(QString::fromStdString(sat.name));
    }
    ui->satelliteCombo->setCurrentIndex(0);
    ui->satelliteCombo->setEnabled(true);
    ui->satelliteCombo->blockSignals(false);

    auto [ok, msg] = tle_check_freshness(sats[0].line1);
    qWarning().noquote() << "[TLE]" << QString::fromStdString(msg);
}
void GEOSCANDecoderGUI::onSatelliteChanged(int index)
{
    if (index <= 0 || (size_t)index > m_tleSatellites.size())
    {
        m_tleActive = false;
        m_dopplerTimer->stop();
        ui->deltaFrequency->setEnabled(true);
        return;
    }

    const TleData& sat = m_tleSatellites[index - 1];
    char line1[70] = {}, line2[70] = {};
    std::strncpy(line1, sat.line1.c_str(), 69);
    std::strncpy(line2, sat.line2.c_str(), 69);
    m_activeTle.parseLines(line1, line2);
    m_tleActive = true;

    qWarning().noquote() << "[TLE] Selected:" << QString::fromStdString(sat.name)
                         << "NORAD" << QString::fromStdString(sat.norad_id);

    m_dopplerTimer->start();
    updateDoppler();
}
void GEOSCANDecoderGUI::updateDoppler()
{
    if (!m_tleActive)
        return;

    bool ok = false;
    double freq_hz = m_satFreq->text().toDouble(&ok);
    if (!ok || freq_hz <= 0)
        return;

    auto now = std::chrono::system_clock::now();
    std::time_t now_tt = std::chrono::system_clock::to_time_t(now);
    std::tm* utc = std::gmtime(&now_tt);

    int y = utc->tm_year + 1900;
    int m = utc->tm_mon + 1;
    int d = utc->tm_mday;
    int h = utc->tm_hour;
    int mi = utc->tm_min;
    double s = utc->tm_sec;

    int a = (m - 14) / 12;
    int y2 = y + 4800 - a;
    int m2 = m + 12 * a - 3;
    double jd = d + (153 * m2 + 2) / 5 + 365 * y2 + y2 / 4 - y2 / 100 + y2 / 400 - 32045;
    double jd_frac = (h + mi / 60.0 + s / 3600.0) / 24.0 - 0.5;
    jd += jd_frac;

    double obs_alt_km = m_settings.m_observerAltM / 1000.0;

    SatelliteState state = tle_calculate_state(
        m_activeTle,
        m_settings.m_observerLat,
        m_settings.m_observerLon,
        obs_alt_km,
        jd,
        freq_hz
    );

    qint64 doppler_offset = (qint64)std::round(state.doppler_hz);

    qWarning().noquote() << "[TLE] Doppler:" << QString::number(state.doppler_hz, 'f', 1) << "Hz"
                         << "Elev:" << QString::number(state.elevation_deg, 'f', 1)
                         << (state.visible ? "VIS" : "NLOS");

    m_settings.m_inputFrequencyOffset = doppler_offset;
    applySettings(QStringList("inputFrequencyOffset"));
    ui->deltaFrequency->setValue(doppler_offset);
}

void GEOSCANDecoderGUI::tick()
{
    double magsqAvg, magsqPeak;
    int nbSamples;
    m_geoscandecoder->getMagSqLevels(magsqAvg, magsqPeak, nbSamples);
    ui->channelPowerMeter->levelChanged((100.0f + CalcDb::dbPower(magsqAvg)) / 100.0f, (100.0f + CalcDb::dbPower(magsqPeak)) / 100.0f, nbSamples);
    if (++m_tickCount % 4 == 0)
        ui->channelPower->setText(QString("%1").arg(CalcDb::dbPower(magsqAvg), 0, 'f', 1));
}

void GEOSCANDecoderGUI::addTelemetryPacket(const GEOSCANDecoderBaseband::TelemetryData &d)
{
    m_packetNumber++;

    auto addRow = [](QTreeWidgetItem *parent, const QString &name, const QString &value) {
        auto *item = new QTreeWidgetItem(parent);
        item->setText(0, name);
        item->setText(1, value);
    };

    QDateTime dt = m_settings.m_useFileTime
        ? QDateTime::fromSecsSinceEpoch(d.timestamp, QTimeZone::utc())
        : QDateTime::currentDateTime();

    QString summary = QString("#%1 | %2 | %3 → %4")
        .arg(m_packetNumber)
        .arg(dt.toString("HH:mm:ss"))
        .arg(d.sourceCallsign)
        .arg(d.destinationCallsign);

    auto *root = new QTreeWidgetItem(m_telemetryTree);
    root->setText(0, summary);
    root->setExpanded(false);

    // AX.25
    auto *ax25 = new QTreeWidgetItem(root);
    ax25->setText(0, "AX.25");
    addRow(ax25, "Источник", d.sourceCallsign);
    addRow(ax25, "Назначение", d.destinationCallsign);
    addRow(ax25, "Управление", QString("0x%1").arg(d.control, 2, 16, QChar('0')));
    addRow(ax25, "PID", QString("0x%1").arg(d.pid, 2, 16, QChar('0')));
    addRow(ax25, "ID Маяк", QString::number(d.mayakId));

    // Time
    auto *timeItem = new QTreeWidgetItem(root);
    timeItem->setText(0, "Время");
    addRow(timeItem, "Unix", QString::number(d.timestamp));
    addRow(timeItem, "МСК", dt.toString("yyyy-MM-dd HH:mm:ss"));

    // EPS
    auto *eps = new QTreeWidgetItem(root);
    eps->setText(0, "ЭСБ");
    addRow(eps, "Режим", QString::number(d.epsMode));
    addRow(eps, "Ток нагрузки", QString("%1 мА").arg(d.currentLoadMa));
    addRow(eps, "Ток солнца", QString("%1 мА").arg(d.currentSolarMa));
    addRow(eps, "Батарея всего", QString("%1 мВ").arg(d.voltageBattSumMv));
    addRow(eps, "Батарея ячейка 1", QString("%1 мВ").arg(d.voltageBattOneMv));

    // Temperature
    auto *temp = new QTreeWidgetItem(root);
    temp->setText(0, "Температура");
    addRow(temp, "Батарея 1", QString("%1 °C").arg(d.tempBatt1));
    addRow(temp, "Батарея 2", QString("%1 °C").arg(d.tempBatt2));
    addRow(temp, "X+", QString("%1 °C").arg(d.tempXPlus));
    addRow(temp, "X-", QString("%1 °C").arg(d.tempXMinus));
    addRow(temp, "Y+", QString("%1 °C").arg(d.tempYPlus));
    addRow(temp, "Y-", QString("%1 °C").arg(d.tempYMinus));

    // OBC
    auto *obc = new QTreeWidgetItem(root);
    obc->setText(0, "БЦВ");
    addRow(obc, "Активность", QString::number(d.obcActivity));
    addRow(obc, "Спутники GNSS", QString::number(d.gnssCount));
    addRow(obc, "Файлы камеры", QString::number(d.mediaFilesCount));

    // Radio
    auto *radio = new QTreeWidgetItem(root);
    radio->setText(0, "Радио");
    addRow(radio, "Напряжение VBUS", QString("%1 мВ").arg(d.vbusVoltageMv));
    addRow(radio, "RSSI последний", QString("%1 дБм").arg(d.rssiLast));
    addRow(radio, "RSSI минимум", QString("%1 дБм").arg(d.rssiMin));
    addRow(radio, "Пакетов отправлено", QString::number(d.packetsSent));
    addRow(radio, "QSO принято", QString::number(d.qsoReceived));

    m_telemetryTree->scrollToBottom();
}

bool GEOSCANDecoderGUI::handleMessage(const Message &msg)
{
    if (GEOSCANDecoder::MsgConfigureGEOSCANDecoder::match(msg))
    {
        const auto &cfg = (const GEOSCANDecoder::MsgConfigureGEOSCANDecoder &)msg;
        m_settings = cfg.getSettings();
        displaySettings();
        return true;
    }
    if (DSPSignalNotification::match(msg))
    {
        const auto &notif = (const DSPSignalNotification &)msg;
        m_deviceCenterFrequency = notif.getCenterFrequency();
        m_basebandSampleRate = notif.getSampleRate();
        ui->deltaFrequency->setValueRange(false, 7, -m_basebandSampleRate / 2, m_basebandSampleRate / 2);
        if (m_resamplerInputFollowsDevice)
        {
            m_settings.m_resamplerInputRate = (int)m_basebandSampleRate;
            ui->resamplerInputRate->setValue(m_settings.m_resamplerInputRate);
            applySettings(QStringList("resamplerInputRate"));
        }
        if (m_aaLpfFollowsDevice)
        {
            m_settings.m_aaLpfCutoff = m_basebandSampleRate / 8.0f;
            m_settings.m_aaLpfTransition = m_basebandSampleRate / 8.0f;
            ui->aaFilterCutoff->setValue(m_settings.m_aaLpfCutoff);
            ui->aaFilterTransition->setValue(m_settings.m_aaLpfTransition);
            applySettings(QStringList({"aaLpfCutoff", "aaLpfTransition"}));
        }
        m_satFreq->setText(QString::number(m_deviceCenterFrequency));
        on_satFreq_editingFinished();
        return true;
    }
    if (GEOSCANDecoderBaseband::MsgPacketFound::match(msg))
    {
        const auto &m = (const GEOSCANDecoderBaseband::MsgPacketFound &)msg;
        m_frameTotal++;
        if (m.getCrcOk())
            m_frameCrcOk++;
        QString line = QString("[%1] %2\n%3\n").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(m.getCrcOk() ? "✓ CRC OK" : "✗ CRC FAIL").arg(m.getHex());
        ui->rawText->appendPlainText(line);

        {
            QTextDocument *doc = ui->rawText->document();
            const int maxBlocks = 5000;
            if (doc->blockCount() > maxBlocks)
            {
                QTextCursor cursor(doc);
                cursor.movePosition(QTextCursor::Start);
                cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor, doc->blockCount() - maxBlocks);
                cursor.removeSelectedText();
                m_lastHighlightedPosition = 0;
            }
        }

        setStatusText(QString("Синхр: %1 | CRC OK: %2").arg(m_frameTotal).arg(m_frameCrcOk));
        highlightSearchMatches(false);
        return true;
    }
    if (GEOSCANDecoderBaseband::MsgTelemetry::match(msg))
    {
        const auto &m = (const GEOSCANDecoderBaseband::MsgTelemetry &)msg;
        const auto &d = m.getData();

        m_lastTelemetry = d;
        m_hasTelemetry = true;

        if (m_settings.m_csvLogEnabled)
            writeTelemetryToCsv(d);

        addTelemetryPacket(d);
        return true;
    }
    if (GEOSCANDecoderBaseband::MsgDebugText::match(msg))
    {
        const auto &m = (const GEOSCANDecoderBaseband::MsgDebugText &)msg;
        ui->textLog->appendPlainText(m.getText());
        // Запись в файл если лог включён
        if (m_textLogFile.isOpen())
        {
            m_textLogFile.write(m.getText().toUtf8());
            m_textLogFile.write("\n");
            m_textLogFile.flush();
        }
        return true;
    }
    if (GEOSCANDecoderBaseband::MsgImageData::match(msg))
    {
        const auto &m = (const GEOSCANDecoderBaseband::MsgImageData &)msg;
        uint16_t fileId = m.getFileId();
        uint32_t offset = m.getOffset();
        const QByteArray &data = m.getData();

        if (offset == 0)
        {
            m_imageRxCount[fileId] = 0;
            m_imageChunkCount[fileId] = 0;
        }

        if (m_settings.m_mergeMode)
        {
            // merge mode: сохраняем при начале нового прохода, но НЕ очищаем буфер
            QByteArray &buf = m_imageBuffers[fileId];
            if (offset == 0 && !buf.isEmpty())
                saveImage(fileId, buf, 0);
        }
        else
        {
            uint32_t &inst = m_imageInstance[fileId];
            QByteArray &buf = m_imageBuffers[fileId];
            if (offset == 0)
            {
                inst++;
                if (!buf.isEmpty())
                    buf.clear();
            }
        }

        QByteArray &buf = m_imageBuffers[fileId];
        if ((uint32_t)buf.size() < offset + (uint32_t)data.size())
            buf.resize(offset + data.size());
        memcpy(buf.data() + offset, data.constData(), data.size());

        m_imageRxCount[fileId] += data.size();
        m_imageChunkCount[fileId]++;

        if (m_settings.m_mergeMode)
        {
            if (m_imageChunkCount[fileId] % 10 == 0)
                displayImage();
        }
        else
        {
            uint32_t saveInst = m_imageInstance[fileId];
            saveImage(fileId, buf, saveInst);
            displayImage();
        }

        return true;
    }
    return false;
}

void GEOSCANDecoderGUI::displayImage()
{
    if (m_imageBuffers.isEmpty())
        return;
    auto it = m_imageBuffers.begin();
    uint16_t fileId = it.key();
    const QByteArray &buf = it.value();
    if (buf.isEmpty())
    {
        ui->imageLabel->setText("Принимаемое изображение");
        return;
    }
    QImage img;
    if (img.loadFromData(buf))
    {
        ui->imageLabel->setPixmap(QPixmap::fromImage(img).scaled(
            ui->imageLabel->width(), ui->imageLabel->height(),
            Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    else
    {
        int rx = m_imageRxCount.value(fileId);
        int total = buf.size();
        if (m_settings.m_mergeMode && rx < total)
            ui->imageLabel->setText(QString("Принимаемое изображение\n%1 байт (всего %2)").arg(rx).arg(total));
        else
            ui->imageLabel->setText(QString("Принимаемое изображение\n%1 байт").arg(rx));
    }
}

void GEOSCANDecoderGUI::saveImage(uint16_t fileId, const QByteArray &data, uint32_t instance)
{
    QString path;
    if (instance > 0)
        path = QString("%1/image_%2_%3.jpg").arg(m_sessionDir).arg(fileId).arg(instance);
    else
        path = QString("%1/image_%2.jpg").arg(m_sessionDir).arg(fileId);

    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
    {
        f.write(data);
        f.close();
    }
}

void GEOSCANDecoderGUI::on_mergeMode_toggled(bool checked)
{
    m_settings.m_mergeMode = checked;
    ui->saveImageBtn->setVisible(checked);
    applySettings(QStringList("mergeMode"));
}

void GEOSCANDecoderGUI::on_saveImageBtn_clicked()
{
    if (m_imageBuffers.isEmpty())
        return;
    auto it = m_imageBuffers.constBegin();
    for (; it != m_imageBuffers.constEnd(); ++it)
        saveImage(it.key(), it.value(), 0);
    qWarning() << "[GEOSCAN] Изображение сохранено вручную";
}

void GEOSCANDecoderGUI::on_threshold_valueChanged(int value)
{
    m_settings.m_corrThreshold = value;
    ui->thresholdText->setText(QString::number(value));
    applySettings(QStringList("corrThreshold"));
}

void GEOSCANDecoderGUI::onDspSettingsClicked()
{
    if (m_dspDialog->isVisible())
        m_dspDialog->hide();
    else
        m_dspDialog->show();
}

void GEOSCANDecoderGUI::onClearLog()
{
    ui->rawText->clear();
    ui->textLog->clear();
    m_hasTelemetry = false;
    m_frameTotal = 0;
    m_frameCrcOk = 0;
    m_extraSelections.clear();
    m_lastHighlightedPosition = 0;
    for (auto it = m_imageBuffers.begin(); it != m_imageBuffers.end(); ++it)
    {
        if (!it.value().isEmpty())
        {
            uint32_t inst = m_settings.m_mergeMode ? 0 : m_imageInstance.value(it.key()) + 1;
            saveImage(it.key(), it.value(), inst);
        }
    }
    m_imageBuffers.clear();
    m_imageRxCount.clear();
    ui->imageLabel->clear();
    ui->imageLabel->setText("Принимаемое изображение");
    setStatusText("Синхр: 0 | CRC OK: 0");
    if (m_telemetryTree)
        m_telemetryTree->clear();
    m_packetNumber = 0;
}

void GEOSCANDecoderGUI::handleInputMessages()
{
    Message *m;
    while ((m = getInputMessageQueue()->pop()))
    {
        if (handleMessage(*m))
            delete m;
    }
}
void GEOSCANDecoderGUI::channelMarkerHighlightedByCursor() {}
void GEOSCANDecoderGUI::leaveEvent(QEvent *) {}
void GEOSCANDecoderGUI::enterEvent(EnterEventType *) {}
void GEOSCANDecoderGUI::setCtcssFreq(Real) {}
void GEOSCANDecoderGUI::setDcsCode(unsigned int) {}

void GEOSCANDecoderGUI::on_textLogEnable_toggled(bool checked)
{
    // Эта кнопка только выбирает файл — не начинает запись
    // Снимаем галку сразу, она не должна оставаться нажатой
    ui->textLogEnable->blockSignals(true);
    ui->textLogEnable->setChecked(false);
    ui->textLogEnable->blockSignals(false);

    if (!checked)
        return; // toggled(false) не интересует

    QString fname = QFileDialog::getSaveFileName(this, tr("Выберите файл для лога текста"), "", tr("Text files (*.txt);;All files (*)"));
    if (fname.isEmpty())
        return;

    // Закрываем старый файл если был открыт
    if (m_textLogFile.isOpen())
        m_textLogFile.close();
    m_textLogFile.setFileName(fname);
    qWarning() << "[GEOSCAN] Файл для текстового лога выбран:" << fname;
}

void GEOSCANDecoderGUI::on_iqRecordEnable_toggled(bool checked)
{
    if (checked)
    {
        // Проверяем что файл выбран
        if (m_textLogFile.fileName().isEmpty())
        {
            QMessageBox::warning(this, "Text Log", "Сначала выберите файл (кнопка слева).");
            ui->iqRecordEnable->blockSignals(true);
            ui->iqRecordEnable->setChecked(false);
            ui->iqRecordEnable->blockSignals(false);
            return;
        }
        if (!m_textLogFile.isOpen())
        {
            if (!m_textLogFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
            {
                qWarning() << "[GEOSCAN] Не удалось открыть файл для записи.";
                ui->iqRecordEnable->blockSignals(true);
                ui->iqRecordEnable->setChecked(false);
                ui->iqRecordEnable->blockSignals(false);
                return;
            }
        }
        qWarning() << "[GEOSCAN] Текстовый лог запущен:" << m_textLogFile.fileName();
    }
    else
    {
        if (m_textLogFile.isOpen())
            m_textLogFile.close();
        qWarning() << "[GEOSCAN] Текстовый лог остановлен.";
    }
}
void GEOSCANDecoderGUI::on_iqRecordArm_toggled(bool checked)
{
    m_settings.m_iqRecordEnabled = checked;
    applySettings({ "iqRecordEnabled" });
    qWarning() << "[GEOSCAN] IQ запись" << (checked ? "взведена (ждём sync)" : "снята");
}
void GEOSCANDecoderGUI::on_iqWavRecord_toggled(bool checked)
{
    m_settings.m_iqWavEnabled = checked;
    applySettings({ "iqWavEnabled" });
    qWarning() << "[GEOSCAN] WAV IQ" << (checked ? "включена" : "выключена");
}
void GEOSCANDecoderGUI::applySettings(const QStringList &settingsKeys, bool force)
{
    GEOSCANDecoder::MsgConfigureGEOSCANDecoder *message = GEOSCANDecoder::MsgConfigureGEOSCANDecoder::create(settingsKeys, m_settings, force);
    m_geoscandecoder->getInputMessageQueue()->push(message);
}

void GEOSCANDecoderGUI::on_logFilename_clicked()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("Select CSV log file to save telemetry"), m_settings.m_csvLogFilename, tr("CSV files (*.csv);;All files (*)"));
    if (!fileName.isEmpty())
    {
        m_settings.m_csvLogFilename = fileName;
        ui->logFilename->setToolTip(QString(".csv log filename: %1").arg(m_settings.m_csvLogFilename));
        applySettings(QStringList("csvLogFilename"));
    }
}

void GEOSCANDecoderGUI::on_logEnable_toggled(bool checked)
{
    m_settings.m_csvLogEnabled = checked;
    if (checked)
    {
        if (m_settings.m_csvLogFilename.isEmpty())
        {
            QMessageBox::warning(this, tr("GEOSCAN Decoder"), tr("Сначала выберите имя файла лога."));
            ui->logEnable->setChecked(false);
            m_settings.m_csvLogEnabled = false;
            return;
        }
        m_csvFile.setFileName(m_settings.m_csvLogFilename);
        bool exists = m_csvFile.exists();
        if (m_csvFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append))
        {
            if (!exists || m_csvFile.size() == 0)
            {
                QTextStream out(&m_csvFile);
                out << "Timestamp,DateTime,Source,Destination,MayakID,Control,PID,"
                    << "EpsMode,CurrentLoad_mA,CurrentSolar_mA,VoltageBattOne_mV,VoltageBattSum_mV,"
                    << "TempBatt1,TempBatt2,TempXPlus,TempXMinus,TempYPlus,TempYMinus,"
                    << "ObcActivity,GnssCount,MediaFilesCount,"
                    << "VbusVoltage_mV,RssiLast,RssiMin,PacketsSent,QsoReceived\n";
            }
        }
        else
        {
            QMessageBox::critical(this, tr("GEOSCAN Decoder"), tr("Не удалось открыть файл для записи: %1").arg(m_settings.m_csvLogFilename));
            ui->logEnable->setChecked(false);
            m_settings.m_csvLogEnabled = false;
        }
    }
    else
    {
        if (m_csvFile.isOpen())
        {
            m_csvFile.close();
        }
    }
    applySettings(QStringList("csvLogEnabled"));
}

void GEOSCANDecoderGUI::writeTelemetryToCsv(const GEOSCANDecoderBaseband::TelemetryData &data)
{
    if (m_csvFile.isOpen())
    {
        QTextStream out(&m_csvFile);
        QString dateTimeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        out << data.timestamp << ","
            << dateTimeStr << ","
            << data.sourceCallsign << ","
            << data.destinationCallsign << ","
            << data.mayakId << ","
            << QString("0x%1").arg(data.control, 2, 16, QChar('0')) << ","
            << QString("0x%1").arg(data.pid, 2, 16, QChar('0')) << ","
            << data.epsMode << ","
            << data.currentLoadMa << ","
            << data.currentSolarMa << ","
            << data.voltageBattOneMv << ","
            << data.voltageBattSumMv << ","
            << data.tempBatt1 << ","
            << data.tempBatt2 << ","
            << data.tempXPlus << ","
            << data.tempXMinus << ","
            << data.tempYPlus << ","
            << data.tempYMinus << ","
            << data.obcActivity << ","
            << data.gnssCount << ","
            << data.mediaFilesCount << ","
            << data.vbusVoltageMv << ","
            << data.rssiLast << ","
            << data.rssiMin << ","
            << data.packetsSent << ","
            << data.qsoReceived << "\n";
        m_csvFile.flush();
    }
}

void GEOSCANDecoderGUI::on_logOpen_clicked()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Select CSV log file to read"), "", tr("CSV files (*.csv);;All files (*)"));
    if (!fileName.isEmpty())
    {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            onClearLog();
            QTextStream in(&file);
            QString headerLine = in.readLine(); // Read header

            int count = 0;
            while (!in.atEnd())
            {
                QString line = in.readLine();
                QStringList fields = line.split(',');
                if (fields.size() >= 8)
                {
                    GEOSCANDecoderBaseband::TelemetryData d;

                    d.timestamp = fields[0].toUInt();
                    d.sourceCallsign = fields.size() >= 4 ? fields[2] : "";
                    d.destinationCallsign = fields.size() >= 4 ? fields[3] : "";
                    d.mayakId = fields.size() >= 5 ? fields[4].toUInt() : 0;
                    d.control = fields.size() >= 6 ? fields[5].toUShort(nullptr, 0) : 0;
                    d.pid = fields.size() >= 7 ? fields[6].toUShort(nullptr, 0) : 0;
                    d.epsMode = fields.size() >= 8 ? fields[7].toUInt() : 0;

                    if (fields.size() >= 9) d.currentLoadMa = fields[8].toUInt();
                    if (fields.size() >= 10) d.currentSolarMa = fields[9].toUInt();
                    if (fields.size() >= 11) d.voltageBattOneMv = fields[10].toUInt();
                    if (fields.size() >= 12) d.voltageBattSumMv = fields[11].toUInt();
                    if (fields.size() >= 13) d.tempBatt1 = fields[12].toShort();
                    if (fields.size() >= 14) d.tempBatt2 = fields[13].toShort();
                    if (fields.size() >= 15) d.tempXPlus = fields[14].toShort();
                    if (fields.size() >= 16) d.tempXMinus = fields[15].toShort();
                    if (fields.size() >= 17) d.tempYPlus = fields[16].toShort();
                    if (fields.size() >= 18) d.tempYMinus = fields[17].toShort();
                    if (fields.size() >= 19) d.obcActivity = fields[18].toUInt();
                    if (fields.size() >= 20) d.gnssCount = fields[19].toUInt();
                    if (fields.size() >= 21) d.mediaFilesCount = fields[20].toUInt();
                    if (fields.size() >= 22) d.vbusVoltageMv = fields[21].toUInt();
                    if (fields.size() >= 23) d.rssiLast = fields[22].toShort();
                    if (fields.size() >= 24) d.rssiMin = fields[23].toShort();
                    if (fields.size() >= 25) d.packetsSent = fields[24].toUInt();
                    if (fields.size() >= 26) d.qsoReceived = fields[25].toUInt();

                    m_lastTelemetry = d;
                    m_hasTelemetry = true;

                    addTelemetryPacket(d);

                    QString logLine = QString("[%1] IMPORT: Timestamp=%2, Source=%3, Dest=%4\n")
                                          .arg(fields.size() >= 2 ? fields[1] : "")
                                          .arg(d.timestamp)
                                          .arg(d.sourceCallsign)
                                          .arg(d.destinationCallsign);
                    ui->rawText->appendPlainText(logLine);
                    count++;
                }
            }
            highlightSearchMatches();
            setStatusText(tr("Импортировано %1 пакетов").arg(count));
        }
        else
        {
            QMessageBox::critical(this, tr("GEOSCAN Decoder"), tr("Не удалось открыть файл %1").arg(fileName));
        }
    }
}

void GEOSCANDecoderGUI::on_udpEnabled_toggled(bool checked)
{
    m_settings.m_udpEnabled = checked;
    applySettings(QStringList("udpEnabled"));
}

void GEOSCANDecoderGUI::on_udpAddress_editingFinished()
{
    m_settings.m_udpAddress = ui->udpAddress->text().trimmed();
    applySettings(QStringList("udpAddress"));
}

void GEOSCANDecoderGUI::on_udpPort_editingFinished()
{
    bool ok;
    int port = ui->udpPort->text().toInt(&ok);
    if (ok && port > 0 && port < 65536)
    {
        m_settings.m_udpPort = port;
        applySettings(QStringList("udpPort"));
    }
    else
    {
        ui->udpPort->setText(QString::number(m_settings.m_udpPort));
    }
}

void GEOSCANDecoderGUI::on_udpFormat_currentIndexChanged(int index)
{
    m_settings.m_udpFormat = index;
    applySettings(QStringList("udpFormat"));
}

void GEOSCANDecoderGUI::highlightSearchMatches(bool full)
{
    QString searchString = ui->filterMMSI->text().trimmed();
    QTextDocument *document = ui->rawText->document();

    if (full || searchString.isEmpty())
    {
        m_extraSelections.clear();
        m_lastHighlightedPosition = 0;
    }

    if (searchString.isEmpty())
    {
        ui->rawText->setExtraSelections(m_extraSelections);
        return;
    }

    int docLength = document->characterCount();
    if (m_lastHighlightedPosition >= docLength)
    {
        ui->rawText->setExtraSelections(m_extraSelections);
        return;
    }

    QTextCharFormat highlightFormat;
    highlightFormat.setBackground(Qt::yellow);
    highlightFormat.setForeground(Qt::black);

    QRegularExpression regex(searchString, QRegularExpression::CaseInsensitiveOption);

    int overlap = qMin(m_lastHighlightedPosition, searchString.length());
    QTextCursor cursor(document);
    cursor.setPosition(m_lastHighlightedPosition - overlap);

    while (true)
    {
        cursor = document->find(regex, cursor);
        if (cursor.isNull())
            break;
        QTextEdit::ExtraSelection selection;
        selection.format = highlightFormat;
        selection.cursor = cursor;
        m_extraSelections.append(selection);
    }

    m_lastHighlightedPosition = docLength;
    ui->rawText->setExtraSelections(m_extraSelections);
}

void GEOSCANDecoderGUI::filterTelemetry(const QString &text)
{
    QString search = text.trimmed().toLower();

    for (int i = 0; i < m_telemetryTree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *root = m_telemetryTree->topLevelItem(i);

        if (search.isEmpty())
        {
            root->setHidden(false);
            continue;
        }

        bool match = false;

        // Проверяем текст корневого элемента (summary: #N | time | src → dst)
        if (root->text(0).toLower().contains(search) || root->text(1).toLower().contains(search))
            match = true;

        // Проверяем всех потомков рекурсивно
        if (!match)
        {
            QList<QTreeWidgetItem *> children = root->takeChildren();
            root->addChildren(children); // restore

            for (int c = 0; c < root->childCount() && !match; ++c)
            {
                QTreeWidgetItem *section = root->child(c);
                if (section->text(0).toLower().contains(search) || section->text(1).toLower().contains(search))
                {
                    match = true;
                    break;
                }
                for (int j = 0; j < section->childCount() && !match; ++j)
                {
                    QTreeWidgetItem *leaf = section->child(j);
                    if (leaf->text(0).toLower().contains(search) || leaf->text(1).toLower().contains(search))
                        match = true;
                }
            }
        }

        root->setHidden(!match);
        if (match)
            root->setExpanded(false);
    }
}

void GEOSCANDecoderGUI::on_telemetrySearch_textChanged(const QString &text)
{
    filterTelemetry(text);
}

void GEOSCANDecoderGUI::on_filterMMSI_textChanged(const QString &text)
{
    m_settings.m_filterText = text;
    highlightSearchMatches(true);
    applySettings(QStringList("filterText"));
}

void GEOSCANDecoderGUI::on_useFileTime_toggled(bool checked)
{
    m_settings.m_useFileTime = checked;
    applySettings(QStringList("useFileTime"));
}

void GEOSCANDecoderGUI::on_resamplerEnabled_toggled(bool checked)
{
    m_settings.m_resamplerEnabled = checked;
    applySettings(QStringList("resamplerEnabled"));
}

void GEOSCANDecoderGUI::on_resamplerInputRate_changed(double value)
{
    m_settings.m_resamplerInputRate = (int)value;
    m_resamplerInputFollowsDevice = ((int)value == m_basebandSampleRate);
    applySettings(QStringList("resamplerInputRate"));
}

void GEOSCANDecoderGUI::on_resamplerOutputRate_changed(double value)
{
    m_settings.m_resamplerOutputRate = (int)value;
    applySettings(QStringList("resamplerOutputRate"));
}

void GEOSCANDecoderGUI::on_symSyncType_changed(int)
{
}

void GEOSCANDecoderGUI::on_symSyncSps_changed(double value)
{
    m_settings.m_symSyncSps = (float)value;
    applySettings(QStringList("symSyncSps"));
}

void GEOSCANDecoderGUI::on_symSyncLoopBw_changed(double value)
{
    m_settings.m_symSyncLoopBw = (float)value;
    applySettings(QStringList("symSyncLoopBw"));
}

void GEOSCANDecoderGUI::on_symSyncDamping_changed(double value)
{
    m_settings.m_symSyncDamping = (float)value;
    applySettings(QStringList("symSyncDamping"));
}

void GEOSCANDecoderGUI::on_symSyncTedGain_changed(double value)
{
    m_settings.m_symSyncTedGain = (float)value;
    applySettings(QStringList("symSyncTedGain"));
}

void GEOSCANDecoderGUI::on_symSyncMaxDev_changed(double value)
{
    m_settings.m_symSyncMaxDev = (float)value;
    applySettings(QStringList("symSyncMaxDev"));
}
