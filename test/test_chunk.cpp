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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <re.h>
#include <avs.h>
#include "gtest/gtest.h"


static const char *encoded_data =
	  "4\r\n"
	  "Wiki\r\n"
	  "5\r\n"
	  "pedia\r\n"
	  "e\r\n"
	  " in\r\n\r\nchunks.\r\n"
	  "0\r\n"
	  "\r\n";

static const char *decoded_data =
	"Wikipedia in\r\n"
	"\r\n"
	"chunks.";


static struct mbuf *mbuf_load_file(const char *filename)
{
	struct mbuf *mb = mbuf_alloc(2048);
	int err = 0, fd = open(filename, O_RDONLY);
	if (fd < 0) {
		warning("could not open '%s' (%m)\n", filename, errno);
		return 0;
	}

	for (;;) {
		uint8_t buf[1024];

		const ssize_t n = read(fd, (void *)buf, sizeof(buf));
		if (n < 0) {
			err = errno;
			break;
		}
		else if (n == 0)
			break;

		err |= mbuf_write_mem(mb, buf, n);
	}

	(void)close(fd);

	if (err)
		return NULL;

	mb->pos = 0;

	return mb;
}


TEST(chunk, encode_message)
{
	struct mbuf *mb = mbuf_alloc(1024);
	int err = 0;

	err |= chunk_encode(mb, (uint8_t *)"Wiki", 4);
	err |= chunk_encode(mb, (uint8_t *)"pedia", 5);
	err |= chunk_encode(mb, (uint8_t *)" in\r\n\r\nchunks.", 14);
	err |= chunk_encode(mb, (uint8_t *)NULL, 0);
	ASSERT_EQ(0, err);

	ASSERT_EQ(str_len(encoded_data), mb->end);
	ASSERT_TRUE(0 == memcmp(encoded_data, mb->buf, mb->end));

	mem_deref(mb);
}


TEST(chunk, decode_message)
{
	struct mbuf *mb_chunked = mbuf_alloc(1024);
	struct mbuf *mb_data = mbuf_alloc(1024);
	int err = 0;

	ASSERT_TRUE(mb_chunked != NULL);
	ASSERT_TRUE(mb_data != NULL);

	mbuf_write_str(mb_chunked, encoded_data);
	mb_chunked->pos = 0;

	/* decode all chunks */
	for (int i=0; i<16; i++) {
		uint8_t *p;
		size_t n;

		err = chunk_decode(&p, &n, mb_chunked);
		if (err)
			break;

		if (n == 0)
			break;

		err = mbuf_write_mem(mb_data, p, n);
		if (err)
			break;
	}

	ASSERT_EQ(str_len(decoded_data), mb_data->end);
	ASSERT_TRUE(0 == memcmp(decoded_data, mb_data->buf, mb_data->end));

	mem_deref(mb_chunked);
	mem_deref(mb_data);
}


TEST(chunk, decode_too_short)
{
	struct mbuf *mb_chunked = mbuf_alloc(1024);
	int err = 0;

	mbuf_write_str(mb_chunked, "3\r\nab");
	mb_chunked->pos = 0;

	err = chunk_decode(NULL, NULL, mb_chunked);
	ASSERT_EQ(EBADMSG, err);

	mem_deref(mb_chunked);
}


TEST(chunk, decode_incomplete_header)
{
	struct mbuf *mb = mbuf_alloc(1024);
	int err = 0;

	mbuf_write_str(mb, "c8"); /* the CRLF is missing here */
	mb->pos = 0;

	err = chunk_decode(NULL, NULL, mb);
	ASSERT_EQ(EBADMSG, err);

	mem_deref(mb);
}


TEST(chunk, decode_invalid_header)
{
	struct mbuf *mb = mbuf_alloc(1024);
	int err = 0;

	mbuf_write_str(mb, "alfred\r\n");
	mb->pos = 0;

	err = chunk_decode(NULL, NULL, mb);
	ASSERT_EQ(EBADMSG, err);

	mem_deref(mb);
}

