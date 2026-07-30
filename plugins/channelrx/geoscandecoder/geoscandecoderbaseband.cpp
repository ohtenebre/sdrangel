#include "dsp.hpp"
#include "dsp/dspcommands.h"
#include "geoscandecoder.h"
#include "geoscandecoderbaseband.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QTimeZone>
#include <QUdpSocket>
#include <stdio.h>
#include <string.h>
#include <vector>

MESSAGE_CLASS_DEFINITION(GEOSCANDecoderBaseband::MsgConfigureGEOSCANDecoderBaseband, Message)
MESSAGE_CLASS_DEFINITION(GEOSCANDecoderBaseband::MsgSampleCount, Message)
MESSAGE_CLASS_DEFINITION(GEOSCANDecoderBaseband::MsgPacketFound, Message)
MESSAGE_CLASS_DEFINITION(GEOSCANDecoderBaseband::MsgSignalReport, Message)
MESSAGE_CLASS_DEFINITION(GEOSCANDecoderBaseband::MsgTelemetry, Message)
MESSAGE_CLASS_DEFINITION(GEOSCANDecoderBaseband::MsgDebugText, Message)
MESSAGE_CLASS_DEFINITION(GEOSCANDecoderBaseband::MsgImageData, Message)

GEOSCANDecoderBaseband::GEOSCANDecoderBaseband() : m_lpf(std::make_shared<FIRFilter>(generate_lowpass(0.0045f, 48000.f, 7000.f, 2000.f))), m_messageQueueToGUI(nullptr), m_basebandSampleRate(48000), m_effectiveSampleRate(48000)
{
    m_freqShifter.setFrequency(-(float)m_settings.m_inputFrequencyOffset, (float)m_basebandSampleRate);

    connect(&m_inputMessageQueue, &MessageQueue::messageEnqueued, this, &GEOSCANDecoderBaseband::handleInputMessages);

    m_timeoutTimer = new QTimer(this);
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]()
            {
        if (m_isRecording &&
            (QDateTime::currentMSecsSinceEpoch() - m_lastSyncTime) > 30000) {
            m_isRecording = false;
            qWarning() << "[GEOSCAN] Спутник улетел (таймер). Запись выключена.";
        }
        if (m_settings.m_iqWavEnabled && !m_wavBuffer.isEmpty() &&
            m_wavLastCrcOk > 0 &&
            (QDateTime::currentMSecsSinceEpoch() - m_wavLastCrcOk) > 15000) {
            qWarning() << "[GEOSCAN] WAV: 15с без CRC OK, сохраняю и жду sync.";
            saveWav();
            m_wavActive = false;
        }
        if (m_settings.m_iqWavEnabled && !m_wavBuffer.isEmpty() &&
            m_wavBuffer.size() > 170 * 1024 * 1024) {
            qWarning() << "[GEOSCAN] WAV: автосохранение (170 МБ).";
            saveWav();
        } });
    m_timeoutTimer->start(5000);
}

GEOSCANDecoderBaseband::~GEOSCANDecoderBaseband()
{
    m_timeoutTimer->stop();
    if (m_iqFile.isOpen())
        m_iqFile.close();
    if (!m_wavBuffer.isEmpty())
        saveWav();
    m_wavSampleRate = 0;
    delete m_resampler;
}

