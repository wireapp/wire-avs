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


/*
 * This sample message was sniffed from an incoming OTR-message from iOS
 */
static const uint8_t sample_protobuf[] = {
	0x0a, 0x24, 0x30, 0x38, 0x61, 0x31, 0x64, 0x36,
	0x35, 0x36, 0x2d, 0x39, 0x63, 0x34, 0x32, 0x2d,
	0x34, 0x36, 0x30, 0x32, 0x2d, 0x61, 0x39, 0x62,
	0x33, 0x2d, 0x30, 0x37, 0x32, 0x31, 0x65, 0x31,
	0x33, 0x65, 0x32, 0x65, 0x62, 0x61, 0x12, 0x11,
	0x0a, 0x0f, 0x57, 0x68, 0x69, 0x74, 0x65, 0x20,
	0x66, 0x6f, 0x78, 0x20, 0x68, 0x75, 0x72, 0x72,
	0x61,
};


TEST(protobuf, decode_and_encode)
{
	GenericMessage *msg;
	Text *text;
	uint8_t buf[256];
	size_t sz, n;

	sz = sizeof(sample_protobuf);

	/* decode */
	msg = generic_message_decode(sz, sample_protobuf);
	ASSERT_TRUE(msg != NULL);

	ASSERT_STREQ("08a1d656-9c42-4602-a9b3-0721e13e2eba", msg->message_id);
	ASSERT_EQ(GENERIC_MESSAGE__CONTENT_TEXT, msg->content_case);

	text = msg->text;

	ASSERT_TRUE(text != NULL);
	ASSERT_STREQ("White fox hurra", text->content);
	ASSERT_EQ(0, text->n_mention);

	/* encode */
	n = generic_message__get_packed_size(msg);
	ASSERT_TRUE(n < sizeof(buf) );
	ASSERT_EQ(sz, n);

	n = generic_message__pack(msg, buf);
	ASSERT_EQ(sz, n);

#if 0
	hexdump(stderr, buf, n);
#endif

	ASSERT_EQ(0, memcmp(buf, sample_protobuf, n));

	generic_message_free(msg);
}
