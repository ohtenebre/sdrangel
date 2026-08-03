#ifndef INCLUDE_GEOSCANDECODER_GUI_H
#define INCLUDE_GEOSCANDECODER_GUI_H

#include "channel/channelgui.h"
#include "dsp/channelmarker.h"
#include "dsp/dsptypes.h"
#include "geoscandecoderbaseband.h"
#include "geoscandecodersettings.h"
#include "util/messagequeue.h"
#include "tle_utils.hpp"

#include <QDir>
#include <QFile>
#include <QMap>
#include <QPixmap>
#include <QTextEdit>
#include <QLineEdit>
#include <QTreeWidget>
#include <QDialog>
#include <QTimer>
#include <QDoubleSpinBox>

class PluginAPI;
class DeviceUISet;
class BasebandSampleSink;
class GEOSCANDecoder;

namespace Ui
{
    class GEOSCANDecoderGUI;
}

class GEOSCANDecoderGUI : public ChannelGUI {
    Q_OBJECT
  public:
    static GEOSCANDecoderGUI *create(PluginAPI *pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSink *rxChannel);
    virtual void destroy();

    void resetToDefaults() override;
    QByteArray serialize() const override;
    bool deserialize(const QByteArray &data) override;
    virtual MessageQueue *getInputMessageQueue() override { return &m_inputMessageQueue; }
    virtual void setWorkspaceIndex(int index) override { m_settings.m_workspaceIndex = index; };
    virtual int getWorkspaceIndex() const override { return m_settings.m_workspaceIndex; };
    virtual void setGeometryBytes(const QByteArray &blob) override { m_settings.m_geometryBytes = blob; };
    virtual QByteArray getGeometryBytes() const override { return m_settings.m_geometryBytes; };
    virtual QString getTitle() const override { return m_settings.m_title; };
    virtual QColor getTitleColor() const override { return m_settings.m_rgbColor; };
    virtual void zetHidden(bool hidden) override { m_settings.m_hidden = hidden; }
    virtual bool getHidden() const override { return m_settings.m_hidden; }
    virtual ChannelMarker &getChannelMarker() override { return m_channelMarker; }
    virtual int getStreamIndex() const override { return m_settings.m_streamIndex; }
    virtual void setStreamIndex(int streamIndex) override { m_settings.m_streamIndex = streamIndex; }

  public slots:
    void channelMarkerChangedByCursor();
    void channelMarkerHighlightedByCursor();
  private:
    Ui::GEOSCANDecoderGUI *ui;
    PluginAPI *m_pluginAPI;
    DeviceUISet *m_deviceUISet;
    ChannelMarker m_channelMarker;
    GEOSCANDecoderSettings m_settings;
    qint64 m_deviceCenterFrequency;
    int m_basebandSampleRate;
    bool m_resamplerInputFollowsDevice = true;
    uint32_t m_tickCount;
    int m_frameTotal;
    int m_frameCrcOk;
    MessageQueue m_inputMessageQueue;
    GEOSCANDecoder *m_geoscandecoder; // Используем базовый тип для стабильности

    QTreeWidget *m_telemetryTree;
    QLineEdit *m_telemetrySearch;
    int m_packetNumber;

    explicit GEOSCANDecoderGUI(PluginAPI *pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSink *rxChannel, QWidget *parent = 0);
    virtual ~GEOSCANDecoderGUI();

    void applySettings(const QStringList &settingsKeys, bool force = false);
    void displaySettings();
    bool handleMessage(const Message &message);
    void makeUIConnections();
    void updateAbsoluteCenterFrequency();
    void writeTelemetryToCsv(const GEOSCANDecoderBaseband::TelemetryData &data);

    QFile m_csvFile;
    QFile m_textLogFile;
    GEOSCANDecoderBaseband::TelemetryData m_lastTelemetry;
    bool m_hasTelemetry;

    void highlightSearchMatches(bool full = false);
    void addTelemetryPacket(const GEOSCANDecoderBaseband::TelemetryData &data);
    void filterTelemetry(const QString &text);
    void displayImage();

