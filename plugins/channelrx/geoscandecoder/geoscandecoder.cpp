#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QThread>

#include "dsp/dspcommands.h"
#include "device/deviceapi.h"
#include "maincore.h"

#include "geoscandecoder.h"

MESSAGE_CLASS_DEFINITION(GEOSCANDecoder::MsgConfigureGEOSCANDecoder, Message)

const char* const GEOSCANDecoder::m_channelIdURI = "sdrangel.channel.geoscandecoder";
const char* const GEOSCANDecoder::m_channelId    = "GEOSCANDecoder";
const int         GEOSCANDecoder::m_udpBlockSize  = 512;

GEOSCANDecoder::GEOSCANDecoder(DeviceAPI* deviceAPI) :
    ChannelAPI(m_channelIdURI, ChannelAPI::StreamSingleSink),
    m_deviceAPI(deviceAPI),
    m_thread(nullptr),
    m_basebandSink(nullptr),
    m_running(false),
    m_basebandSampleRate(0)
{
    qDebug("GEOSCANDecoder::GEOSCANDecoder");
    setObjectName(m_channelId);
    applySettings(QStringList(), m_settings, true);

    m_deviceAPI->addChannelSink(this);
    m_deviceAPI->addChannelSinkAPI(this);

    m_networkManager = new QNetworkAccessManager();
    QObject::connect(
        this, &ChannelAPI::indexInDeviceSetChanged,
        this, &GEOSCANDecoder::handleIndexInDeviceSetChanged);
}

GEOSCANDecoder::~GEOSCANDecoder()
{
    m_deviceAPI->removeChannelSinkAPI(this);
    m_deviceAPI->removeChannelSink(this, true);
    stop();
    delete m_networkManager;
}

void GEOSCANDecoder::setDeviceAPI(DeviceAPI* deviceAPI)
{
    if (deviceAPI != m_deviceAPI)
    {
        m_deviceAPI->removeChannelSinkAPI(this);
        m_deviceAPI->removeChannelSink(this, false);
        m_deviceAPI = deviceAPI;
        m_deviceAPI->addChannelSink(this);
        m_deviceAPI->addChannelSinkAPI(this);
    }
}

uint32_t GEOSCANDecoder::getNumberOfDeviceStreams() const
{
    return m_deviceAPI->getNbSourceStreams();
}

void GEOSCANDecoder::feed(const SampleVector::const_iterator& begin,
                          const SampleVector::const_iterator& end, bool firstOfBurst)
{
    (void)firstOfBurst;

    if (m_running)
        m_basebandSink->feed(begin, end);
}

void GEOSCANDecoder::getMagSqLevels(double& avg, double& peak, int& nbSamples) const
{
    if (m_basebandSink)
        m_basebandSink->getMagSqLevels(avg, peak, nbSamples);
    else
        { avg = 0.0; peak = 0.0; nbSamples = 1; }
}

void GEOSCANDecoder::start()
{
    if (m_running) return;

    qDebug() << "GEOSCANDecoder::start";
    m_thread = new QThread();
    m_basebandSink = new GEOSCANDecoderBaseband();
    m_basebandSink->setFifoLabel(QString("%1 [%2:%3]")
        .arg(m_channelId)
        .arg(m_deviceAPI->getDeviceSetIndex())
        .arg(getIndexInDeviceSet()));
    m_basebandSink->setChannel(this);
    m_basebandSink->setMessageQueueToGUI(getMessageQueueToGUI());
    m_basebandSink->moveToThread(m_thread);

    QObject::connect(m_thread, &QThread::finished,
                     m_basebandSink, &QObject::deleteLater);
    QObject::connect(m_thread, &QThread::finished,
                     m_thread,      &QThread::deleteLater);

    if (m_basebandSampleRate != 0)
        m_basebandSink->setBasebandSampleRate(m_basebandSampleRate);

    m_thread->start();

    auto* msg = GEOSCANDecoderBaseband::MsgConfigureGEOSCANDecoderBaseband::create(
        QStringList(), m_settings, true);
    m_basebandSink->getInputMessageQueue()->push(msg);

    m_running = true;
}

void GEOSCANDecoder::stop()
{
    if (!m_running) return;
    qDebug() << "GEOSCANDecoder::stop";
    m_running = false;
    m_thread->quit();
    m_thread->wait();
    m_basebandSink = nullptr;
    m_thread = nullptr;
}