void GEOSCANDecoderBaseband::saveWav()
{
    if (m_wavBuffer.isEmpty())
        return;
    QString dir = QString("/tmp/geoscandecoder/%1").arg(QCoreApplication::applicationPid());
    QDir().mkpath(dir);
    QString fname = dir + QString("/iq_%1_%2.wav").arg(QDateTime::currentMSecsSinceEpoch()).arg(m_wavPartNumber);
    QFile f(fname);
    if (!f.open(QIODevice::WriteOnly))
    {
        qWarning() << "[GEOSCAN] WAV: не удалось открыть" << fname;
        m_wavBuffer.clear();
        return;
    }
    uint32_t sampleCount = m_wavBuffer.size() / 4;
    uint32_t dataSize = m_wavBuffer.size();
    uint32_t fileSize = dataSize + 36;
    uint16_t nch = 2;
    uint16_t bps = 16;
    uint16_t align = nch * bps / 8;
    uint32_t byteRate = m_wavSampleRate * align;
    char h[44] = {};
    memcpy(h, "RIFF", 4);
    memcpy(h + 4, &fileSize, 4);
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    uint32_t fmtSize = 16;
    memcpy(h + 16, &fmtSize, 4);
    uint16_t fmt = 1;
    memcpy(h + 20, &fmt, 2);
    memcpy(h + 22, &nch, 2);
    memcpy(h + 24, &m_wavSampleRate, 4);
    memcpy(h + 28, &byteRate, 4);
    memcpy(h + 32, &align, 2);
    memcpy(h + 34, &bps, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &dataSize, 4);
    f.write(h, 44);
    f.write(m_wavBuffer);
    f.close();
    qWarning() << "[GEOSCAN] WAV сохранён:" << fname << "сэмплов:" << sampleCount;
    m_wavBuffer.clear();
    m_wavPartNumber++;
    m_wavLastCrcOk = 0;
}

void GEOSCANDecoderBaseband::getMagSqLevels(double &avg, double &peak, int &nbSamples)
{
    m_mutex.lock();
    avg = m_magSqCount > 0 ? (m_magSqSum / m_magSqCount) / 1e10 : 0;
    peak = m_magSqPeak / 1e10;
    nbSamples = m_magSqCount;
    m_magSqSum = 0;
    m_magSqPeak = 0;
    m_magSqCount = 0;
    m_mutex.unlock();
}

void GEOSCANDecoderBaseband::feed(const SampleVector::const_iterator &b, const SampleVector::const_iterator &e)
{
    if (m_isRecording && (QDateTime::currentMSecsSinceEpoch() - m_lastSyncTime) > 30000)
    {
        m_isRecording = false;
        qWarning() << "[GEOSCAN] Спутник улетел. Запись выключена.";
    }

    if (m_isRecording)
    {
        if (!m_iqFile.isOpen())
        {
            QString fname = QString("geoscan_iq_%1.bin").arg(QDateTime::currentSecsSinceEpoch());
            m_iqFile.setFileName(fname);
            m_iqFile.open(QIODevice::WriteOnly);
            qWarning() << "[GEOSCAN] Запись IQ начата:" << fname;
        }
        for (auto it = b; it != e; ++it)
        {
            int16_t i16 = (int16_t)it->real();
            int16_t q16 = (int16_t)it->imag();
            m_iqFile.write(reinterpret_cast<const char *>(&i16), 2);
            m_iqFile.write(reinterpret_cast<const char *>(&q16), 2);
        }
    }
    else
    {
        if (m_iqFile.isOpen())
        {
            m_iqFile.close();
            qWarning() << "[GEOSCAN] Запись IQ завершена.";
        }
    }

    m_mutex.lock();
    FractionalResampler *r = m_resampler;
    bool iqEnabled = m_settings.m_iqRecordEnabled;
    bool wavEnabled = m_settings.m_iqWavEnabled;
    m_mutex.unlock();

    if (wavEnabled && m_wavBuffer.isEmpty() && !m_wavActive && m_wavSampleRate == 0)
    {
        m_wavSampleRate = r ? m_effectiveSampleRate : m_basebandSampleRate;
        m_wavPartNumber = 0;
        qWarning() << "[GEOSCAN] WAV: ожидание sync, rate=" << m_wavSampleRate;
    }

    if (!wavEnabled && !m_wavBuffer.isEmpty())
    {
        qWarning() << "[GEOSCAN] WAV: кнопка выключена, сохраняю, байт=" << m_wavBuffer.size();
        saveWav();
        m_wavSampleRate = 0;
        m_wavActive = false;
    }

    std::shared_ptr<FIRFilter> lpf = std::atomic_load(&m_lpf);
    int wavAppendCount = 0;

    for (auto it = b; it != e; ++it)
    {
        std::complex<float> raw(it->real(), it->imag());

        std::complex<float> s = m_freqShifter.processSample(raw);

        if (r)
        {
            std::complex<float> resampled[4];
            int n = r->processSample(s, resampled, 4);
            for (int i = 0; i < n; i++)
            {
                processSample(resampled[i], iqEnabled, lpf);
                if (wavEnabled && m_wavActive)
                {
                    int32_t ii = (int32_t)resampled[i].real();
                    int32_t qq = (int32_t)resampled[i].imag();
                    ii = std::clamp(ii, (int32_t)-32768, (int32_t)32767);
                    qq = std::clamp(qq, (int32_t)-32768, (int32_t)32767);
                    int16_t si = (int16_t)ii;
                    int16_t sq = (int16_t)qq;
                    m_wavBuffer.append(reinterpret_cast<const char *>(&si), 2);
                    m_wavBuffer.append(reinterpret_cast<const char *>(&sq), 2);
                    wavAppendCount++;
                }
            }
        }
        else
        {
            processSample(s, iqEnabled, lpf);
            if (wavEnabled && m_wavActive)
            {
                int32_t ii = (int32_t)s.real();
                int32_t qq = (int32_t)s.imag();
                ii = std::clamp(ii, (int32_t)-32768, (int32_t)32767);
                qq = std::clamp(qq, (int32_t)-32768, (int32_t)32767);
                int16_t si = (int16_t)ii;
                int16_t sq = (int16_t)qq;
                m_wavBuffer.append(reinterpret_cast<const char *>(&si), 2);
                m_wavBuffer.append(reinterpret_cast<const char *>(&sq), 2);
                wavAppendCount++;
            }
        }
    }

    if (wavEnabled && wavAppendCount > 0 && m_wavBuffer.size() % (16384 * 4) < wavAppendCount * 4)
    {
        qWarning() << "[GEOSCAN] WAV: буфер=" << m_wavBuffer.size() << "байт";
    }
}

