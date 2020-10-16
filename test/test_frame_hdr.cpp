/*
* Wire
* Copyright (C) 2020 Wire Swiss GmbH
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


class FrameHdrTest : public ::testing::Test {

public:

	virtual void SetUp() override
	{
	}

	virtual void TearDown() override
	{
	}

protected:
	uint8_t buffer[128];
};

TEST_F(FrameHdrTest, write_read_inline_key)
{
	size_t bw, br;
	uint64_t f, k;

	bw = frame_hdr_write(buffer, 0x11, 0x03);
	ASSERT_EQ(bw, 3);

	br = frame_hdr_read(buffer, &f, &k);

	ASSERT_EQ(br, bw);
	ASSERT_EQ(k, 0x03);
	ASSERT_EQ(f, 0x11);
}

TEST_F(FrameHdrTest, write_read_external_key)
{
	size_t bw, br;
	uint64_t f, k;

	bw = frame_hdr_write(buffer, 0x11, 0x22);
	ASSERT_EQ(bw, 4);

	br = frame_hdr_read(buffer, &f, &k);

	ASSERT_EQ(br, bw);
	ASSERT_EQ(k, 0x22);
	ASSERT_EQ(f, 0x11);
}

TEST_F(FrameHdrTest, write_read_multibyte)
{
	size_t bw, br;
	uint64_t f, k;

	bw = frame_hdr_write(buffer, 0x224466, 0x11335577);
	ASSERT_EQ(bw, 9);

	br = frame_hdr_read(buffer, &f, &k);

	ASSERT_EQ(br, bw);
	ASSERT_EQ(f, 0x224466);
	ASSERT_EQ(k, 0x11335577);
}

TEST_F(FrameHdrTest, read_buffer_inline)
{
	size_t br;
	uint64_t f, k;

	memcpy(buffer, "\x00\x06\x11", 3);

	br = frame_hdr_read(buffer, &f, &k);
	ASSERT_EQ(br, 3);
	ASSERT_EQ(k, 0x06);
	ASSERT_EQ(f, 0x11);
}

TEST_F(FrameHdrTest, read_buffer_external)
{
	size_t br;
	uint64_t f, k;

	memcpy(buffer, "\x00\x08\x22\x11", 4);

	br = frame_hdr_read(buffer, &f, &k);
	ASSERT_EQ(br, 4);
	ASSERT_EQ(k, 0x22);
	ASSERT_EQ(f, 0x11);
}

TEST_F(FrameHdrTest, read_buffer_skip_extensions1)
{
	size_t br;
	uint64_t f, k;

	memcpy(buffer, "\x10\x1B\x22\x44\x66\x88\x11\x33\x80\x00\x00\x00", 12);

	br = frame_hdr_read(buffer, &f, &k);
	ASSERT_EQ(br, 12);
	ASSERT_EQ(k, 0x22446688);
	ASSERT_EQ(f, 0x1133);
}

TEST_F(FrameHdrTest, read_buffer_skip_extensions2)
{
	size_t br;
	uint64_t f, k;

	memcpy(buffer, "\x10\x1B\x22\x44\x66\x88\x11\x33\x80\x00\x90\x00\x00\x00\x00", 15);

	br = frame_hdr_read(buffer, &f, &k);
	ASSERT_EQ(br, 15);
	ASSERT_EQ(k, 0x22446688);
	ASSERT_EQ(f, 0x1133);
}

