#include "geoscandecodersettings.h"
#include "util/simpleserializer.h"

#include <QColor>

GEOSCANDecoderSettings::GEOSCANDecoderSettings() : m_channelMarker(nullptr),
                                                   m_rollupState(nullptr)
{
    resetToDefaults();
}

void GEOSCANDecoderSettings::resetToDefaults()
{
    m_inputFrequencyOffset = 0;
    m_rfBandwidth = 12500.0f;
    m_streamIndex = 0;
    m_workspaceIndex = 0;
    m_title = "GEOSCAN Decoder";
    m_rgbColor = QColor(255, 0, 0).rgb();
    m_hidden = false;
    m_audioInput = false;
    m_geometryBytes.clear();
    m_useReverseAPI = false;
    m_reverseAPIAddress = "127.0.0.1";
    m_reverseAPIPort = 8888;
    m_reverseAPIDeviceIndex = 0;
    m_reverseAPIChannelIndex = 0;
    m_csvLogFilename = "";
    m_csvLogEnabled = false;
    m_udpEnabled = false;
    m_udpAddress = "127.0.0.1";
    m_udpPort = 9999;
    m_udpFormat = 0;
    m_useFileTime = true;
    m_filterText = "";
    m_iqRecordEnabled = false;
    m_iqWavEnabled = false;
    m_lpfEnabled = true;
    m_lpfCutoff = 7000.0f;
    m_lpfTransition = 2000.0f;
    m_lpfGain = 1.f;
    m_aaLpfEnabled = true;
    m_aaLpfCutoff = 6000.0f;
    m_aaLpfTransition = 6000.0f;
    m_aaLpfGain = 1.0f;
    m_resamplerEnabled = true;
    m_resamplerInputRate = 2000000;
    m_resamplerOutputRate = 48000;
    m_symSyncType = 0;
    m_symSyncSps = 5.0f;
    m_symSyncLoopBw = 0.2f;
    m_symSyncDamping = 1.0f;
    m_symSyncTedGain = 1.0f;
    m_symSyncMaxDev = 0.00001f;
    m_mergeMode = true;
    m_corrThreshold = 0;
    m_observerLat = 0.0f;
    m_observerLon = 0.0f;
    m_observerAltM = 0.0f;
}

QByteArray GEOSCANDecoderSettings::serialize() const
{
    SimpleSerializer s(1);
    s.writeS64(1, m_inputFrequencyOffset);
    s.writeReal(2, m_rfBandwidth);
    s.writeS32(3, m_streamIndex);
    s.writeS32(4, m_workspaceIndex);
    s.writeString(5, m_title);
    s.writeU32(6, m_rgbColor);
    s.writeBool(7, m_hidden);
    s.writeBool(8, m_audioInput);
    s.writeBool(9, m_useReverseAPI);
    s.writeString(10, m_reverseAPIAddress);
    s.writeU32(11, m_reverseAPIPort);
    s.writeU32(12, m_reverseAPIDeviceIndex);
    s.writeU32(13, m_reverseAPIChannelIndex);
    s.writeString(14, m_csvLogFilename);
    s.writeBool(15, m_csvLogEnabled);
    s.writeBool(16, m_udpEnabled);
    s.writeString(17, m_udpAddress);
    s.writeS32(18, m_udpPort);
    s.writeS32(19, m_udpFormat);
    s.writeBool(20, m_useFileTime);
    s.writeString(21, m_filterText);
    s.writeBool(22, m_iqRecordEnabled);
    s.writeReal(23, m_lpfCutoff);
    s.writeReal(24, m_lpfTransition);
    s.writeReal(25, m_lpfGain);
    s.writeBool(41, m_lpfEnabled);
    s.writeBool(42, m_aaLpfEnabled);
    s.writeReal(43, m_aaLpfCutoff);
    s.writeReal(44, m_aaLpfTransition);
    s.writeReal(45, m_aaLpfGain);
    s.writeBool(26, m_resamplerEnabled);
    s.writeS32(27, m_resamplerInputRate);
    s.writeS32(28, m_resamplerOutputRate);
    s.writeS32(29, m_symSyncType);
    s.writeReal(30, m_symSyncSps);
    s.writeReal(31, m_symSyncLoopBw);
    s.writeReal(32, m_symSyncDamping);
    s.writeReal(33, m_symSyncTedGain);
    s.writeReal(34, m_symSyncMaxDev);
    s.writeBool(35, m_mergeMode);
    s.writeS32(36, m_corrThreshold);
    s.writeReal(37, m_observerLat);
    s.writeReal(38, m_observerLon);
    s.writeReal(39, m_observerAltM);
    s.writeBool(40, m_iqWavEnabled);
    return s.final();
}