void GEOSCANDecoderBaseband::processSample(std::complex<float> s, bool iqEnabled, std::shared_ptr<FIRFilter> lpf)
{
    m_sampleCount++;
    float magSq = std::norm(s);
    m_magSqSum += magSq;
    if (magSq > m_magSqPeak)
        m_magSqPeak = magSq;
    m_magSqCount++;

    float freq = 1.27323954f * std::arg(s * std::conj(m_prevSample));
    m_prevSample = s;

    if (m_lpfReset.exchange(false))
        lpf->reset();

    float filtered_freq = lpf->processSample(freq);

    auto processSymbolBit = [this, iqEnabled](int hardBit, StreamState &st) -> bool
    {
        st.shiftReg = (st.shiftReg << 1) | hardBit;

        if (st.bitsLeft > 0)
        {
            st.bitAccum = (st.bitAccum << 1) | hardBit;
            if (++st.bitAccumCount == 8)
            {
                st.packetBuf.push_back((uint8_t)st.bitAccum);
                st.bitAccum = 0;
                st.bitAccumCount = 0;
            }
            if (--st.bitsLeft == 0)
                processPacket(st.packetBuf, st.inverted);
            return false;
        }

        bool syncFound = false;
        int thresh = m_settings.m_corrThreshold;
        if (__builtin_popcount(st.shiftReg ^ 0x930B51DE) <= thresh)
        {
            st.inverted = false;
            syncFound = true;
        }
        else if (__builtin_popcount(st.shiftReg ^ 0x6CF4AE21) <= thresh)
        {
            st.inverted = true;
            syncFound = true;
        }

        if (syncFound)
        {
            if (iqEnabled)
            {
                if (!m_isRecording)
                    qWarning() << "[GEOSCAN] Sync! Запись включена. inverted=" << st.inverted;
                m_isRecording = true;
                m_lastSyncTime = QDateTime::currentMSecsSinceEpoch();
            }
            if (m_settings.m_iqWavEnabled)
            {
                m_wavLastCrcOk = QDateTime::currentMSecsSinceEpoch();
                if (!m_wavActive && m_wavSampleRate > 0)
                {
                    m_wavActive = true;
                    qWarning() << "[GEOSCAN] WAV: sync получен, запись активна";
                }
            }
            st.packetBuf.clear();
            st.bitAccumCount = 0;
            st.bitsLeft = 74 * 8;
            st.shiftReg = 0;
            return true;
        }

        if (m_isRecording && (QDateTime::currentMSecsSinceEpoch() - m_lastSyncTime) > 30000)
        {
            m_isRecording = false;
            qWarning() << "[GEOSCAN] Спутник улетел. Запись выключена.";
        }

        return false;
    };

    if (m_symbolSync)
    {
        float symbol;
        if (m_symbolSync->processSample(filtered_freq, symbol))
        {
            int hardBit = (symbol >= 0.0f) ? 1 : 0;
            if (processSymbolBit(hardBit, m_symStream))
                return;
        }
    }
}