bool GEOSCANDecoder::handleMessage(const Message& cmd)
{
    if (MsgConfigureGEOSCANDecoder::match(cmd))
    {
        auto& cfg = (MsgConfigureGEOSCANDecoder&)cmd;
        qDebug() << "GEOSCANDecoder::handleMessage: MsgConfigureGEOSCANDecoder";
        applySettings(cfg.getSettingsKeys(), cfg.getSettings(), cfg.getForce());
        return true;
    }
    else if (DSPSignalNotification::match(cmd))
    {
        auto& notif = (DSPSignalNotification&)cmd;
        m_basebandSampleRate = notif.getSampleRate();

        if (m_running)
            m_basebandSink->getInputMessageQueue()->push(new DSPSignalNotification(notif));
        if (getMessageQueueToGUI())
            getMessageQueueToGUI()->push(new DSPSignalNotification(notif));

        return true;
    }
    else if (MainCore::MsgChannelDemodQuery::match(cmd))
    {
        return true;
    }
    return false;
}

void GEOSCANDecoder::setCenterFrequency(qint64 frequency)
{
    GEOSCANDecoderSettings settings = m_settings;
    settings.m_inputFrequencyOffset = frequency;
    applySettings(QStringList("inputFrequencyOffset"), settings, false);
}

void GEOSCANDecoder::applySettings(const QStringList& settingsKeys,
                                   const GEOSCANDecoderSettings& settings, bool force)
{
    qDebug() << "GEOSCANDecoder::applySettings:" << settings.getDebugString(settingsKeys, force);

    if (m_running) {
        auto* msg = GEOSCANDecoderBaseband::MsgConfigureGEOSCANDecoderBaseband::create(
            settingsKeys, settings, force);
        m_basebandSink->getInputMessageQueue()->push(msg);
    }

    if (force) {
        m_settings = settings;
    } else {
        m_settings.applySettings(settingsKeys, settings);
    }
}

QByteArray GEOSCANDecoder::serialize() const   { return m_settings.serialize(); }
bool GEOSCANDecoder::deserialize(const QByteArray& data)
{
    if (!m_settings.deserialize(data)) {
        m_settings.resetToDefaults();
        return false;
    }
    MsgConfigureGEOSCANDecoder* msg =
        MsgConfigureGEOSCANDecoder::create(QStringList(), m_settings, true);
    m_inputMessageQueue.push(msg);
    return true;
}

// WebAPI — заглушки
int GEOSCANDecoder::webapiSettingsGet(SWGSDRangel::SWGChannelSettings& response, QString& errorMessage)
    { (void)response; (void)errorMessage; return 501; }
int GEOSCANDecoder::webapiWorkspaceGet(SWGSDRangel::SWGWorkspaceInfo& response, QString& errorMessage)
    { (void)response; (void)errorMessage; return 501; }
int GEOSCANDecoder::webapiSettingsPutPatch(bool force, const QStringList& keys,
    SWGSDRangel::SWGChannelSettings& response, QString& errorMessage)
    { (void)force; (void)keys; (void)response; (void)errorMessage; return 501; }
int GEOSCANDecoder::webapiReportGet(SWGSDRangel::SWGChannelReport& response, QString& errorMessage)
    { (void)response; (void)errorMessage; return 501; }

void GEOSCANDecoder::webapiFormatChannelSettings(SWGSDRangel::SWGChannelSettings& response,
    const GEOSCANDecoderSettings& settings) { (void)response; (void)settings; }
void GEOSCANDecoder::webapiUpdateChannelSettings(GEOSCANDecoderSettings& settings,
    const QStringList& keys, SWGSDRangel::SWGChannelSettings& response)
    { (void)settings; (void)keys; (void)response; }

void GEOSCANDecoder::networkManagerFinished(QNetworkReply* reply) { reply->deleteLater(); }

void GEOSCANDecoder::handleIndexInDeviceSetChanged(int index)
{
    if (!m_running || index < 0) return;
    m_basebandSink->setFifoLabel(QString("%1 [%2:%3]")
        .arg(m_channelId)
        .arg(m_deviceAPI->getDeviceSetIndex())
        .arg(index));
}