TEST(chunk, decoder)
{
	struct chunk_decoder *dec = NULL;
	int err;

	err = chunk_decoder_alloc(&dec);
	ASSERT_EQ(0, err);

	ASSERT_EQ(0, chunk_decoder_count_chunks(dec));
	ASSERT_FALSE(chunk_decoder_is_final(dec));

	/* 1 chunk */
	err = chunk_decoder_append_data(dec, (uint8_t *)"1\r\nA\r\n", 6);
	ASSERT_EQ(0, err);
	ASSERT_EQ(1, chunk_decoder_count_chunks(dec));
	ASSERT_EQ(1, chunk_decoder_length(dec));
	ASSERT_FALSE(chunk_decoder_is_final(dec));

	/* 2 chunks */
	err = chunk_decoder_append_data(dec, (uint8_t *)"2\r\nBC\r\n", 7);
	ASSERT_EQ(0, err);
	ASSERT_EQ(2, chunk_decoder_count_chunks(dec));
	ASSERT_EQ(3, chunk_decoder_length(dec));
	ASSERT_FALSE(chunk_decoder_is_final(dec));

	/* 3 chunks */
	err = chunk_decoder_append_data(dec, (uint8_t *)"0\r\n\r\n", 5);
	ASSERT_EQ(0, err);
	ASSERT_EQ(3, chunk_decoder_count_chunks(dec));
	ASSERT_EQ(3, chunk_decoder_length(dec));
	ASSERT_TRUE(chunk_decoder_is_final(dec));

	mem_deref(dec);
}


TEST(chunk, decoder_from_file)
{
	struct chunk_decoder *dec = NULL;
	struct mbuf *mb2=0, *mb3=0, *mb4, *mb_payload, *mb_ref;
	int err;

	err = chunk_decoder_alloc(&dec);
	ASSERT_EQ(0, err);

	/* load chunk2 */
	mb2 = mbuf_load_file("./test/data/chunk2");
	ASSERT_TRUE(mb2 != NULL);
	err = chunk_decoder_append_data(dec, mb2->buf, mb2->end);
	ASSERT_EQ(0, err);
	ASSERT_EQ(2, chunk_decoder_count_chunks(dec));
	ASSERT_EQ(10924, chunk_decoder_length(dec));
	ASSERT_FALSE(chunk_decoder_is_final(dec));

	/* load chunk3 */
	mb3 = mbuf_load_file("./test/data/chunk3");
	ASSERT_TRUE(mb3 != NULL);
	err = chunk_decoder_append_data(dec, mb3->buf, mb3->end);
	ASSERT_EQ(0, err);
	ASSERT_EQ(6, chunk_decoder_count_chunks(dec));
	ASSERT_EQ(29948, chunk_decoder_length(dec));
	ASSERT_FALSE(chunk_decoder_is_final(dec));

	/* load chunk4 */
	mb4 = mbuf_load_file("./test/data/chunk4");
	ASSERT_TRUE(mb4 != NULL);
	err = chunk_decoder_append_data(dec, mb4->buf, mb4->end);
	ASSERT_EQ(0, err);
	ASSERT_EQ(8, chunk_decoder_count_chunks(dec));
	ASSERT_EQ(36142, chunk_decoder_length(dec));
	ASSERT_TRUE(chunk_decoder_is_final(dec));

	/* finally, decode the whole buffer */
	mb_payload = mbuf_alloc(8192);
	err = chunk_decoder_unchunk(dec, mb_payload);
	ASSERT_EQ(0, err);
	ASSERT_EQ(36142, mb_payload->end);

	mb_ref = mbuf_load_file("./test/data/chunk_total");
	ASSERT_TRUE(mb_ref != NULL);
	ASSERT_EQ(36142, mb_ref->end);
	ASSERT_TRUE(0 == memcmp(mb_ref->buf, mb_payload->buf, mb_ref->end));

	mem_deref(dec);
	mem_deref(mb4);
	mem_deref(mb3);
	mem_deref(mb2);
	mem_deref(mb_payload);
	mem_deref(mb_ref);
}
