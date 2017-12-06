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


int protobuf_encode_text(uint8_t *pbuf, size_t *pbuf_len,
			 const char *text_content)
{
	GenericMessage msg;
	Text text;
	size_t sz;
	int err;

	if (!pbuf || !pbuf_len || !text_content)
		return EINVAL;

	generic_message__init(&msg);
	text__init(&text);

	err = uuid_v4(&msg.message_id);
	if (err)
		return err;

	text.content = (char *)text_content;

	msg.content_case = GENERIC_MESSAGE__CONTENT_TEXT;
	msg.text         = &text;

	sz = generic_message__get_packed_size(&msg);
	if (sz > *pbuf_len) {
		warning("buffer too small for protobuf\n");
		err = EOVERFLOW;
		goto out;
	}

	sz = generic_message__pack(&msg, pbuf);
	if (!sz) {
		warning("generic_message__pack failed\n");
		err = EPROTO;
		goto out;
	}

	*pbuf_len = sz;

 out:
	mem_deref(msg.message_id);
	return err;
}


int protobuf_encode_calling(uint8_t *pbuf, size_t *pbuf_len,
			    const char *content)
{
	GenericMessage msg;
	Calling calling;
	size_t sz;
	int err;

	if (!pbuf || !pbuf_len || !content)
		return EINVAL;

	generic_message__init(&msg);
	calling__init(&calling);

	err = uuid_v4(&msg.message_id);
	if (err)
		return err;

	calling.content = (char *)content;

	msg.content_case = GENERIC_MESSAGE__CONTENT_CALLING;
	msg.calling      = &calling;

	sz = generic_message__get_packed_size(&msg);
	if (sz > *pbuf_len) {
		warning("buffer too small for protobuf\n");
		err = EOVERFLOW;
		goto out;
	}

	sz = generic_message__pack(&msg, pbuf);
	if (!sz) {
		warning("generic_message__pack failed\n");
		err = EPROTO;
		goto out;
	}

	*pbuf_len = sz;

 out:
	mem_deref(msg.message_id);
	return err;
}


static void destructor(void *data)
{
	struct protobuf_msg *msg = data;

	if (msg->gm)
		generic_message_free(msg->gm);
}


int protobuf_decode(struct protobuf_msg **msgp, const uint8_t *buf, size_t len)
{
	struct protobuf_msg *msg;
	int err = 0;

	if (!msgp || !buf || !len)
		return EINVAL;

	msg = mem_zalloc(sizeof(*msg), destructor);
	if (!msg)
		return ENOMEM;

	msg->gm = generic_message_decode(len, buf);
	if (!msg->gm) {
		warning("generic_message_decode failed (%zu bytes)\n", len);
		err = EPROTO;
		goto out;
	}

 out:
	if (err)
		mem_deref(msg);
	else
		*msgp = msg;

	return err;
}


