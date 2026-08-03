#ifndef GEOSCANDECODERBASEBAND_H
#define GEOSCANDECODERBASEBAND_H

#include "dsp.hpp"
#include "dsp/dsptypes.h"
#include "geoscandecodersettings.h"
#include "util/message.h"
#include "util/messagequeue.h"

#include <QDateTime>
#include <QFile>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <atomic>
#include <complex>
#include <vector>
#include <memory>

class GEOSCANDecoderBaseband : public QObject {
    Q_OBJECT
  public:
    struct TelemetryData
    {
        // AX.25
        QString sourceCallsign;
        QString destinationCallsign;
        uint8_t control;
        uint8_t pid;

        // OBC
        uint32_t timestamp;
        uint8_t mayakId;
        uint8_t obcActivity;
        uint8_t gnssCount;
        uint8_t mediaFilesCount;

        // EPS
        uint8_t epsMode;
        uint16_t currentLoadMa;
        uint16_t currentSolarMa;
        uint16_t voltageBattOneMv;
        uint16_t voltageBattSumMv;

        // TEMP
        int8_t tempBatt1;
        int8_t tempBatt2;
        int8_t tempXPlus;
        int8_t tempXMinus;
        int8_t tempYPlus;
        int8_t tempYMinus;

        // RADIO
        uint16_t vbusVoltageMv;
        int8_t rssiLast;
        int8_t rssiMin;
        uint16_t packetsSent;
        uint8_t qsoReceived;
    };

    std::shared_ptr<FIRFilter> m_lpf;
    std::atomic<bool> m_lpfReset{ false };
    std::shared_ptr<ComplexFIRFilter> m_aaFilter;

    class MsgConfigureGEOSCANDecoderBaseband : public Message {
        MESSAGE_CLASS_DECLARATION
      public:
        MsgConfigureGEOSCANDecoderBaseband(const QStringList &keys, const GEOSCANDecoderSettings &settings, bool force) : m_settings(settings), m_keys(keys), m_force(force) {}
        static MsgConfigureGEOSCANDecoderBaseband *create(const QStringList &keys, const GEOSCANDecoderSettings &settings, bool force) { return new MsgConfigureGEOSCANDecoderBaseband(keys, settings, force); }
        const GEOSCANDecoderSettings &getSettings() const { return m_settings; }
        const QStringList &getSettingsKeys() const { return m_keys; }
      private:
        GEOSCANDecoderSettings m_settings;
        QStringList m_keys;
        bool m_force;
    };

    class MsgSampleCount : public Message {
        MESSAGE_CLASS_DECLARATION
      public:
        MsgSampleCount(uint32_t sampleCount) : m_sampleCount(sampleCount) {}
        uint32_t getSampleCount() const { return m_sampleCount; }
      private:
        uint32_t m_sampleCount;
    };

    class MsgPacketFound : public Message {
        MESSAGE_CLASS_DECLARATION
      public:
        MsgPacketFound(const QString &hex, bool matched, uint32_t samplesFromStart) : m_hex(hex), m_matched(matched), m_samplesFromStart(samplesFromStart) {}
        static MsgPacketFound *create(const QString &hex, bool matched, uint32_t samplesFromStart) { return new MsgPacketFound(hex, matched, samplesFromStart); }
        const QString &getHex() const { return m_hex; }
        bool isMatched() const { return m_matched; }
        bool getCrcOk() const { return m_matched; }
        uint32_t getSamplesFromStart() const { return m_samplesFromStart; }
      private:
        QString m_hex;
        bool m_matched;
        uint32_t m_samplesFromStart;
    };

    class MsgSignalReport : public Message {
        MESSAGE_CLASS_DECLARATION
      public:
        MsgSignalReport(float snr, float frequencyOffset) : m_snr(snr), m_frequencyOffset(frequencyOffset) {}
        float getSnr() const { return m_snr; }
        float getFrequencyOffset() const { return m_frequencyOffset; }
      private:
        float m_snr;
        float m_frequencyOffset;
    };

    class MsgDebugText : public Message {
        MESSAGE_CLASS_DECLARATION
      public:
        MsgDebugText(const QString &text) : m_text(text) {}
        static MsgDebugText *create(const QString &text) { return new MsgDebugText(text); }
        const QString &getText() const { return m_text; }
      private:
        QString m_text;
    };