QString decodeAX25Callsign(const uint8_t *data)
{
    QString callsign;

    for (int i = 0; i < 6; i++)
    {
        char c = data[i] >> 1;

        if (c != ' ')
            callsign += QChar(c);
    }

    int ssid = (data[6] >> 1) & 0x0F;

    callsign += QString("-%1").arg(ssid);

    return callsign;
}

void GEOSCANDecoderBaseband::processPacket(std::vector<uint8_t> &packet, bool inverted)
{
    if (inverted)
        for (auto &b : packet)
            b = ~b;

    // PN9 descrambler
    uint32_t st = 0x1FF;
    for (size_t i = 0; i < packet.size(); i++)
    {
        uint8_t mask = 0;
        for (int b = 0; b < 8; b++)
        {
            mask |= ((st & 1) << b);
            uint8_t next_bit = ((st >> 5) ^ (st >> 0)) & 1;
            st = ((st >> 1) | (next_bit << 8)) & 0x1FF;
        }
        packet[i] ^= mask;
    }

    bool crcOk = checkCRC16(packet);

    // Дедупликация от 8 параллельных потоков: 200 отсчётов окно, CRC-OK вытесняет CRC-FAIL
    if (m_sampleCount - m_lastPacketSampleCount < 200)
    {
        if (crcOk && !m_lastPacketWasCrcOk)
        {
            m_lastPacketWasCrcOk = true;
            m_lastPacketSampleCount = m_sampleCount;
        }
        else
        {
            return;
        }
    }
    else
    {
        m_lastPacketWasCrcOk = crcOk;
        m_lastPacketSampleCount = m_sampleCount;
    }

    if (!crcOk || packet.size() < 74)
    {
        if (m_messageQueueToGUI)
        {
            QString hex;
            for (auto b : packet)
                hex += QString("%1 ").arg(b, 2, 16, QChar('0')).toUpper();
            m_messageQueueToGUI->push(MsgPacketFound::create(hex, false, 0));

            QString ascii;
            for (auto b : packet)
                ascii += (b >= 32 && b < 127) ? QChar(b) : QChar('.');
            m_messageQueueToGUI->push(MsgDebugText::create(ascii + "\n"));
        }
        return;
    }

    uint32_t imgSync = ((uint32_t)packet[5] << 24) | ((uint32_t)packet[6] << 16) | ((uint32_t)packet[7] << 8) | packet[8];
    if (imgSync == 0x316F6B6F)
    {
        uint32_t offset = ((uint32_t)packet[9]) | ((uint32_t)packet[10] << 8) | ((uint32_t)packet[11] << 16) | ((uint32_t)packet[12] << 24);
        uint16_t fileId = packet[13] | (packet[14] << 8);

        uint64_t key = ((uint64_t)fileId << 32) | offset;
        auto lastIt = m_lastImageChunkSample.find(key);
        if (lastIt != m_lastImageChunkSample.end() && (m_sampleCount - lastIt.value()) < 5000)
            return;
        m_lastImageChunkSample[key] = m_sampleCount;
        if (m_lastImageChunkSample.size() > 10000)
            m_lastImageChunkSample.clear();

        if (m_messageQueueToGUI)
        {
            QString hex;
            for (auto b : packet)
                hex += QString("%1 ").arg(b, 2, 16, QChar('0')).toUpper();
            m_messageQueueToGUI->push(MsgPacketFound::create(hex, true, 0));
        }

        QByteArray imgData((const char *)&packet[15], 54);
        if (m_messageQueueToGUI)
            m_messageQueueToGUI->push(MsgImageData::create(fileId, offset, imgData));
        return;
    }

    // Уникальный CRC-OK пакет телеметрии — отправляем в GUI
    if (m_messageQueueToGUI)
    {
        QString hex;
        for (auto b : packet)
            hex += QString("%1 ").arg(b, 2, 16, QChar('0')).toUpper();
        m_messageQueueToGUI->push(MsgPacketFound::create(hex, true, 0));

        QString ascii;
        for (auto b : packet)
            ascii += (b >= 32 && b < 127) ? QChar(b) : QChar('.');
        m_messageQueueToGUI->push(MsgDebugText::create(ascii + "\n"));
    }

    m_mutex.lock();
    bool udpEnabled = m_settings.m_udpEnabled;
    QString udpAddress = m_settings.m_udpAddress;
    int udpPort = m_settings.m_udpPort;
    int udpFormat = m_settings.m_udpFormat;
    m_mutex.unlock();

    TelemetryData tel;
    tel.destinationCallsign = decodeAX25Callsign(packet.data());
    tel.sourceCallsign = decodeAX25Callsign(packet.data() + 7);
    tel.control = packet[14];
    tel.pid = packet[15];

    tel.mayakId = packet[16];
    tel.timestamp = packet[17] | (packet[18] << 8) | (packet[19] << 16) | (packet[20] << 24);
    tel.epsMode = packet[21];
    tel.currentLoadMa = packet[23] | (packet[24] << 8);
    tel.currentSolarMa = packet[25] | (packet[26] << 8);
    tel.voltageBattOneMv = packet[27] | (packet[28] << 8);
    tel.voltageBattSumMv = packet[29] | (packet[30] << 8);
    tel.tempBatt1 = (int8_t)packet[33];
    tel.tempBatt2 = (int8_t)packet[34];

    tel.obcActivity = packet[42];
    tel.tempXPlus = (int8_t)packet[43];
    tel.tempXMinus = (int8_t)packet[44];
    tel.tempYPlus = (int8_t)packet[45];
    tel.tempYMinus = (int8_t)packet[46];
    tel.gnssCount = packet[47];
    tel.mediaFilesCount = packet[50];

    tel.vbusVoltageMv = packet[57] | (packet[58] << 8);
    tel.rssiLast = (int8_t)packet[61];
    tel.rssiMin = (int8_t)packet[62];
    tel.packetsSent = packet[65] | (packet[66] << 8);
    tel.qsoReceived = packet[69];
    m_messageQueueToGUI->push(MsgTelemetry::create(tel));

    if (udpEnabled)
    {
        QUdpSocket udpSocket;
        if (udpFormat == 0)
        {
            udpSocket.writeDatagram((const char *)packet.data(), packet.size(), QHostAddress(udpAddress), udpPort);
        }
        else if (udpFormat == 1)
        {
            QString dateTimeStr = QDateTime::fromSecsSinceEpoch(tel.timestamp, QTimeZone::utc()).toString("yyyy-MM-dd HH:mm:ss");
            QString jsonStr = QString(
                                  "{"
                                  "\"timestamp\":%1,"
                                  "\"dateTime\":\"%2\","
                                  "\"sourceCallsign\":\"%3\","
                                  "\"destinationCallsign\":\"%4\","
                                  "\"mayakId\":%5,"
                                  "\"epsMode\":%6,"
                                  "\"currentLoadMa\":%7,"
                                  "\"currentSolarMa\":%8,"
                                  "\"voltageBattOneMv\":%9,"
                                  "\"voltageBattSumMv\":%10,"
                                  "\"tempBatt1\":%11,"
                                  "\"tempBatt2\":%12,"
                                  "\"tempXPlus\":%13,"
                                  "\"tempXMinus\":%14,"
                                  "\"tempYPlus\":%15,"
                                  "\"tempYMinus\":%16,"
                                  "\"obcActivity\":%17,"
                                  "\"gnssCount\":%18,"
                                  "\"mediaFilesCount\":%19,"
                                  "\"vbusVoltageMv\":%20,"
                                  "\"rssiLast\":%21,"
                                  "\"rssiMin\":%22,"
                                  "\"packetsSent\":%23,"
                                  "\"qsoReceived\":%24"
                                  "}"
            )
                                  .arg(tel.timestamp)
                                  .arg(dateTimeStr)
                                  .arg(tel.sourceCallsign)
                                  .arg(tel.destinationCallsign)
                                  .arg(tel.mayakId)
                                  .arg(tel.epsMode)
                                  .arg(tel.currentLoadMa)
                                  .arg(tel.currentSolarMa)
                                  .arg(tel.voltageBattOneMv)
                                  .arg(tel.voltageBattSumMv)
                                  .arg(tel.tempBatt1)
                                  .arg(tel.tempBatt2)
                                  .arg(tel.tempXPlus)
                                  .arg(tel.tempXMinus)
                                  .arg(tel.tempYPlus)
                                  .arg(tel.tempYMinus)
                                  .arg(tel.obcActivity)
                                  .arg(tel.gnssCount)
                                  .arg(tel.mediaFilesCount)
                                  .arg(tel.vbusVoltageMv)
                                  .arg(tel.rssiLast)
                                  .arg(tel.rssiMin)
                                  .arg(tel.packetsSent)
                                  .arg(tel.qsoReceived);
            QByteArray bytes = jsonStr.toUtf8();
            udpSocket.writeDatagram(bytes.data(), bytes.size(), QHostAddress(udpAddress), udpPort);
        }
    }
}