bool GEOSCANDecoderSettings::deserialize(const QByteArray &data)
{
    SimpleDeserializer d(data);
    if (!d.isValid())
    {
        resetToDefaults();
        return false;
    }
    if (d.getVersion() == 1)
    {
        qint64 inputFrequencyOffset;
        d.readS64(1, &inputFrequencyOffset, 0);
        m_inputFrequencyOffset = inputFrequencyOffset;

        Real rfBandwidth;
        d.readReal(2, &rfBandwidth, 12500.0f);
        m_rfBandwidth = rfBandwidth;

        d.readS32(3, &m_streamIndex, 0);
        d.readS32(4, &m_workspaceIndex, 0);
        d.readString(5, &m_title, "GEOSCAN Decoder");
        d.readU32(6, &m_rgbColor, QColor(255, 0, 0).rgb());
        d.readBool(7, &m_hidden, false);
        d.readBool(8, &m_audioInput, false);
        d.readBool(9, &m_useReverseAPI, false);
        d.readString(10, &m_reverseAPIAddress, "127.0.0.1");

        uint32_t utmp;
        d.readU32(11, &utmp, 8888);
        m_reverseAPIPort = utmp;
        d.readU32(12, &m_reverseAPIDeviceIndex, 0);
        d.readU32(13, &m_reverseAPIChannelIndex, 0);

        d.readString(14, &m_csvLogFilename, "");
        d.readBool(15, &m_csvLogEnabled, false);
        d.readBool(16, &m_udpEnabled, false);
        d.readString(17, &m_udpAddress, "127.0.0.1");
        d.readS32(18, &m_udpPort, 9999);
        d.readS32(19, &m_udpFormat, 0);
        d.readBool(20, &m_useFileTime, true);
        d.readString(21, &m_filterText, "");
        d.readBool(22, &m_iqRecordEnabled, false);
        d.readReal(23, &m_lpfCutoff, 7000.0f);
        d.readReal(24, &m_lpfTransition, 2000.0f);
        d.readReal(25, &m_lpfGain, 0.0045f);
        d.readBool(41, &m_lpfEnabled, true);
        d.readBool(42, &m_aaLpfEnabled, true);
        d.readReal(43, &m_aaLpfCutoff, 6000.0f);
        d.readReal(44, &m_aaLpfTransition, 6000.0f);
        d.readReal(45, &m_aaLpfGain, 1.0f);
        d.readBool(26, &m_resamplerEnabled, true);
        d.readS32(27, &m_resamplerInputRate, 2000000);
        d.readS32(28, &m_resamplerOutputRate, 48000);
        d.readS32(29, &m_symSyncType, 0);
        d.readReal(30, &m_symSyncSps, 4.0f);
        d.readReal(31, &m_symSyncLoopBw, 0.2f);
        d.readReal(32, &m_symSyncDamping, 1.0f);
        d.readReal(33, &m_symSyncTedGain, 1.0f);
        d.readReal(34, &m_symSyncMaxDev, 0.05f);
        d.readBool(35, &m_mergeMode, true);
        d.readS32(36, &m_corrThreshold, 2);
        d.readReal(37, &m_observerLat, 0.0);
        d.readReal(38, &m_observerLon, 0.0);
        d.readReal(39, &m_observerAltM, 0.0);
        d.readBool(40, &m_iqWavEnabled, false);
        return true;
    }
    resetToDefaults();
    return false;
}

