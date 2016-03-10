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

#include <string.h>
#include <ctype.h>
#include <re.h>
#include "avs_log.h"
#include "avs_jzon.h"
#include "avs_store.h"
#include "avs_rest.h"


enum {
	MAX_CHUNK_SIZE = 65536,
	MAX_CHUNKS     = 256,
	MIN_HDR_SIZE   = 2,
};


/**
 * Defines a chunked decoder for HTTP transfer
 *
 * the decoder can handle re-assembly of small TCP packets
 * that is all appended to the buffer. when the terminating
 * chunk has arrived (length = 0), then you can decode the whole
 * buffer and get your application data (aka 'unchunking')
 *
 *
 *    App-payload  [..........................]
 *
 *    chunks       [H............] [H...........] [0]
 *
 *    TCP-data     [...] [...] [...] [...]
 *
 */
struct chunk_decoder {
	struct mbuf *mb;
};


/*
 * Decode a chunk header, which is a hexadecimal length followed
 * by CRLF:
 *
 *     bd0\r\n
 *     DATA_FOLLOWS_HERE
 */

static int len_decode(size_t *lenp, struct mbuf *mb)
{
	struct pl pl, hdr, end;
	size_t hdr_len;

	if (mbuf_get_left(mb) < MIN_HDR_SIZE)
		return EBADMSG;

	pl_set_mbuf(&pl, mb);

	if (re_regex(pl.p, pl.l,
		     "[0-9a-f]+[\r]*[\n]1",
		     &hdr, NULL, &end) || hdr.p != pl.p) {

		return EBADMSG;
	}

	hdr_len = end.p + end.l - pl.p;

	if (lenp) {
		*lenp = pl_x32(&hdr);
	}

	mbuf_advance(mb, hdr_len);

	return 0;
}


int chunk_encode(struct mbuf *mb, const uint8_t *p, size_t len)
{
	int err;

	if (!mb || len > MAX_CHUNK_SIZE)
		return EINVAL;

	err = mbuf_printf(mb, "%x\r\n", len);
	if (p)
		err |= mbuf_write_mem(mb, p, len);
	err |= mbuf_printf(mb, "\r\n");

	return err;
}


int chunk_decode(uint8_t **bufp, size_t *lenp, struct mbuf *mb_in)
{
	size_t len;
	size_t start;
	int err = 0;

	if (!mb_in)
		return EINVAL;

	if (mbuf_get_left(mb_in) < 2)
		return EBADMSG;

	start = mb_in->pos;
	err = len_decode(&len, mb_in);
	if (err)
		goto out;

	if (len > MAX_CHUNK_SIZE) {
		err = EOVERFLOW;
		goto out;
	}

	/* make sure there is enough data in the buffer */
	if ((len+2) > mbuf_get_left(mb_in)) {
		err = EBADMSG;
		goto out;
	}

	/* Write the content without the trailing '\r\n' */
	if (bufp)
		*bufp = mbuf_buf(mb_in);
	if (lenp)
		*lenp = len;

	if ((mb_in->pos + len + 2) > mb_in->end) {
		warning("[%zu bytes] -- pos exceeds end by %zd bytes\n",
			      len, mb_in->pos + len + 2 - mb_in->end);
	}

	mbuf_advance(mb_in, len+2);

 out:
	/* rewind the buffer in case of errors */
	if (err)
		mb_in->pos = start;

	return err;
}


static void destructor(void *arg)
{
	struct chunk_decoder *dec = arg;

	mem_deref(dec->mb);
}


int chunk_decoder_alloc(struct chunk_decoder **decp)
{
	struct chunk_decoder *dec;
	int err = 0;

	if (!decp)
		return EINVAL;

	dec = mem_zalloc(sizeof(*dec), destructor);
	if (!dec)
		return ENOMEM;

	dec->mb = mbuf_alloc(4096);
	if (!dec->mb) {
		err = ENOMEM;
		goto out;
	}

 out:
	if (err)
		mem_deref(dec);
	else
		*decp = dec;

	return err;
}


int chunk_decoder_append_data(struct chunk_decoder *dec,
			      const uint8_t *data, size_t len)
{
	int err;

	if (!dec || !data)
		return EINVAL;

	/* append data to the end of the re-assembly buffer */
	dec->mb->pos = dec->mb->end;
	err = mbuf_write_mem(dec->mb, data, len);
	if (err)
		return err;

	return err;
}


/* count the number of valid chunks (including len=0 terminator) */
size_t chunk_decoder_count_chunks(const struct chunk_decoder *dec)
{
	size_t n = 0;

	if (!dec)
		return 0;

	dec->mb->pos = 0;

	for (int i=0; i<MAX_CHUNKS; i++) {

		if (chunk_decode(NULL, NULL, dec->mb))
			break;

		++n;
	}

	return n;
}


bool chunk_decoder_is_final(const struct chunk_decoder *dec)
{
	if (!dec)
		return false;

	dec->mb->pos = 0;

	for (int i=0; i<MAX_CHUNKS; i++) {

		size_t len;

		if (chunk_decode(NULL, &len, dec->mb))
			break;

		if (len == 0)
			return true;
	}

	return false;
}


size_t chunk_decoder_length(const struct chunk_decoder *dec)
{
	size_t total = 0;

	if (!dec)
		return 0;

	dec->mb->pos = 0;

	for (int i=0; i<MAX_CHUNKS; i++) {

		size_t len;

		if (chunk_decode(NULL, &len, dec->mb))
			break;

		total += len;

		if (len == 0)
			break;
	}

	return total;
}


int chunk_decoder_unchunk(struct chunk_decoder *dec, struct mbuf *mb)
{
	int err = 0;

	if (!dec || !mb)
		return EINVAL;

	dec->mb->pos = 0;

	for (int i=0; i<MAX_CHUNKS; i++) {

		uint8_t *p;
		size_t len;

		err = chunk_decode(&p, &len, dec->mb);
		if (err)
			break;

		if (len == 0)
			break;

		err = mbuf_write_mem(mb, p, len);
		if (err)
			break;
	}

	return err;
}