void GEOSCANDecoderBaseband::handleInputMessages()
{
    Message *m;
    while ((m = m_inputMessageQueue.pop()))
    {
        if (MsgConfigureGEOSCANDecoderBaseband::match(*m))
        {
            auto &msg = (MsgConfigureGEOSCANDecoderBaseband &)*m;
            applySettings(msg.getSettingsKeys(), msg.getSettings(), true);
        }
        else if (DSPSignalNotification::match(*m))
        {
            auto &notif = (DSPSignalNotification &)*m;
            m_basebandSampleRate = notif.getSampleRate();

            m_mutex.lock();
            std::atomic_store(&m_lpf, std::make_shared<FIRFilter>(generate_lowpass(m_settings.m_lpfGain, (float)m_effectiveSampleRate, m_settings.m_lpfCutoff, m_settings.m_lpfTransition)));
            m_freqShifter.setFrequency(-(float)m_settings.m_inputFrequencyOffset, (float)m_basebandSampleRate);
            m_mutex.unlock();
        }
        delete m;
    }
}

void GEOSCANDecoderBaseband::applySettings(const QStringList &k, const GEOSCANDecoderSettings &s, bool force)
{
    m_mutex.lock();

    if (force)
        m_settings = s;
    else
        m_settings.applySettings(k, s);

    bool updateLPF = force || k.contains("lpfCutoff") || k.contains("lpfTransition") || k.contains("lpfGain");
    bool updateFreqShift = force || k.contains("inputFrequencyOffset");
    bool needUpdateResampler = force || k.contains("resamplerEnabled") || k.contains("resamplerInputRate") || k.contains("resamplerOutputRate");

    if (updateLPF)
    {
        std::atomic_store(&m_lpf, std::make_shared<FIRFilter>(generate_lowpass(m_settings.m_lpfGain, (float)m_effectiveSampleRate, m_settings.m_lpfCutoff, m_settings.m_lpfTransition)));
    }

    if (updateFreqShift)
    {
        m_freqShifter.setFrequency(-(float)m_settings.m_inputFrequencyOffset, (float)m_basebandSampleRate);
    }

    if (needUpdateResampler)
    {
        updateResampler();
    }

    if (force || k.contains("symSyncType") || k.contains("symSyncSps") || k.contains("symSyncLoopBw") || k.contains("symSyncDamping") || k.contains("symSyncTedGain") || k.contains("symSyncMaxDev"))
    {
        updateSymbolSync();
    }

    m_mutex.unlock();
}

