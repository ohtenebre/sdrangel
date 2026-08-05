///////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2012 maintech GmbH, Otto-Hahn-Str. 15, 97204 Hoechberg, Germany     //
// written by Christian Daniel                                                       //
// Copyright (C) 2015-2022 Edouard Griffiths, F4EXB <f4exb06@gmail.com>              //
// Copyright (C) 2019 Davide Gerhard <rainbow@irh.it>                                //
// Copyright (C) 2020 Kacper Michajłow <kasper93@gmail.com>                          //
//                                                                                   //
// This program is free software; you can redistribute it and/or modify              //
// it under the terms of the GNU General Public License as published by              //
// the Free Software Foundation as version 3 of the License, or                      //
// (at your option) any later version.                                               //
//                                                                                   //
// This program is distributed in the hope that it will be useful,                   //
// but WITHOUT ANY WARRANTY; without even the implied warranty of                    //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                      //
// GNU General Public License V3 for more details.                                   //
//                                                                                   //
// You should have received a copy of the GNU General Public License                 //
// along with this program. If not, see <http://www.gnu.org/licenses/>.              //
///////////////////////////////////////////////////////////////////////////////////////
#include "geoscandecoderplugin.h"
#include "plugin/pluginapi.h"

#include <QtPlugin>

#ifndef SERVER_MODE
#include "geoscandecoder_gui.h"
#endif
#include "geoscandecoder.h"
#include "geoscandecoderplugin.h"

const PluginDescriptor GEOSCANDecoderPlugin::m_pluginDescriptor = {
    GEOSCANDecoder::m_channelId,
    QStringLiteral("GEOSCAN SATELLITES DECODER"),
    QStringLiteral("7.23.1"),
    QStringLiteral("(c) Edouard Griffiths, F4EXB"),
    QStringLiteral("https://github.com/f4exb/sdrangel"),
    true,
    QStringLiteral("https://github.com/f4exb/sdrangel")
};

GEOSCANDecoderPlugin::GEOSCANDecoderPlugin(QObject *parent) : QObject(parent),
                                                              m_pluginAPI(0)
{
}

const PluginDescriptor &GEOSCANDecoderPlugin::getPluginDescriptor() const
{
    return m_pluginDescriptor;
}

void GEOSCANDecoderPlugin::initPlugin(PluginAPI *pluginAPI)
{
    m_pluginAPI = pluginAPI;

    // register NFM demodulator
    m_pluginAPI->registerRxChannel(GEOSCANDecoder::m_channelIdURI, GEOSCANDecoder::m_channelId, this);
}

void GEOSCANDecoderPlugin::createRxChannel(DeviceAPI *deviceAPI, BasebandSampleSink **bs, ChannelAPI **cs) const
{
    if (bs || cs)
    {
        GEOSCANDecoder *instance = new GEOSCANDecoder(deviceAPI);

        if (bs)
        {
            *bs = instance;
        }

        if (cs)
        {
            *cs = instance;
        }
    }
}

#ifdef SERVER_MODE
ChannelGUI *GEOSCANDecoderPlugin::createRxChannelGUI(
    DeviceUISet *deviceUISet,
    BasebandSampleSink *rxChannel
) const
{
    (void)deviceUISet;
    (void)rxChannel;
    return nullptr;
}
#else
ChannelGUI *GEOSCANDecoderPlugin::createRxChannelGUI(DeviceUISet *deviceUISet, BasebandSampleSink *rxChannel) const
{
    return GEOSCANDecoderGUI::create(m_pluginAPI, deviceUISet, rxChannel);
}
#endif

ChannelWebAPIAdapter *GEOSCANDecoderPlugin::createChannelWebAPIAdapter() const
{
    return nullptr;
}