void GEOSCANDecoderSettings::applySettings(const QStringList &settingsKeys, const GEOSCANDecoderSettings &settings)
{
    if (settingsKeys.contains("inputFrequencyOffset"))
        m_inputFrequencyOffset = settings.m_inputFrequencyOffset;
    if (settingsKeys.contains("rfBandwidth"))
        m_rfBandwidth = settings.m_rfBandwidth;
    if (settingsKeys.contains("streamIndex"))
        m_streamIndex = settings.m_streamIndex;
    if (settingsKeys.contains("workspaceIndex"))
        m_workspaceIndex = settings.m_workspaceIndex;
    if (settingsKeys.contains("title"))
        m_title = settings.m_title;
    if (settingsKeys.contains("audioInput"))
        m_audioInput = settings.m_audioInput;
    if (settingsKeys.contains("rgbColor"))
        m_rgbColor = settings.m_rgbColor;
    if (settingsKeys.contains("hidden"))
        m_hidden = settings.m_hidden;
    if (settingsKeys.contains("useReverseAPI"))
        m_useReverseAPI = settings.m_useReverseAPI;
    if (settingsKeys.contains("reverseAPIAddress"))
        m_reverseAPIAddress = settings.m_reverseAPIAddress;
    if (settingsKeys.contains("reverseAPIPort"))
        m_reverseAPIPort = settings.m_reverseAPIPort;
    if (settingsKeys.contains("reverseAPIDeviceIndex"))
        m_reverseAPIDeviceIndex = settings.m_reverseAPIDeviceIndex;
    if (settingsKeys.contains("reverseAPIChannelIndex"))
        m_reverseAPIChannelIndex = settings.m_reverseAPIChannelIndex;
    if (settingsKeys.contains("csvLogFilename"))
        m_csvLogFilename = settings.m_csvLogFilename;
    if (settingsKeys.contains("csvLogEnabled"))
        m_csvLogEnabled = settings.m_csvLogEnabled;
    if (settingsKeys.contains("udpEnabled"))
        m_udpEnabled = settings.m_udpEnabled;
    if (settingsKeys.contains("udpAddress"))
        m_udpAddress = settings.m_udpAddress;
    if (settingsKeys.contains("udpPort"))
        m_udpPort = settings.m_udpPort;
    if (settingsKeys.contains("udpFormat"))
        m_udpFormat = settings.m_udpFormat;
    if (settingsKeys.contains("useFileTime"))
        m_useFileTime = settings.m_useFileTime;
    if (settingsKeys.contains("filterText"))
        m_filterText = settings.m_filterText;
    if (settingsKeys.contains("iqRecordEnabled"))
        m_iqRecordEnabled = settings.m_iqRecordEnabled;
    if (settingsKeys.contains("iqWavEnabled"))
        m_iqWavEnabled = settings.m_iqWavEnabled;
    if (settingsKeys.contains("lpfEnabled"))
        m_lpfEnabled = settings.m_lpfEnabled;
    if (settingsKeys.contains("lpfCutoff"))
        m_lpfCutoff = settings.m_lpfCutoff;

    if (settingsKeys.contains("lpfTransition"))
        m_lpfTransition = settings.m_lpfTransition;

    if (settingsKeys.contains("lpfGain"))
        m_lpfGain = settings.m_lpfGain;

    if (settingsKeys.contains("aaLpfEnabled"))
        m_aaLpfEnabled = settings.m_aaLpfEnabled;
    if (settingsKeys.contains("aaLpfCutoff"))
        m_aaLpfCutoff = settings.m_aaLpfCutoff;
    if (settingsKeys.contains("aaLpfTransition"))
        m_aaLpfTransition = settings.m_aaLpfTransition;
    if (settingsKeys.contains("aaLpfGain"))
        m_aaLpfGain = settings.m_aaLpfGain;

    if (settingsKeys.contains("resamplerEnabled"))
        m_resamplerEnabled = settings.m_resamplerEnabled;
    if (settingsKeys.contains("resamplerInputRate"))
        m_resamplerInputRate = settings.m_resamplerInputRate;
    if (settingsKeys.contains("resamplerOutputRate"))
        m_resamplerOutputRate = settings.m_resamplerOutputRate;

    if (settingsKeys.contains("symSyncType"))
        m_symSyncType = settings.m_symSyncType;
    if (settingsKeys.contains("symSyncSps"))
        m_symSyncSps = settings.m_symSyncSps;
    if (settingsKeys.contains("symSyncLoopBw"))
        m_symSyncLoopBw = settings.m_symSyncLoopBw;
    if (settingsKeys.contains("symSyncDamping"))
        m_symSyncDamping = settings.m_symSyncDamping;
    if (settingsKeys.contains("symSyncTedGain"))
        m_symSyncTedGain = settings.m_symSyncTedGain;
    if (settingsKeys.contains("symSyncMaxDev"))
        m_symSyncMaxDev = settings.m_symSyncMaxDev;
    if (settingsKeys.contains("mergeMode"))
        m_mergeMode = settings.m_mergeMode;
    if (settingsKeys.contains("corrThreshold"))
        m_corrThreshold = settings.m_corrThreshold;
    if (settingsKeys.contains("observerLat"))
        m_observerLat = settings.m_observerLat;
    if (settingsKeys.contains("observerLon"))
        m_observerLon = settings.m_observerLon;
    if (settingsKeys.contains("observerAltM"))
        m_observerAltM = settings.m_observerAltM;
}

QString GEOSCANDecoderSettings::getDebugString(const QStringList &settingsKeys, bool force) const
{
    (void)settingsKeys;
    (void)force;
    return QString("GEOSCANDecoderSettings");
}