void GEOSCANDecoderBaseband::updateResampler()
{
    if (m_settings.m_resamplerEnabled && m_settings.m_resamplerInputRate > 0 && m_settings.m_resamplerOutputRate > 0)
    {
        double ratio = (double)m_settings.m_resamplerInputRate / (double)m_settings.m_resamplerOutputRate;
        if (m_resampler)
        {
            if (m_resampler->getRatio() != ratio)
            {
                delete m_resampler;
                m_resampler = new FractionalResampler(ratio);
                qWarning() << "[GEOSCAN] Resampler ratio updated:" << ratio;
            }
        }
        else
        {
            m_resampler = new FractionalResampler(ratio);
            qWarning() << "[GEOSCAN] Resampler enabled, ratio:" << ratio;
        }
        m_effectiveSampleRate = m_settings.m_resamplerOutputRate;
    }
    else
    {
        if (m_resampler)
        {
            delete m_resampler;
            m_resampler = nullptr;
            qWarning() << "[GEOSCAN] Resampler disabled";
        }
        m_effectiveSampleRate = m_basebandSampleRate;
    }

    std::atomic_store(&m_lpf, std::make_shared<FIRFilter>(generate_lowpass(m_settings.m_lpfGain, (float)m_effectiveSampleRate, m_settings.m_lpfCutoff, m_settings.m_lpfTransition)));
}

