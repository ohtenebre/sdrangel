#pragma once

#include <QByteArray>
#include <QString>

class Serializable;

struct GEOSCANDecoderSettings
{
    qint64 m_inputFrequencyOffset;
    float m_rfBandwidth;
    int m_streamIndex;
    int m_workspaceIndex;
    QString m_title;
    quint32 m_rgbColor;
    bool m_hidden;
    bool m_audioInput; // NEW: Режим аудио-входа
    QByteArray m_geometryBytes;

    Serializable *m_channelMarker;
    Serializable *m_rollupState;

    bool m_useReverseAPI;
    QString m_reverseAPIAddress;
    uint16_t m_reverseAPIPort;
    uint32_t m_reverseAPIDeviceIndex;
    uint32_t m_reverseAPIChannelIndex;

    QString m_csvLogFilename;
    bool m_csvLogEnabled;

    bool m_udpEnabled;
    QString m_udpAddress;
    int m_udpPort;
    int m_udpFormat;

    bool m_useFileTime;
    QString m_filterText;
    bool m_iqRecordEnabled;
    bool m_iqWavEnabled;

    bool m_lpfEnabled;
    float m_lpfCutoff;
    float m_lpfTransition;
    float m_lpfGain;

    bool m_aaLpfEnabled;
    float m_aaLpfCutoff;
    float m_aaLpfTransition;
    float m_aaLpfGain;

    bool m_resamplerEnabled;
    int m_resamplerInputRate;
    int m_resamplerOutputRate;

    int m_symSyncType;
    float m_symSyncSps;
    float m_symSyncLoopBw;
    float m_symSyncDamping;
    float m_symSyncTedGain;
    float m_symSyncMaxDev;

    bool m_mergeMode;
    int m_corrThreshold;

    float m_observerLat;
    float m_observerLon;
    float m_observerAltM;

    GEOSCANDecoderSettings();
    void resetToDefaults();
    QByteArray serialize() const;
    bool deserialize(const QByteArray &data);
    void applySettings(const QStringList &settingsKeys, const GEOSCANDecoderSettings &settings);
    QString getDebugString(const QStringList &settingsKeys, bool force = false) const;
};