    int m_lastHighlightedPosition = 0;
    QList<QTextEdit::ExtraSelection> m_extraSelections;
    QMap<uint16_t, QByteArray> m_imageBuffers;
    QMap<uint16_t, uint32_t> m_imageInstance;
    QMap<uint16_t, int> m_imageRxCount;
    QMap<uint16_t, int> m_imageChunkCount;
    QString m_sessionDir;
    void saveImage(uint16_t fileId, const QByteArray &data, uint32_t instance);

    void leaveEvent(QEvent *) override;
    void enterEvent(EnterEventType *) override;

    QWidget *m_dspSettingsWidget = nullptr;
    QDialog *m_dspDialog = nullptr;
    QLineEdit *m_satFreq = nullptr;
    QLineEdit *m_resultFreq = nullptr;

    std::vector<TleData> m_tleSatellites;
    TLE m_activeTle;
    bool m_tleActive = false;

    QTimer *m_dopplerTimer = nullptr;

    QDoubleSpinBox *m_observerLat = nullptr;
    QDoubleSpinBox *m_observerLon = nullptr;
    QDoubleSpinBox *m_observerAlt = nullptr;

  private slots:
    void on_deltaFrequency_changed(qint64 value);
    void on_satFreq_editingFinished();
    void onMenuDialogCalled(const QPoint &p);
    void handleInputMessages();
    void tick();
    void onTleButtonClicked();
    void onSatelliteChanged(int index);
    void onClearLog();
    void on_logFilename_clicked();
    void on_logEnable_toggled(bool checked);
    void on_logOpen_clicked();
    void on_udpEnabled_toggled(bool checked);
    void on_udpAddress_editingFinished();
    void on_udpPort_editingFinished();
    void on_udpFormat_currentIndexChanged(int index);
    void on_filterMMSI_textChanged(const QString &text);
    void on_telemetrySearch_textChanged(const QString &text);
    void on_useFileTime_toggled(bool checked);
    void on_textLogEnable_toggled(bool checked);
    void on_iqRecordEnable_toggled(bool checked);
    void on_iqRecordArm_toggled(bool checked);
    void on_iqWavRecord_toggled(bool checked);

    void setCtcssFreq(Real ctcssFreq);
    void setDcsCode(unsigned int dcsCode);

    void on_rfBW_valueChanged(int) {}
    void on_afBW_valueChanged(int) {}
    void on_fmDev_valueChanged(int) {}
    void on_volume_valueChanged(int) {}
    void on_squelchGate_valueChanged(int) {}
    void on_deltaSquelch_toggled(bool) {}
    void on_squelch_valueChanged(int) {}
    void on_ctcss_currentIndexChanged(int) {}
    void on_ctcssOn_toggled(bool) {}
    void on_dcsOn_toggled(bool) {}
    void on_dcsCode_currentIndexChanged(int) {}
    void on_highPassFilter_toggled(bool) {}
    void on_audioMute_toggled(bool) {}
    void on_channelSpacingApply_clicked() {}
    void onWidgetRolled(QWidget *, bool) {}
    void audioSelect(const QPoint &) {}

    void on_lpfCutoff_changed(double value);
    void on_lpfTransition_changed(double value);
    void on_lpfGain_changed(double value);
    void on_lpfEnabled_toggled(bool checked);

    void on_resamplerEnabled_toggled(bool checked);
    void on_resamplerInputRate_changed(double value);
    void on_resamplerOutputRate_changed(double value);

    void on_symSyncType_changed(int index);
    void on_symSyncSps_changed(double value);
    void on_symSyncLoopBw_changed(double value);
    void on_symSyncDamping_changed(double value);
    void on_symSyncTedGain_changed(double value);
    void on_symSyncMaxDev_changed(double value);
    void on_mergeMode_toggled(bool checked);
    void on_saveImageBtn_clicked();
    void on_threshold_valueChanged(int value);
    void onDspSettingsClicked();
    void updateDoppler();
};

#endif