    class MsgTelemetry : public Message {
        MESSAGE_CLASS_DECLARATION
      public:
        MsgTelemetry(const TelemetryData &data) : m_data(data) {}
        static MsgTelemetry *create(const TelemetryData &data) { return new MsgTelemetry(data); }
        const TelemetryData &getData() const { return m_data; }
      private:
        TelemetryData m_data;
    };

    class MsgImageData : public Message {
        MESSAGE_CLASS_DECLARATION
      public:
        MsgImageData(uint16_t fileId, uint32_t offset, const QByteArray &data)
            : m_fileId(fileId), m_offset(offset), m_data(data) {}
        static MsgImageData *create(uint16_t fileId, uint32_t offset, const QByteArray &data)
            { return new MsgImageData(fileId, offset, data); }
        uint16_t getFileId() const { return m_fileId; }
        uint32_t getOffset() const { return m_offset; }
        const QByteArray &getData() const { return m_data; }
      private:
        uint16_t m_fileId;
        uint32_t m_offset;
        QByteArray m_data;
    };

    GEOSCANDecoderBaseband();
    virtual ~GEOSCANDecoderBaseband();

    void feed(const SampleVector::const_iterator &begin, const SampleVector::const_iterator &end);
    void setMessageQueueToGUI(MessageQueue *messageQueue)
    {
        m_mutex.lock();
        m_messageQueueToGUI = messageQueue;
        m_mutex.unlock();
    }
    MessageQueue *getInputMessageQueue() { return &m_inputMessageQueue; }

    void setFifoLabel(const QString &label) { (void)label; }
    void setChannel(QObject *channel) { (void)channel; }

    void setBasebandSampleRate(uint32_t sampleRate)
    {
        m_basebandSampleRate = sampleRate;
        m_mutex.lock();
        m_freqShifter.setFrequency(-(float)m_settings.m_inputFrequencyOffset, (float)m_basebandSampleRate);
        m_mutex.unlock();
    }

    void getMagSqLevels(double &avg, double &peak, int &nbSamples);

  public slots:
    void handleInputMessages();
    void handleData();
  private:
    void applySettings(const QStringList &keys, const GEOSCANDecoderSettings &settings, bool force);
    void processSample(std::complex<float> s, bool iqEnabled, std::shared_ptr<FIRFilter> lpf);
    void processPacket(std::vector<uint8_t> &packet, bool inverted);
    bool checkCRC16(const std::vector<uint8_t> &data);
    bool handleMessage(const Message &msg);

    MessageQueue m_inputMessageQueue;
    MessageQueue *m_messageQueueToGUI;
    GEOSCANDecoderSettings m_settings;
    QMutex m_mutex;
    uint32_t m_basebandSampleRate;
    uint32_t m_effectiveSampleRate;
    std::complex<float> m_prevSample;
    double m_magSqSum = 0;
    double m_magSqPeak = 0;
    int m_magSqCount = 0;

    FrequencyShifter m_freqShifter;

    FractionalResampler *m_resampler = nullptr;
    void updateResampler();

    SymbolSync *m_symbolSync = nullptr;
    bool m_symSyncEnabled = false;
    void updateSymbolSync();

    uint64_t m_sampleCount = 0;
    uint64_t m_lastPacketSampleCount = 0;
    bool m_lastPacketWasCrcOk = false;

    struct StreamState
    {
        uint32_t shiftReg = 0;
        int bitAccum = 0;
        int bitAccumCount = 0;
        int bitsLeft = 0;
        bool inverted = false;
        std::vector<uint8_t> packetBuf;
    };
    StreamState m_symStream;

    bool m_isRecording = false;
    QFile m_iqFile;
    qint64 m_lastSyncTime = 0;
    QTimer *m_timeoutTimer = nullptr;
    QMap<uint64_t, uint64_t> m_lastImageChunkSample;

    QByteArray m_wavBuffer;
    uint32_t m_wavSampleRate = 0;
    qint64 m_wavLastCrcOk = 0;
    int m_wavPartNumber = 0;
    bool m_wavActive = false;
    void saveWav();
};

#endif