void GEOSCANDecoderBaseband::updateSymbolSync()
{
    bool wasEnabled = m_symSyncEnabled;
    bool nowEnabled = true;
    m_symSyncEnabled = nowEnabled;

    if (!wasEnabled && !nowEnabled)
        return;

    if (nowEnabled && !wasEnabled)
    {
        m_symbolSync = new SymbolSync(
            m_settings.m_symSyncSps,
            m_settings.m_symSyncLoopBw,
            m_settings.m_symSyncDamping,
            m_settings.m_symSyncTedGain,
            m_settings.m_symSyncMaxDev
        );
        return;
    }

    if (!nowEnabled && wasEnabled)
    {
        delete m_symbolSync;
        m_symbolSync = nullptr;
        return;
    }

    m_symbolSync->setSps(m_settings.m_symSyncSps);
    m_symbolSync->setLoopParams(
        m_settings.m_symSyncLoopBw,
        m_settings.m_symSyncDamping,
        m_settings.m_symSyncTedGain
    );
    m_symbolSync->setMaxDev(m_settings.m_symSyncMaxDev);
    m_symbolSync->reset();
}

void GEOSCANDecoderBaseband::handleData() {}
bool GEOSCANDecoderBaseband::handleMessage(const Message &)
{
    return false;
}

bool GEOSCANDecoderBaseband::checkCRC16(const std::vector<uint8_t> &data)
{
    if (data.size() < 2)
        return false;
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < data.size() - 2; ++i)
    {
        crc ^= ((uint16_t)data[i]) << 8;
        for (int b = 0; b < 8; ++b)
        {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x8005;
            else
                crc = crc << 1;
        }
    }
    uint16_t stored = (((uint16_t)data[data.size() - 2]) << 8) | data[data.size() - 1];
    return crc == stored;
}
