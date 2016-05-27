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


TEST(packetqueue, 1)
{
	packet_queue_t *pq = 0;
	packet_type_t packet_type;
	uint8_t *packet_data;
	size_t packet_size;
	int err;

	err = packet_queue_alloc(&pq, false);
	ASSERT_EQ(0, err);
	ASSERT_TRUE(pq != NULL);

	// empty queue
	err = packet_queue_pop(pq, &packet_type, &packet_data, &packet_size);
	ASSERT_EQ(ENODATA, err);

	err = packet_queue_push(pq, PACKET_TYPE_RTP, (uint8_t *)"RTP", 3);
	ASSERT_EQ(0, err);
	err = packet_queue_push(pq, PACKET_TYPE_RTCP, (uint8_t *)"RTCP", 4);
	ASSERT_EQ(0, err);

	// should be 2 items now

	err = packet_queue_pop(pq, &packet_type, &packet_data, &packet_size);
	ASSERT_EQ(0, err);
	ASSERT_EQ(PACKET_TYPE_RTP, packet_type);
	ASSERT_EQ(3, packet_size);
	ASSERT_TRUE(0 == memcmp("RTP", packet_data, 3));
	mem_deref(packet_data);

	err = packet_queue_pop(pq, &packet_type, &packet_data, &packet_size);
	ASSERT_EQ(0, err);
	ASSERT_EQ(PACKET_TYPE_RTCP, packet_type);
	ASSERT_EQ(4, packet_size);
	ASSERT_TRUE(0 == memcmp("RTCP", packet_data, 4));
	mem_deref(packet_data);

	// empty queue
	err = packet_queue_pop(pq, &packet_type, &packet_data, &packet_size);
	ASSERT_EQ(ENODATA, err);

	mem_deref(pq);
}
