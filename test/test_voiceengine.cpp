/*
* Wire
* Copyright (C) 2016 Wire Swiss GmbH
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#include <re.h>
#include <avs.h>
#include <gtest/gtest.h>

#include "webrtc/voice_engine/include/voe_base.h"
#include "webrtc/voice_engine/include/voe_network.h"


using namespace webrtc;


static VoENetwork* netw;


class transp : public Transport {

	virtual int SendPacket(int channel,
			       const void *data, int len) override
	{
		if (netw)
			netw->ReceivedRTPPacket(channel, data, len);

		return len;
	}

	virtual int SendRTCPPacket(int channel,
				   const void *data, int len) override
	{
		if (netw)
			netw->ReceivedRTCPPacket(channel, data, len);

		return len;
	}
};


TEST(voiceengine, verify_create_and_release)
{
	transp transp;
	VoiceEngine* voe = VoiceEngine::Create();
	ASSERT_TRUE(voe != NULL);
	VoEBase* base = VoEBase::GetInterface(voe);
	ASSERT_TRUE(base != NULL);
	netw  = VoENetwork::GetInterface(voe);
	ASSERT_TRUE(netw != NULL);
	int r;

	/* setup */
	base->Init();
	int ch = base->CreateChannel();
	ASSERT_TRUE(ch >= 0);
	r = netw->RegisterExternalTransport(ch, transp);
	ASSERT_EQ(0, r);

	/* start the engine */
	r = base->StartReceive(ch);
	ASSERT_EQ(0, r);
	r = base->StartSend(ch);
	ASSERT_EQ(0, r);

	/*  ... wait here or loop ... */
#if 0
	re_printf("press ENTER to stop test\n");
	getchar();
#endif

	base->DeleteChannel(ch);
	base->Terminate();
	base->Release();
	netw->Release(); netw=0;
	VoiceEngine::Delete(voe);
}
