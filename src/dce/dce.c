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
*
* This module of the Wire Software uses software code from
* sctplab/usrsctp (https://github.com/sctplab/usrsctp)
*
** Copyright (C) 2012-2013 Michael Tuexen
** Copyright (C) 2012-2013 Irene Ruengeler
**
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. Neither the name of the project nor the names of its contributors
**    may be used to endorse or promote products derived from this software
**    without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED.	IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
** OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
** HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
** SUCH DAMAGE.
*/


#include <assert.h>
#include <sys/types.h>

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>


//#define SCTP_DEBUG
#include <usrsctplib/usrsctp.h>

#include <re.h>
#include "avs.h"

#define DATA_CHANNEL_PORT 5000

#define LINE_LENGTH (1024)
#define BUFFER_SIZE (1<<16)
#define NUMBER_OF_CHANNELS (10)
#define NUMBER_OF_STREAMS (10)

#define DATA_CHANNEL_PPID_CONTROL   50
#define DATA_CHANNEL_PPID_DOMSTRING 51
#define DATA_CHANNEL_PPID_BINARY    52

#define DATA_CHANNEL_CLOSED     0
#define DATA_CHANNEL_CONNECTING 1
#define DATA_CHANNEL_OPEN       2
#define DATA_CHANNEL_CLOSING    3

#define DATA_CHANNEL_FLAGS_SEND_REQ 0x00000001
#define DATA_CHANNEL_FLAGS_SEND_ACK 0x00000002

#define DATA_CHANNEL_MAX_LABEL_STR_LEN 128
#define DATA_CHANNEL_MAX_PROTOCOL_STR_LEN 128


#define PAYLOAD_MAGIC 0x60504030


enum mq_type {
	ESTAB,
	CH_ESTAB,
	CH_OPEN,
	CH_CLOSE,
	CH_DATA
};

struct payload {
	struct le le;
	enum mq_type type;
	struct dce *dce;           /* pointer */
	struct dce_channel *ch;    /* pointer */
	uint32_t magic;

	union {
		struct {
			uint16_t sid;
		} chopen;

		struct {
			uint8_t *buf;
			size_t len;
		} chdata;
	} v;
};


static struct {
	struct lock *lock;
	struct list dcel;
	struct list pendingl;
	struct mqueue *mqueue;
} g_dce = {
	.lock = NULL
};


struct channel {
	uint32_t id;
	uint32_t pr_value;
	uint16_t pr_policy;
	uint16_t i_stream;
	uint16_t o_stream;
	uint8_t unordered;
	uint8_t state;
	uint32_t flags;
	char label[DATA_CHANNEL_MAX_LABEL_STR_LEN];
	char protocol[DATA_CHANNEL_MAX_PROTOCOL_STR_LEN];
};

struct peer_connection {
	struct channel channels[NUMBER_OF_CHANNELS];
	struct channel *i_stream_channel[NUMBER_OF_STREAMS];
	struct channel *o_stream_channel[NUMBER_OF_STREAMS];
	uint16_t o_stream_buffer[NUMBER_OF_STREAMS];
	uint32_t o_stream_buffer_counter;
	struct lock *lock;
	struct socket *sock;
	bool open_even_sid;
};

struct dce_channel {
	struct le le;
	char label[DATA_CHANNEL_MAX_LABEL_STR_LEN];
	char protocol[DATA_CHANNEL_MAX_PROTOCOL_STR_LEN];
	dce_estab_h *estabh;
	dce_open_chan_h *openh;
	dce_close_chan_h *closeh;
	dce_data_h *datah;
	int id;
	void *arg;
};

struct dce {
	struct socket *sock;
	struct peer_connection pc;

	dce_send_h *sendh;
	dce_estab_h *estabh;
	struct list channell;
	bool snd_dry_event;
	void *arg;

	struct le le; /* member of global active list */

	uint32_t magic;
};

#define DCE_MAGIC 0xdcedce

#define DATA_CHANNEL_OPEN_REQUEST  0x03
#define DATA_CHANNEL_OPEN_ACK      0x02

#define DCOMCT_ORDERED_RELIABLE         0x00
#define DCOMCT_ORDERED_PARTIAL_RTXS     0x01
#define DCOMCT_ORDERED_PARTIAL_TIME     0x02
#define DCOMCT_UNORDERED_RELIABLE       0x80
#define DCOMCT_UNORDERED_PARTIAL_RTXS   0x81
#define DCOMCT_UNORDERED_PARTIAL_TIME   0x82

#define DATA_CHANNEL_FLAG_OUT_OF_ORDER_ALLOWED 0x0001

#ifndef _WIN32
#define SCTP_PACKED __attribute__((packed))
#else
#pragma pack (push, 1)
#define SCTP_PACKED
#endif

static bool dce_inited = false;

struct rtcweb_datachannel_open_request_fixed {
	uint8_t msg_type; /* DATA_CHANNEL_OPEN_REQUEST */
	uint8_t channel_type;
	int16_t priority;
	uint32_t reliability_params;
	int16_t label_length;
	int16_t protocol_length;
} SCTP_PACKED;

struct rtcweb_datachannel_open_request {
	struct rtcweb_datachannel_open_request_fixed fixed;
	uint8_t label_and_protocol[DATA_CHANNEL_MAX_LABEL_STR_LEN + DATA_CHANNEL_MAX_PROTOCOL_STR_LEN];
} SCTP_PACKED;

struct rtcweb_datachannel_ack {
	uint8_t  msg_type; /* DATA_CHANNEL_ACK */
} SCTP_PACKED;

#ifdef _WIN32
#pragma pack()
#endif

#undef SCTP_PACKED


struct sctp_header {
	uint16_t sport;
	uint16_t dport;
	uint32_t vtag;
	uint32_t checksum;
};


/*
 * Mqueue payload
 */

static void payload_destructor(void *arg)
{
	struct payload *pld = arg;

	list_unlink(&pld->le);

	switch (pld->type) {

	case CH_DATA:
		mem_deref(pld->v.chdata.buf);
		break;

	default:
		break;
	}
}


static struct payload *payload_new(struct dce *dce, struct dce_channel *ch,
				   enum mq_type type)
{
	struct payload *pld;

	pld = mem_zalloc(sizeof(*pld), payload_destructor);
	if (!pld)
		return NULL;

	pld->magic = PAYLOAD_MAGIC;
	pld->type = type;
	pld->dce = dce;
	pld->ch = ch;

	list_append(&g_dce.pendingl, &pld->le, pld);

	return pld;
}


static bool exist_dce(struct list *dcel, struct dce *dce)
{
	bool found = false;
	struct le *le;

	le = dcel->head;
	while (le && !found) {
		found = le->data == (void *)dce;
		le = le->next;
	}

	return found;
}

static int sctp_header_decode(struct sctp_header *hdr, struct mbuf *mb)
{
	if (mbuf_get_left(mb) < 12)
		return EBADMSG;

	hdr->sport    = ntohs(mbuf_read_u16(mb));
	hdr->dport    = ntohs(mbuf_read_u16(mb));
	hdr->vtag     = ntohl(mbuf_read_u32(mb));
	hdr->checksum = ntohl(mbuf_read_u32(mb));

	return 0;
}


static int
init_peer_connection(struct peer_connection *pc)
{
	uint32_t i;
	struct channel *channel;
	int err;

	for (i = 0; i < NUMBER_OF_CHANNELS; i++) {
		channel = &(pc->channels[i]);
		channel->id = i;
		channel->state = DATA_CHANNEL_CLOSED;
		channel->pr_policy = SCTP_PR_SCTP_NONE;
		channel->pr_value = 0;
		channel->i_stream = 0;
		channel->o_stream = 0;
		channel->unordered = 0;
		channel->flags = 0;
	}
	for (i = 0; i < NUMBER_OF_STREAMS; i++) {
		pc->i_stream_channel[i] = NULL;
		pc->o_stream_channel[i] = NULL;
		pc->o_stream_buffer[i] = 0;
	}
	pc->o_stream_buffer_counter = 0;
	pc->sock = NULL;

	err = lock_alloc(&pc->lock);
	if (err)
		return err;

	return 0;
}


static void close_peer_connection(struct peer_connection *pc)
{
	if (!pc)
		return;

	pc->lock = mem_deref(pc->lock);
}


static void
lock_peer_connection(struct peer_connection *pc)
{
	lock_write_get(pc->lock);
}


static void
unlock_peer_connection(struct peer_connection *pc)
{
	lock_rel(pc->lock);
}


static struct channel *
find_channel_by_i_stream(struct peer_connection *pc, uint16_t i_stream)
{
	if (i_stream < NUMBER_OF_STREAMS) {
		return pc->i_stream_channel[i_stream];
	} else {
		return NULL;
	}
}


static struct channel *
find_channel_by_o_stream(struct peer_connection *pc, uint16_t o_stream)
{
	if (o_stream < NUMBER_OF_STREAMS) {
		return pc->o_stream_channel[o_stream];
	} else {
		return NULL;
	}
}


static struct channel *
find_free_channel(struct peer_connection *pc)
{
	uint32_t i;

	for (i = 0; i < NUMBER_OF_CHANNELS; i++) {
		if (pc->channels[i].state == DATA_CHANNEL_CLOSED) {
			break;
		}
	}
	if (i == NUMBER_OF_CHANNELS) {
		return NULL;
	} else {
		return &(pc->channels[i]);
	}
}


static bool
valid_o_stream(struct peer_connection *pc, uint16_t o_stream)
{
	struct sctp_status status;
	uint32_t limit;
	socklen_t len;
    
	len = (socklen_t)sizeof(struct sctp_status);
	if (usrsctp_getsockopt(pc->sock, IPPROTO_SCTP, SCTP_STATUS,
			       &status, &len) < 0) {
		warning("dce: getsockopt \n");
		return false;
	}
	if (status.sstat_outstrms < NUMBER_OF_STREAMS) {
		limit = status.sstat_outstrms;
	} else {
		limit = NUMBER_OF_STREAMS;
	}
	if(o_stream > limit){
		return false;
	} else {
		return true;
	}
}

static uint16_t
find_free_o_stream(struct peer_connection *pc)
{
	struct sctp_status status;
	uint32_t i, limit;
	socklen_t len;

	len = (socklen_t)sizeof(struct sctp_status);
	if (usrsctp_getsockopt(pc->sock, IPPROTO_SCTP, SCTP_STATUS,
			       &status, &len) < 0) {
		warning("dce: getsockopt \n");
		return 0;
	}
	if (status.sstat_outstrms < NUMBER_OF_STREAMS) {
		limit = status.sstat_outstrms;
	} else {
		limit = NUMBER_OF_STREAMS;
	}
	/* stream id 0 is reserved */    
	for (i = 1; i < limit; i++) {
		if ((pc->o_stream_channel[i] == NULL) &&
			((i & (uint32_t)1) == (uint32_t)!pc->open_even_sid)){
			break;
		}
	}
	if (i == limit) {
		return 0;
	} else {
		return (uint16_t)i;
	}
}


static void
request_more_o_streams(struct peer_connection *pc)
{
	struct sctp_status status;
	struct sctp_add_streams sas;
	uint32_t i, o_streams_needed;
	socklen_t len;

	o_streams_needed = 0;
	for (i = 0; i < NUMBER_OF_CHANNELS; i++) {
		if ((pc->channels[i].state == DATA_CHANNEL_CONNECTING) &&
		    (pc->channels[i].o_stream == 0)) {
			o_streams_needed++;
		}
	}
	len = (socklen_t)sizeof(struct sctp_status);
	if (usrsctp_getsockopt(pc->sock, IPPROTO_SCTP, SCTP_STATUS, &status, &len) < 0) {
		warning("dce: getsockopt \n");
		return;
	}
	if (status.sstat_outstrms + o_streams_needed > NUMBER_OF_STREAMS) {
		o_streams_needed = NUMBER_OF_STREAMS - status.sstat_outstrms;
	}
	if (o_streams_needed == 0) {
		return;
	}
	memset(&sas, 0, sizeof(struct sctp_add_streams));
	sas.sas_instrms = 0;
	sas.sas_outstrms = (uint16_t)o_streams_needed; /* XXX eror handling */
	if (usrsctp_setsockopt(pc->sock, IPPROTO_SCTP, SCTP_ADD_STREAMS, &sas, (socklen_t)sizeof(struct sctp_add_streams)) < 0) {
		warning("dce: getsockopt \n");
	}
	return;
}

static int
send_open_request_message(struct socket *sock, uint16_t o_stream, uint8_t unordered, uint16_t pr_policy, uint32_t pr_value, const char *label, const char *protocol)
{
	/* XXX: This should be encoded in a better way */
	struct rtcweb_datachannel_open_request req;
	struct sctp_sndinfo sndinfo;
	int16_t label_len;
	int16_t proto_len;
	int var_len;
	uint8_t *ptr;

	memset(&req, 0, sizeof(struct rtcweb_datachannel_open_request));
	req.fixed.msg_type = DATA_CHANNEL_OPEN_REQUEST;
	switch (pr_policy) {
	case SCTP_PR_SCTP_NONE:
		if(unordered){
			req.fixed.channel_type = DCOMCT_UNORDERED_RELIABLE;
		} else {
			req.fixed.channel_type = DCOMCT_ORDERED_RELIABLE;
		}
		break;
	case SCTP_PR_SCTP_TTL:
		if(unordered){
			req.fixed.channel_type = DCOMCT_UNORDERED_PARTIAL_TIME;
		} else {
			req.fixed.channel_type = DCOMCT_ORDERED_PARTIAL_TIME;
		}
		break;
	case SCTP_PR_SCTP_RTX:
		if(unordered){
			req.fixed.channel_type = DCOMCT_UNORDERED_PARTIAL_RTXS;
		} else {
			req.fixed.channel_type = DCOMCT_ORDERED_PARTIAL_RTXS;
		}
		break;
	default:
		return 0;
	}
	label_len = strlen(label);
	proto_len = strlen(protocol);
	req.fixed.reliability_params = htonl((uint32_t)pr_value);
	req.fixed.priority = htons(0); /* XXX: add support */

	req.fixed.label_length = htons(label_len);
	ptr = req.label_and_protocol;
	memcpy(ptr, label, label_len);
	ptr += label_len;
	req.fixed.protocol_length = htons(proto_len);
	memcpy(ptr, protocol, proto_len);
	var_len = label_len + proto_len;
    
	memset(&sndinfo, 0, sizeof(struct sctp_sndinfo));
	sndinfo.snd_sid = o_stream;
	sndinfo.snd_flags = SCTP_EOR;
	sndinfo.snd_ppid = htonl(DATA_CHANNEL_PPID_CONTROL);
	if (usrsctp_sendv(sock, &req,
			  sizeof(req.fixed) + var_len, NULL, 0,
			  &sndinfo, (socklen_t)sizeof(struct sctp_sndinfo),
			  SCTP_SENDV_SNDINFO, 0) < 0) {
		warning("dce: open_request: sctp_sendv failed (%m)\n", errno);
		return 0;
	}
	else {
		return 1;
	}
}


static int
send_open_ack_message(struct socket *sock, uint16_t o_stream)
{
	/* XXX: This should be encoded in a better way */
	struct rtcweb_datachannel_ack ack;
	struct sctp_sndinfo sndinfo;

	memset(&ack, 0, sizeof(struct rtcweb_datachannel_ack));
	ack.msg_type = DATA_CHANNEL_OPEN_ACK;
	memset(&sndinfo, 0, sizeof(struct sctp_sndinfo));
	sndinfo.snd_sid = o_stream;
	sndinfo.snd_flags = SCTP_EOR;
	sndinfo.snd_ppid = htonl(DATA_CHANNEL_PPID_CONTROL);
	if (usrsctp_sendv(sock,
	                  &ack, sizeof(struct rtcweb_datachannel_ack),
	                  NULL, 0,
	                  &sndinfo, (socklen_t)sizeof(struct sctp_sndinfo),
	                  SCTP_SENDV_SNDINFO, 0) < 0) {
		warning("dce: open_ack: sctp_sendv failed (%m)\n", errno);
		return 0;
	} else {
		return 1;
	}
}


static void
send_deferred_messages(struct peer_connection *pc)
{
	uint32_t i;
	struct channel *channel;

	for (i = 0; i < NUMBER_OF_CHANNELS; i++) {
		channel = &(pc->channels[i]);
		if (channel->flags & DATA_CHANNEL_FLAGS_SEND_REQ) {
			if (send_open_request_message(pc->sock, channel->o_stream, channel->unordered, channel->pr_policy, channel->pr_value, channel->label, channel->protocol)) {
				channel->flags &= ~DATA_CHANNEL_FLAGS_SEND_REQ;
			} else {
				if (errno != EAGAIN) {
					/* XXX: error handling */
				}
			}
		}
		if (channel->flags & DATA_CHANNEL_FLAGS_SEND_ACK) {
			if (send_open_ack_message(pc->sock, channel->o_stream)) {
				channel->flags &= ~DATA_CHANNEL_FLAGS_SEND_ACK;
			} else {
				if (errno != EAGAIN) {
					/* XXX: error handling */
				}
			}
		}
	}
	return;
}

static struct channel *
open_channel(struct peer_connection *pc, uint8_t unordered, uint16_t pr_policy, uint32_t pr_value, const char *label, const char *protocol )
{
	struct channel *channel;
	uint16_t o_stream;

	if(strlen(label) > DATA_CHANNEL_MAX_LABEL_STR_LEN){
		warning("dce: label string too long %u max %u\n",
		      strlen(label), DATA_CHANNEL_MAX_LABEL_STR_LEN);
		return NULL;
	}
	if(strlen(protocol) > DATA_CHANNEL_MAX_PROTOCOL_STR_LEN){
		warning("dce: protocol string too long %u max %u  \n",
		      strlen(protocol), DATA_CHANNEL_MAX_PROTOCOL_STR_LEN);
		return NULL;
	}
    
	if ((pr_policy != SCTP_PR_SCTP_NONE) &&
	    (pr_policy != SCTP_PR_SCTP_TTL) &&
	    (pr_policy != SCTP_PR_SCTP_RTX)) {
		return (NULL);
	}
	if ((unordered != 0) && (unordered != 1)) {
		return (NULL);
	}
	if ((pr_policy == SCTP_PR_SCTP_NONE) && (pr_value != 0)) {
		return (NULL);
	}
	if ((channel = find_free_channel(pc)) == NULL) {
		return (NULL);
	}
	o_stream = find_free_o_stream(pc);
	channel->state = DATA_CHANNEL_CONNECTING;
	channel->unordered = unordered;
	channel->pr_policy = pr_policy;
	channel->pr_value = pr_value;
	channel->o_stream = o_stream;
	channel->i_stream = o_stream; // The signalling assumes symmetric stream id_s
	memcpy(channel->label, label, strlen(label));
	memcpy(channel->protocol, protocol, strlen(protocol));
	pc->i_stream_channel[o_stream] = channel;
	channel->flags = 0;
	if (o_stream == 0) {
		request_more_o_streams(pc);
	} else {
		if (send_open_request_message(pc->sock, o_stream, unordered, pr_policy, pr_value, label, protocol)) {
			pc->o_stream_channel[o_stream] = channel;
		} else {
			if (errno == EAGAIN) {
				pc->o_stream_channel[o_stream] = channel;
				channel->flags |= DATA_CHANNEL_FLAGS_SEND_REQ;
			} else {
				channel->state = DATA_CHANNEL_CLOSED;
				channel->unordered = 0;
				channel->pr_policy = 0;
				channel->pr_value = 0;
				channel->o_stream = 0;
				channel->flags = 0;
				channel = NULL;
			}
		}
	}
	return (channel);
}

static int
send_user_message(struct peer_connection *pc, struct channel *channel, const void *message, size_t length)
{
	struct sctp_sendv_spa spa;

	if (channel == NULL) {
		return (0);
	}
	if ((channel->state != DATA_CHANNEL_OPEN) &&
	    (channel->state != DATA_CHANNEL_CONNECTING)) {
		/* XXX: What to do in other states */
		warning("dce: %s Channel %u (%s/%s) is closed \n",
		      __FUNCTION__, channel->id,
		      channel->label, channel->protocol);
		return (0);
	}

	memset(&spa, 0, sizeof(struct sctp_sendv_spa));
	spa.sendv_sndinfo.snd_sid = channel->o_stream;
    
	if ((channel->state == DATA_CHANNEL_OPEN) &&
	    (channel->unordered)) {
		spa.sendv_sndinfo.snd_flags = SCTP_EOR | SCTP_UNORDERED;
	} else {
		spa.sendv_sndinfo.snd_flags = SCTP_EOR;
	}
	spa.sendv_sndinfo.snd_ppid = htonl(DATA_CHANNEL_PPID_DOMSTRING);
	spa.sendv_flags = SCTP_SEND_SNDINFO_VALID;
	if ((channel->pr_policy == SCTP_PR_SCTP_TTL) ||
	    (channel->pr_policy == SCTP_PR_SCTP_RTX)) {
		spa.sendv_prinfo.pr_policy = channel->pr_policy;
		spa.sendv_prinfo.pr_value = channel->pr_value;
		spa.sendv_flags |= SCTP_SEND_PRINFO_VALID;
	}
	if (usrsctp_sendv(pc->sock,
	                  message, length,
	                  NULL, 0,
	                  &spa, (socklen_t)sizeof(struct sctp_sendv_spa),
	                  SCTP_SENDV_SPA, 0) < 0) {
		warning("dce: user: sctp_sendv (%zu bytes) failed (%m)\n", length, errno);
		return -1;
	} else {
		return 0;
	}
}

static void
reset_outgoing_stream(struct peer_connection *pc, uint16_t o_stream)
{
	uint32_t i;

	for (i = 0; i < pc->o_stream_buffer_counter; i++) {
		if (pc->o_stream_buffer[i] == o_stream) {
			return;
		}
	}
	pc->o_stream_buffer[pc->o_stream_buffer_counter++] = o_stream;
	return;
}

static void
send_outgoing_stream_reset(struct peer_connection *pc)
{
	struct sctp_reset_streams *srs;
	uint32_t i;
	size_t len;

	if (pc->o_stream_buffer_counter == 0) {
		return;
	}
	len = sizeof(sctp_assoc_t) + (2 + pc->o_stream_buffer_counter) * sizeof(uint16_t);
	srs = (struct sctp_reset_streams *)malloc(len);
	if (srs == NULL) {
		return;
	}
	memset(srs, 0, len);
	srs->srs_flags = SCTP_STREAM_RESET_OUTGOING;
	srs->srs_number_streams = pc->o_stream_buffer_counter;
	for (i = 0; i < pc->o_stream_buffer_counter; i++) {
		srs->srs_stream_list[i] = pc->o_stream_buffer[i];
	}
	if (usrsctp_setsockopt(pc->sock, IPPROTO_SCTP, SCTP_RESET_STREAMS, srs, (socklen_t)len) < 0) {
		warning("dce: setsockopt \n");
	} else {
		for (i = 0; i < pc->o_stream_buffer_counter; i++) {
			srs->srs_stream_list[i] = 0;
		}
		pc->o_stream_buffer_counter = 0;
	}
	free(srs);
	return;
}

static void
close_channel(struct peer_connection *pc, struct channel *channel)
{
	if (channel == NULL) {
		return;
	}
	if (channel->state != DATA_CHANNEL_OPEN) {
		return;
	}
	reset_outgoing_stream(pc, channel->o_stream);
	send_outgoing_stream_reset(pc);
	channel->state = DATA_CHANNEL_CLOSING;
	return;
}

static struct dce_channel*
get_dce_channel(struct dce *dce, const char *label)
{
	struct le *le;
    
	if (!dce || !label)
		return NULL;
    
	assert(DCE_MAGIC == dce->magic);
    
	for (le = list_head(&dce->channell); le; le = le->next) {
		struct dce_channel *ch = le->data;
        
		if(streq(ch->label, label))
			return ch;
	}
	return NULL;
}


static void
handle_open_request_message(struct dce *dce,
                            struct rtcweb_datachannel_open_request *req,
                            size_t length,
                            uint16_t i_stream)
{
	struct peer_connection *pc = &dce->pc;
	struct channel *channel;
	uint32_t pr_value;
	uint16_t pr_policy;
	uint16_t o_stream;
	uint8_t unordered;
	uint16_t label_len;
	uint16_t protocol_len;
    
	o_stream = i_stream; // The signalling we assumes symmetric stream id_s
    
	bool is_odd = (bool)(i_stream & (uint16_t)1);
	if(!is_odd && pc->open_even_sid){
		error("dce: We expect the remote side to open odd stream id's \n");
	}
	if(is_odd && !pc->open_even_sid){
		error("dce: We expect the remote side to open even stream id's \n");
	}
    
	if ((channel = find_channel_by_i_stream(pc, i_stream))) {
		warning("handle_open_request_message:"
		      " channel %d is in state %d instead of CLOSED.\n",
		       channel->id, channel->state);
		/* XXX: some error handling */
		return;
	}
	if(!valid_o_stream(pc, o_stream)){
		warning("handle_open_request_message:"
		      " o_stream = %d not valid .\n", o_stream);
		/* XXX: some error handling */
		return;
	}
	if ((channel = find_channel_by_o_stream(pc, o_stream))) {
		warning("handle_open_request_message:"
		      " channel %d is in state %d instead of CLOSED.\n",
			channel->id, channel->state);
		/* XXX: some error handling */
		return;
	}
	if ((channel = find_free_channel(pc)) == NULL) {
		/* XXX: some error handling */
		return;
	}
    
    switch (req->fixed.channel_type) {
	case DCOMCT_ORDERED_RELIABLE:
		pr_policy = SCTP_PR_SCTP_NONE;
		unordered = 0;
		break;
	/* XXX Doesn't make sense */
	case DCOMCT_UNORDERED_RELIABLE:
		pr_policy = SCTP_PR_SCTP_NONE;
		unordered = 1;
		break;
	/* XXX Doesn't make sense */
	case DCOMCT_ORDERED_PARTIAL_RTXS:
		pr_policy = SCTP_PR_SCTP_RTX;
		unordered = 0;
		break;
	case DCOMCT_UNORDERED_PARTIAL_RTXS:
		pr_policy = SCTP_PR_SCTP_RTX;
		unordered = 1;
		break;
	case DCOMCT_ORDERED_PARTIAL_TIME:
		pr_policy = SCTP_PR_SCTP_TTL;
		unordered = 0;
		break;
	case DCOMCT_UNORDERED_PARTIAL_TIME:
		pr_policy = SCTP_PR_SCTP_TTL;
		unordered = 1;
		break;
	default:
		pr_policy = SCTP_PR_SCTP_NONE;
		unordered = 0;
		/* XXX error handling */
		break;
	}
	pr_value = ntohl(req->fixed.reliability_params);
	label_len = ntohs(req->fixed.label_length);
	protocol_len = ntohs(req->fixed.protocol_length);
	channel->state = DATA_CHANNEL_OPEN;
	channel->unordered = unordered;
	channel->pr_policy = pr_policy;
	channel->pr_value = pr_value;
	channel->i_stream = i_stream;
	channel->o_stream = o_stream;
	channel->flags = 0;
	uint8_t *ptr = req->label_and_protocol;
	if(label_len > 0){
		if((size_t)label_len > sizeof(channel->label)){
			warning("dce: label longer than %d bytes \n",
			      sizeof(channel->label));
		} else {
			memcpy(channel->label, ptr, label_len);
			ptr += label_len;
		}
	}
	if(protocol_len > 0){
		if((size_t)protocol_len > sizeof(channel->protocol)){
			warning("dce: protocol longer than %d bytes \n",
				sizeof(channel->protocol));
		} else {
			memcpy(channel->protocol, ptr, protocol_len);
		}
	}
	pc->i_stream_channel[i_stream] = channel;
	if (o_stream == 0) {
		request_more_o_streams(pc);
	} else {
		if (send_open_ack_message(pc->sock, o_stream)) {
			pc->o_stream_channel[o_stream] = channel;
		} else {
			if (errno == EAGAIN) {
				channel->flags |= DATA_CHANNEL_FLAGS_SEND_ACK;
				pc->o_stream_channel[o_stream] = channel;
			} else {
				/* XXX: Signal error to the other end. */
				pc->i_stream_channel[i_stream] = NULL;
				channel->state = DATA_CHANNEL_CLOSED;
				channel->unordered = 0;
				channel->pr_policy = 0;
				channel->pr_value = 0;
				channel->i_stream = 0;
				channel->o_stream = 0;
				channel->flags = 0;
			}
		}
	}
	if(channel->state == DATA_CHANNEL_OPEN){
		struct dce_channel *ch = NULL;
		ch = get_dce_channel(dce, channel->label);

		if(ch){
			struct payload *pld;

			ch->id = channel->id;

			pld = payload_new(dce, ch, CH_OPEN);
			if (!pld)
				return;

			pld->v.chopen.sid = channel->o_stream;

			mqueue_push(g_dce.mqueue, pld->type, pld);
		}
	}
}


static void
handle_open_ack_message(struct dce *dce,
                        struct rtcweb_datachannel_ack *ack,
                        size_t length, uint16_t i_stream)
{
	struct peer_connection *pc = &dce->pc;
	struct channel *channel;

	channel = find_channel_by_i_stream(pc, i_stream);
	if (channel == NULL) {
		/* XXX: some error handling */
		return;
	}
	if (channel->state == DATA_CHANNEL_OPEN) {
		return;
	}
	if (channel->state != DATA_CHANNEL_CONNECTING) {
		/* XXX: error handling */
		return;
	}
	channel->state = DATA_CHANNEL_OPEN;
	struct dce_channel *ch = NULL;
	ch = get_dce_channel(dce, channel->label);
	if(ch) {
		struct payload *pld;

		ch->id = channel->id;

		pld = payload_new(dce, ch, CH_OPEN);
		if (!pld)
			return;

		pld->v.chopen.sid = channel->i_stream;

		mqueue_push(g_dce.mqueue, pld->type, pld);
	}
	return;
}

static void
handle_unknown_message(char *msg, size_t length, uint16_t i_stream)
{
	/* XXX: Send an error message */
	return;
}

static void
handle_data_message(struct dce *dce,
                    char *buffer, size_t length, uint16_t i_stream)
{
	struct peer_connection *pc = &dce->pc;
	struct channel *channel;

	channel = find_channel_by_i_stream(pc, i_stream);
	if (channel == NULL) {
		/* XXX: Some error handling */
		return;
	}
	if (channel->state == DATA_CHANNEL_CONNECTING) {
		/* Implicit ACK */
		channel->state = DATA_CHANNEL_OPEN;
	}
	if (channel->state != DATA_CHANNEL_OPEN) {
		/* XXX: What about other states? */
		/* XXX: Some error handling */
		return;
	} else {
		debug("dce: message received of length %zu on chan: %d\n",
		      length, channel->id);

		unlock_peer_connection(&dce->pc);		

		struct dce_channel *ch = NULL;
		ch = get_dce_channel(dce, channel->label);

		if (ch) {
			struct payload *pld;

			pld = payload_new(dce, ch, CH_DATA);
			if (!pld)
				return;

			pld->v.chdata.buf = mem_alloc(length, NULL);
			memcpy(pld->v.chdata.buf, buffer, length);
			pld->v.chdata.len = length;

			mqueue_push(g_dce.mqueue, pld->type, pld);
		}

		lock_peer_connection(&dce->pc);		

		/* Assuming DATA_CHANNEL_PPID_DOMSTRING */
		/* XXX: Protect for non 0 terminated buffer */
	}
	return;
}

static void
handle_message(struct dce *dce,
	       char *buffer, size_t length, uint32_t ppid, uint16_t i_stream)
{
	struct rtcweb_datachannel_open_request *req;
	struct rtcweb_datachannel_ack *ack, *msg;

	switch (ppid) {
	case DATA_CHANNEL_PPID_CONTROL:
		if (length < sizeof(struct rtcweb_datachannel_ack)) {
			return;
		}
		msg = (struct rtcweb_datachannel_ack *)buffer;
		switch (msg->msg_type) {
		case DATA_CHANNEL_OPEN_REQUEST:
			if (length < sizeof(struct rtcweb_datachannel_open_request_fixed)) {
				/* XXX: error handling? */
				return;
			}
			req = (struct rtcweb_datachannel_open_request *)buffer;
			handle_open_request_message(dce, req, length, i_stream);
			break;
		case DATA_CHANNEL_OPEN_ACK:
			if (length < sizeof(struct rtcweb_datachannel_ack)) {
				/* XXX: error handling? */
				return;
			}
			ack = (struct rtcweb_datachannel_ack *)buffer;
			handle_open_ack_message(dce, ack, length, i_stream);
			break;
		default:
			warning("dce: recieved unknown control message "
			      "with msg_type = %d \n", msg->msg_type);
			handle_unknown_message(buffer, length, i_stream);
			break;
		}
		break;

	case DATA_CHANNEL_PPID_DOMSTRING:
	case DATA_CHANNEL_PPID_BINARY:
		handle_data_message(dce, buffer, length, i_stream);
		break;

	default:
		debug("dce: msg len=%zu, PPID %u on stream %u received.\n",
		       length, ppid, i_stream);
		break;
	}
}


static void
handle_association_change_event(struct dce *dce,
				struct sctp_assoc_change *sac)
{
	unsigned int i, n;

	switch (sac->sac_state) {
	case SCTP_COMM_UP:
		info("dce: association change: SCTP_COMM_UP\n");
		unlock_peer_connection(&dce->pc);
            
		if (dce) {            
			struct le *le;
			struct payload *pld;

			pld = payload_new(dce, NULL, ESTAB);
			if (!pld)
				return;

			mqueue_push(g_dce.mqueue, pld->type, pld);

			LIST_FOREACH(&dce->channell, le) {
				struct dce_channel *ch = le->data;
                
				pld = payload_new(dce, ch, CH_ESTAB);
				if (!pld)
					return;

				mqueue_push(g_dce.mqueue, pld->type, pld);
			}
		}
		lock_peer_connection(&dce->pc);		
		break;
		
	case SCTP_COMM_LOST:
		info("dce: association change: SCTP_COMM_LOST\n");
		break;
		
	case SCTP_RESTART:
		info("dce: association change: SCTP_RESTART\n");
		break;
		
	case SCTP_SHUTDOWN_COMP:
		info("dce: association change: SCTP_SHUTDOWN_COMP\n");
		break;
		
	case SCTP_CANT_STR_ASSOC:
		info("dce: association change: SCTP_CANT_STR_ASSOC\n");
		break;
		
	default:
		info("dce: association change: UNKNOWN\n");
		break;
	}

	info("dce: association change: streams (in/out) = (%u/%u)\n",
	     sac->sac_inbound_streams, sac->sac_outbound_streams);
	
	n = sac->sac_length - sizeof(struct sctp_assoc_change);
	if (((sac->sac_state == SCTP_COMM_UP) ||
	     (sac->sac_state == SCTP_RESTART)) && (n > 0)) {
		debug(", supports");
		for (i = 0; i < n; i++) {
			switch (sac->sac_info[i]) {
			case SCTP_ASSOC_SUPPORTS_PR:
				debug(" PR");
				break;
			case SCTP_ASSOC_SUPPORTS_AUTH:
				debug(" AUTH");
				break;
			case SCTP_ASSOC_SUPPORTS_ASCONF:
				debug(" ASCONF");
				break;
			case SCTP_ASSOC_SUPPORTS_MULTIBUF:
				debug(" MULTIBUF");
				break;
			case SCTP_ASSOC_SUPPORTS_RE_CONFIG:
				debug(" RE-CONFIG");
				break;
			default:
				debug(" UNKNOWN(0x%02x)", sac->sac_info[i]);
				break;
			}
		}
	} else if (((sac->sac_state == SCTP_COMM_LOST) ||
	            (sac->sac_state == SCTP_CANT_STR_ASSOC)) && (n > 0)) {
		debug(", ABORT =");
		for (i = 0; i < n; i++) {
			debug(" 0x%02x", sac->sac_info[i]);
		}
	}
	debug(".\n");
	if ((sac->sac_state == SCTP_CANT_STR_ASSOC) ||
	    (sac->sac_state == SCTP_SHUTDOWN_COMP) ||
	    (sac->sac_state == SCTP_COMM_LOST)) {
		warning("dce: association change: lost\n");
	}
	return;
}


static void
handle_peer_address_change_event(struct sctp_paddr_change *spc)
{
	char addr_buf[INET6_ADDRSTRLEN];
	const char *addr;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;

	switch (spc->spc_aaddr.ss_family) {
	case AF_INET:
		sin = (struct sockaddr_in *)&spc->spc_aaddr;
		addr = inet_ntop(AF_INET, &sin->sin_addr,
				 addr_buf, INET_ADDRSTRLEN);
		break;
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)&spc->spc_aaddr;
		addr = inet_ntop(AF_INET6, &sin6->sin6_addr,
				 addr_buf, INET6_ADDRSTRLEN);
		break;
	default:
		snprintf(addr_buf, INET6_ADDRSTRLEN,
			 "Unknown family %d", spc->spc_aaddr.ss_family);
		addr = addr_buf;
		break;
	}
	debug("Peer address %s is now ", addr);
	switch (spc->spc_state) {
	case SCTP_ADDR_AVAILABLE:
		debug("SCTP_ADDR_AVAILABLE");
		break;
	case SCTP_ADDR_UNREACHABLE:
		debug("SCTP_ADDR_UNREACHABLE");
		break;
	case SCTP_ADDR_REMOVED:
		debug("SCTP_ADDR_REMOVED");
		break;
	case SCTP_ADDR_ADDED:
		debug("SCTP_ADDR_ADDED");
		break;
	case SCTP_ADDR_MADE_PRIM:
		debug("SCTP_ADDR_MADE_PRIM");
		break;
	case SCTP_ADDR_CONFIRMED:
		debug("SCTP_ADDR_CONFIRMED");
		break;
	default:
		debug("UNKNOWN");
		break;
	}
	debug(" (error = 0x%08x).\n", spc->spc_error);
	return;
}

static void
handle_adaptation_indication(struct sctp_adaptation_event *sai)
{
	debug("Adaptation indication: %x.\n", sai-> sai_adaptation_ind);
	return;
}

static void
handle_shutdown_event(struct sctp_shutdown_event *sse)
{
	debug("Shutdown event.\n");
	/* XXX: notify all channels. */
	return;
}


static void
handle_stream_reset_event(struct dce *dce, struct sctp_stream_reset_event *strrst)
{
	uint32_t n, i;
	struct channel *channel = NULL;
	struct peer_connection *pc = &dce->pc;
    
	n = (strrst->strreset_length - sizeof(struct sctp_stream_reset_event)) / sizeof(uint16_t);
	debug("Stream reset event: flags = %x, ", strrst->strreset_flags);
	if (strrst->strreset_flags & SCTP_STREAM_RESET_INCOMING_SSN) {
		if (strrst->strreset_flags & SCTP_STREAM_RESET_OUTGOING_SSN) {
			debug("incoming/");
		}
		debug("incoming ");
	}
	if (strrst->strreset_flags & SCTP_STREAM_RESET_OUTGOING_SSN) {
		debug("outgoing ");
	}
	debug("stream ids = ");
	for (i = 0; i < n; i++) {
		if (i > 0) {
			debug(", ");
		}
		debug("%d", strrst->strreset_stream_list[i]);
	}
	debug(".\n");
	if (!(strrst->strreset_flags & SCTP_STREAM_RESET_DENIED) &&
	    !(strrst->strreset_flags & SCTP_STREAM_RESET_FAILED)) {
		for (i = 0; i < n; i++) {
			if (strrst->strreset_flags & SCTP_STREAM_RESET_INCOMING_SSN) {
				channel = find_channel_by_i_stream(pc, strrst->strreset_stream_list[i]);
				if (channel != NULL) {
					pc->i_stream_channel[channel->i_stream] = NULL;
					channel->i_stream = 0;
					if (channel->o_stream == 0) {
						channel->pr_policy = SCTP_PR_SCTP_NONE;
						channel->pr_value = 0;
						channel->unordered = 0;
						channel->flags = 0;
						channel->state = DATA_CHANNEL_CLOSED;
					} else {
						if (channel->state == DATA_CHANNEL_OPEN) {
							reset_outgoing_stream(pc, channel->o_stream);
							channel->state = DATA_CHANNEL_CLOSING;
						} else {
							/* XXX: What to do? */
						}
					}
				}
			}
			if (strrst->strreset_flags & SCTP_STREAM_RESET_OUTGOING_SSN) {
				channel = find_channel_by_o_stream(pc, strrst->strreset_stream_list[i]);
				if (channel != NULL) {
					pc->o_stream_channel[channel->o_stream] = NULL;
					channel->o_stream = 0;
					if (channel->i_stream == 0) {
						channel->pr_policy = SCTP_PR_SCTP_NONE;
						channel->pr_value = 0;
						channel->unordered = 0;
						channel->flags = 0;
						channel->state = DATA_CHANNEL_CLOSED;
					}
				}
			}
		}

		if (channel && channel->state == DATA_CHANNEL_CLOSED) {
			struct dce_channel *ch = NULL;
			ch = get_dce_channel(dce, channel->label);

			if (ch) {
				struct payload *pld;

				pld = payload_new(dce, ch, CH_CLOSE);
				if (!pld)
					return;

				mqueue_push(g_dce.mqueue, pld->type, pld);
			}
		}
	}
}


static void
handle_stream_change_event(struct peer_connection *pc, struct sctp_stream_change_event *strchg)
{
	uint16_t o_stream;
	uint32_t i;
	struct channel *channel;

	debug("Stream change event: streams (in/out) = (%u/%u), flags = %x.\n",
	       strchg->strchange_instrms, strchg->strchange_outstrms, strchg->strchange_flags);
	for (i = 0; i < NUMBER_OF_CHANNELS; i++) {
		channel = &(pc->channels[i]);
		if ((channel->state == DATA_CHANNEL_CONNECTING) &&
		    (channel->o_stream == 0)) {
			if ((strchg->strchange_flags & SCTP_STREAM_CHANGE_DENIED) ||
			    (strchg->strchange_flags & SCTP_STREAM_CHANGE_FAILED)) {
				/* XXX: Signal to the other end. */
				if (channel->i_stream != 0) {
					pc->i_stream_channel[channel->i_stream] = NULL;
				}
				channel->unordered = 0;
				channel->pr_policy = SCTP_PR_SCTP_NONE;
				channel->pr_value = 0;
				channel->i_stream = 0;
				channel->o_stream = 0;
				channel->flags = 0;
				channel->state = DATA_CHANNEL_CLOSED;
			} else {
				o_stream = find_free_o_stream(pc);
				if (o_stream != 0) {
					channel->o_stream = o_stream;
					pc->o_stream_channel[o_stream] = channel;
					if (channel->i_stream == 0) {
						channel->flags |= DATA_CHANNEL_FLAGS_SEND_REQ;
					}
				} else {
					/* We will not find more ... */
					break;
				}
			}
		}
	}
	return;
}

static void
handle_remote_error_event(struct sctp_remote_error *sre)
{
	size_t i, n;

	n = sre->sre_length - sizeof(struct sctp_remote_error);
	debug("Remote Error (error = 0x%04x): ", sre->sre_error);
	for (i = 0; i < n; i++) {
		debug(" 0x%02x", sre-> sre_data[i]);
	}
	debug(".\n");
	return;
}

static void
handle_send_failed_event(struct sctp_send_failed_event *ssfe)
{
	size_t i, n;

	if (ssfe->ssfe_flags & SCTP_DATA_UNSENT) {
		debug("Unsent ");
	}
	if (ssfe->ssfe_flags & SCTP_DATA_SENT) {
		debug("Sent ");
	}
	if (ssfe->ssfe_flags & ~(SCTP_DATA_SENT | SCTP_DATA_UNSENT)) {
		debug("(flags = %x) ", ssfe->ssfe_flags);
	}
	warning("message with PPID = %d, SID = %d, flags: 0x%04x due to error = 0x%08x",
	       ntohl(ssfe->ssfe_info.snd_ppid), ssfe->ssfe_info.snd_sid,
	       ssfe->ssfe_info.snd_flags, ssfe->ssfe_error);
	n = ssfe->ssfe_length - sizeof(struct sctp_send_failed_event);
	for (i = 0; i < n; i++) {
		debug(" 0x%02x", ssfe->ssfe_data[i]);
	}
	debug(".\n");
	return;
}

static void
handle_notification(struct dce *dce,
		    union sctp_notification *notif, size_t n)
{
	struct peer_connection *pc = &dce->pc;
	
	if (notif->sn_header.sn_length != (uint32_t)n) {
		return;
	}
	switch (notif->sn_header.sn_type) {
	case SCTP_ASSOC_CHANGE:
		handle_association_change_event(dce,
						&(notif->sn_assoc_change));
		break;
	case SCTP_PEER_ADDR_CHANGE:
		handle_peer_address_change_event(&(notif->sn_paddr_change));
		break;
	case SCTP_REMOTE_ERROR:
		handle_remote_error_event(&(notif->sn_remote_error));
		break;
	case SCTP_SHUTDOWN_EVENT:
		handle_shutdown_event(&(notif->sn_shutdown_event));
		break;
	case SCTP_ADAPTATION_INDICATION:
		handle_adaptation_indication(&(notif->sn_adaptation_event));
		break;
	case SCTP_PARTIAL_DELIVERY_EVENT:
		break;
	case SCTP_AUTHENTICATION_EVENT:
		break;
	case SCTP_SENDER_DRY_EVENT:
		dce->snd_dry_event = true;
		break;
	case SCTP_NOTIFICATIONS_STOPPED_EVENT:
		break;
	case SCTP_SEND_FAILED_EVENT:
		handle_send_failed_event(&(notif->sn_send_failed_event));
		break;
	case SCTP_STREAM_RESET_EVENT:
		handle_stream_reset_event(dce, &(notif->sn_strreset_event));
		send_deferred_messages(pc);
		send_outgoing_stream_reset(pc);
		request_more_o_streams(pc);
		break;
	case SCTP_ASSOC_RESET_EVENT:
		break;
	case SCTP_STREAM_CHANGE_EVENT:
		handle_stream_change_event(pc, &(notif->sn_strchange_event));
		send_deferred_messages(pc);
		send_outgoing_stream_reset(pc);
		request_more_o_streams(pc);
		break;
	default:
		break;
	}
}


static int
print_status(const struct peer_connection *pc, struct re_printf *pf)
{
	struct sctp_status status;
	socklen_t len;
	uint32_t i;
	const struct channel *channel;
	int err = 0;

	if (!pc)
		return 0;

	len = (socklen_t)sizeof(struct sctp_status);

	if (usrsctp_getsockopt(pc->sock, IPPROTO_SCTP, SCTP_STATUS, &status, &len) < 0) {
		return re_hprintf(pf, "getsockopt error\n");
	}
	err = re_hprintf(pf,"Association state: ");
	switch (status.sstat_state) {
	case SCTP_CLOSED:
		err = re_hprintf(pf,"CLOSED\n");
		break;
	case SCTP_BOUND:
		err |= re_hprintf(pf,"BOUND\n");
		break;
	case SCTP_LISTEN:
		err |= re_hprintf(pf,"LISTEN\n");
		break;
	case SCTP_COOKIE_WAIT:
		err |= re_hprintf(pf,"COOKIE_WAIT\n");
		break;
	case SCTP_COOKIE_ECHOED:
		err |= re_hprintf(pf,"COOKIE_ECHOED\n");
		break;
	case SCTP_ESTABLISHED:
		err |= re_hprintf(pf,"ESTABLISHED\n");
		break;
	case SCTP_SHUTDOWN_PENDING:
		err |= re_hprintf(pf,"SHUTDOWN_PENDING\n");
		break;
	case SCTP_SHUTDOWN_SENT:
		err |= re_hprintf(pf,"SHUTDOWN_SENT\n");
		break;
	case SCTP_SHUTDOWN_RECEIVED:
		err |= re_hprintf(pf,"SHUTDOWN_RECEIVED\n");
		break;
	case SCTP_SHUTDOWN_ACK_SENT:
		err |= re_hprintf(pf,"SHUTDOWN_ACK_SENT\n");
		break;
	default:
		err |= re_hprintf(pf,"UNKNOWN\n");
		break;
	}
	err |= re_hprintf(pf,"        Number of streams (i/o) = (%u/%u)\n",
	       status.sstat_instrms, status.sstat_outstrms);
	for (i = 0; i < NUMBER_OF_CHANNELS; i++) {
		channel = &(pc->channels[i]);
		if (channel->state == DATA_CHANNEL_CLOSED) {
			continue;
		}
		err |= re_hprintf(pf,"        Channel with id = %u: state ", channel->id);
		switch (channel->state) {
		case DATA_CHANNEL_CLOSED:
			err |= re_hprintf(pf,"CLOSED");
			break;
		case DATA_CHANNEL_CONNECTING:
			err |= re_hprintf(pf,"CONNECTING");
			break;
		case DATA_CHANNEL_OPEN:
			err |= re_hprintf(pf,"OPEN");
			break;
		case DATA_CHANNEL_CLOSING:
			err |= re_hprintf(pf,"CLOSING");
			break;
		default:
			err |= re_hprintf(pf,"UNKNOWN(%d)", channel->state);
			break;
		}
		err |= re_hprintf(pf,", flags = 0x%08x, stream id (in/out): (%u/%u), ",
		       channel->flags,
		       channel->i_stream,
		       channel->o_stream);
		if (channel->unordered) {
			err |= re_hprintf(pf,"unordered, ");
		} else {
			err |= re_hprintf(pf,"ordered, ");
		}
		switch (channel->pr_policy) {
		case SCTP_PR_SCTP_NONE:
			err |= re_hprintf(pf,"reliable.\n");
			break;
		case SCTP_PR_SCTP_TTL:
			err |= re_hprintf(pf,"unreliable (timeout %ums).\n", channel->pr_value);
			break;
		case SCTP_PR_SCTP_RTX:
			err |= re_hprintf(pf,"unreliable (max. %u rtx).\n", channel->pr_value);
			break;
		default:
			err |= re_hprintf(pf,"unkown policy %u.\n", channel->pr_policy);
			break;
		}
		err |= re_hprintf(pf,"        label:    %s \n", channel->label);
		err |= re_hprintf(pf,"        protocol: %s \n", channel->protocol);
	}
    return err;
}


static int
receive_cb(struct socket *sock, union sctp_sockstore addr, void *data,
           size_t datalen, struct sctp_rcvinfo rcv, int flags, void *ulp_info)
{
	struct dce *dce = ulp_info;
	int err = 0;

	if (!dce) {
		warning("dce: receive_cb: dce == NULL\n");
		return 1;
	}

	debug("sock=%p dce=%p dce->pc=%p\n", sock, dce, &dce->pc);

	lock_write_get(g_dce.lock);
	if (!exist_dce(&g_dce.dcel, dce)) {
		warning("dce: receive_cb: dce(%p) not active\n", dce);
		err = ENOSYS;
	}
	else {
		assert(DCE_MAGIC == dce->magic);
		/* Make sure we have a ref to the dce */
		mem_ref(dce);
	}
	lock_rel(g_dce.lock);

	if (err)
		return 1;
	
	if (data) {
		lock_peer_connection(&dce->pc);
		if (flags & MSG_NOTIFICATION) {
			handle_notification(dce,
				  (union sctp_notification *)data, datalen);
		} else {
			handle_message(dce,
				       data, datalen,
				       ntohl(rcv.rcv_ppid), rcv.rcv_sid);
		}
		unlock_peer_connection(&dce->pc);

		free(data);
	}
	else {
		usrsctp_deregister_address(dce);
		if (dce)
			dce->sock = NULL;
		usrsctp_close(sock);
	}

	mem_deref(dce);

	return 1;
}


int dce_status(struct re_printf *pf, struct dce *dce)
{
	if (!dce)
		return EINVAL;
	
	if (DCE_MAGIC != dce->magic) {
		return re_hprintf(pf, "invalid dce magic\n");
	}
    
	lock_peer_connection(&dce->pc);
	int err = print_status(&dce->pc, pf);
	unlock_peer_connection(&dce->pc);

	return err;
}


int dce_connect(struct dce *dce, bool dtls_role_active)
{
	struct sockaddr_conn sconn;
	int sctp_err;
	int err = 0;
	int port = DATA_CHANNEL_PORT;
    
	if (!dce)
		return EINVAL;

	assert(DCE_MAGIC == dce->magic);

	info("dce: Connecting on dce=%p to port=%d\n", dce, port);

	memset(&sconn, 0, sizeof(struct sockaddr_conn));
	sconn.sconn_family = AF_CONN;
#ifdef HAVE_SCONN_LEN
	sconn.sconn_len = sizeof(struct sockaddr_conn);
#endif
	sconn.sconn_port = htons(port);
	sconn.sconn_addr = dce;
	
	sctp_err = usrsctp_connect(dce->sock, (struct sockaddr *)&sconn,
				   sizeof(sconn));
	if (sctp_err < 0) {
		err = errno;
		if (err == EINPROGRESS)
			err = 0;
		else {
			warning("dce: alloc: connect failed sctp=%d EINP=%d errno=%d\n", sctp_err, EINPROGRESS, errno);
			goto out;
		}
	}

    /* Use even or odd stream ids according to https://tools.ietf.org/html/draft-ietf-rtcweb-data-protocol-09 */
    if(dtls_role_active){
        dce->pc.open_even_sid = true;
    }else {
        dce->pc.open_even_sid = false;
    }
    
 out:
	return err;
}


int dce_open_chan(struct dce *dce, struct dce_channel *ch)
{
	struct channel *channel;
	int err = 0;

	if (!dce || !ch)
		return EINVAL;

	assert(DCE_MAGIC == dce->magic);
    
	lock_peer_connection(&dce->pc);
	channel = open_channel(&dce->pc, 0, SCTP_PR_SCTP_NONE, 0, ch->label, ch->protocol);
	unlock_peer_connection(&dce->pc);
	if (channel == NULL) {
		warning("dce: open: creating channel failed.\n");
		err = ENOSYS;
		goto out;
	}
	else {
        ch->id = channel->o_stream;
		info("dce: channel id %u created.\n", ch->id);
	}
 out:
	return err;
}

int
dce_close_chan(struct dce *dce, struct dce_channel *ch)
{
	if (!dce || !ch)
		return EINVAL;

	assert(DCE_MAGIC == dce->magic);
    
	if (ch->id >= NUMBER_OF_CHANNELS || ch->id < 0)
		return ERANGE;
    
	lock_peer_connection(&dce->pc);
	close_channel(&dce->pc, &dce->pc.channels[ch->id]);
	unlock_peer_connection(&dce->pc);

	return 0;
}


void dce_recv_pkt(struct dce *dce, const uint8_t *pkt, size_t len)
{
	debug("dce: recv_pkt: dce=%p len=%u\n", dce, (uint32_t)len);
	
	if (!dce)
		return;

	assert(DCE_MAGIC == dce->magic);
    
	{
		struct sctp_header hdr;
		struct mbuf mb;
		int err;

		mb.buf = (void *)pkt;
		mb.pos = 0;
		mb.end = mb.size = len;

		err = sctp_header_decode(&hdr, &mb);
		if (err) {
			warning("dce: recv: SCTP decode error (%m)\n", err);
		}
		else {
			assert(hdr.sport == DATA_CHANNEL_PORT);
			assert(hdr.dport == DATA_CHANNEL_PORT);
		}
	}

	usrsctp_conninput(dce, pkt, len, 0);
}


int dce_send(struct dce *dce, struct dce_channel *ch, const void *data, size_t len)
{
	if (!dce || !ch)
		return EINVAL;

	assert(DCE_MAGIC == dce->magic);
    
	if (ch->id >= NUMBER_OF_CHANNELS || ch->id < 0) {
		warning("dce: send: invalid channel %d\n", ch->id);
		return ERANGE;
	}

	lock_peer_connection(&dce->pc);
	dce->snd_dry_event = false;
	int ret = send_user_message(&dce->pc,
			  &dce->pc.channels[ch->id],
			  data, len);
	unlock_peer_connection(&dce->pc);

	return ret;
}

bool dce_snd_dry(struct dce *dce)
{
	return dce->snd_dry_event;
}

static
void
debug_printf(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vlog(LOG_LEVEL_INFO, format, ap);
	va_end(ap);
}


static void dce_destructor(void *arg)
{
	struct dce *dce = arg;

	lock_write_get(g_dce.lock);
	list_unlink(&dce->le);
	lock_rel(g_dce.lock);

	assert(DCE_MAGIC == dce->magic);
    
	info("dce: destructor: %p\n", dce);

	dce->sendh = NULL;
	dce->estabh = NULL;
    
#if 0
	if (dce->sock) {
		re_printf("shutdown\n");
		usrsctp_shutdown(dce->sock, 0);
	}
#endif

	usrsctp_deregister_address(dce);
	if (dce->sock) {
		struct socket *sock = dce->sock;
		dce->sock = NULL;
		usrsctp_set_ulpinfo(sock, NULL);
		usrsctp_close(sock);
	}

	list_flush(&dce->channell);
	close_peer_connection(&dce->pc);
}


static int usrsctp_send_handler(void *addr, void *buf, size_t len,
				uint8_t tos, uint8_t set_df)
{
	struct dce *dce = addr;
	struct sctp_header hdr;
	struct mbuf mb;
	int err;
    
	if (!dce)
		return EINVAL;

	lock_write_get(g_dce.lock);
	if (!exist_dce(&g_dce.dcel, dce)) {
		debug("dce: send: dce(%p) not active\n", dce);
		err = ENOSYS;
		goto out;
	}

	assert(DCE_MAGIC == dce->magic);
    
	mb.buf = buf;
	mb.pos = 0;
	mb.end = mb.size = len;

	err = sctp_header_decode(&hdr, &mb);
	if (err) {
		warning("dce: send: SCTP decode error (%m)\n", err);
		goto out;
	}
	else {
		assert(hdr.sport == DATA_CHANNEL_PORT);
		assert(hdr.dport == DATA_CHANNEL_PORT);
	}

	if (dce->sendh) {
		err = dce->sendh((uint8_t *)buf, len, dce->arg);
		if (err) {
			warning("dce: sendh error (%m)\n", err);
			goto out;
		}
	}

 out:
	lock_rel(g_dce.lock);

	return err ? 1 : 0;
}


static void dce_mqueue_handler(int id, void *data, void *arg)
{
	struct payload *pld = data;
	struct dce_channel *ch;
	bool valid;
	(void)arg;

	info("dce: mqueue_handler:  id=%d <dce=%p>\n", id, pld->dce);

	if (PAYLOAD_MAGIC != pld->magic) {
		warning("dce: invalid payload magic\n");
		return;
	}

	lock_write_get(g_dce.lock);
	valid = exist_dce(&g_dce.dcel, pld->dce);
	lock_rel(g_dce.lock);

	if (!valid) {
		warning("dce: mqueue_recv: dce(%p) not valid\n", pld->dce);
		goto out;
	}

	ch = pld->ch;

	switch (id) {

	case ESTAB:
		if (pld->dce->estabh)
			pld->dce->estabh(pld->dce->arg);
		break;

	case CH_ESTAB:
		if (ch->estabh)
			ch->estabh(ch->arg);
		break;

	case CH_OPEN:
		if (ch->openh) {
			ch->openh(pld->v.chopen.sid, ch->label,
				  ch->protocol, ch->arg);
		}
		break;

	case CH_CLOSE:
		if (ch && ch->closeh) {
			ch->closeh(ch->id, ch->label,
				   ch->protocol, ch->arg);
		}

		break;

	case CH_DATA:
		if (ch->datah) {
			ch->datah(ch->id,
				  pld->v.chdata.buf, pld->v.chdata.len,
				  ch->arg);
		}
		break;

	default:
		warning("dce: mqueue: ignored event %d\n", id);
		break;
	}

 out:
	mem_deref(pld);
}


int dce_init(void)
{
	int err;

	debug("dce_init: inited=%d\n", dce_inited);
	
	if (dce_inited)
		return 0;

	memset(&g_dce, 0, sizeof(g_dce));

	list_init(&g_dce.dcel);

	err = lock_alloc(&g_dce.lock);
	if (err)
		return err;

	err = mqueue_alloc(&g_dce.mqueue, dce_mqueue_handler, NULL);
	if (err)
		return err;

	usrsctp_init(0, usrsctp_send_handler, debug_printf);
    
	dce_inited = true;

	return 0;
}


void dce_close(void)
{
	int tries = 60;
	
	debug("dce_close: inited=%d\n", dce_inited);
	
	if (!dce_inited)
		return;

	while (usrsctp_finish() != 0 && tries--) {
		re_printf("dce: close: usrsctp_finish failed (%m)\n", errno);
		usleep(500000);
	}

	mem_deref(g_dce.lock);

	g_dce.mqueue = mem_deref(g_dce.mqueue);

	if (!list_isempty(&g_dce.pendingl)) {
		debug("dce: flush pending events: %u\n",
			  list_count(&g_dce.pendingl));
	}
	list_flush(&g_dce.pendingl);

	dce_inited = false;	
}


int dce_alloc(struct dce **dcep,
	      dce_send_h *sendh,
	      dce_estab_h *estabh,
	      void *arg)
{
	struct dce *dce;
	struct sockaddr_conn sconn;	
	struct sctp_initmsg initmsg;
	struct sctp_assoc_value av;
	struct sctp_event event;	
	static uint16_t event_types[] = {
		SCTP_ASSOC_CHANGE,
		SCTP_PEER_ADDR_CHANGE,
		SCTP_REMOTE_ERROR,
		SCTP_SHUTDOWN_EVENT,
		SCTP_ADAPTATION_INDICATION,
		SCTP_SEND_FAILED_EVENT,
		SCTP_STREAM_RESET_EVENT,
		SCTP_STREAM_CHANGE_EVENT,
		SCTP_SENDER_DRY_EVENT
	};
	int err = 0;
	int sctp_err;
	const int on = 1;
	unsigned long i;
	int port = DATA_CHANNEL_PORT;

	if (!dce_inited)
		return EINVAL;

	if (!dcep)
		return EINVAL;

	dce = mem_zalloc(sizeof(*dce), dce_destructor);
	if (!dce)
		return ENOMEM;

	dce->sendh = sendh;
	dce->estabh = estabh;
	dce->arg = arg;

#ifdef SCTP_DEBUG
	usrsctp_sysctl_set_sctp_debug_on(SCTP_DEBUG_ALL);
#endif
	usrsctp_sysctl_set_sctp_blackhole(2);

	usrsctp_register_address(dce);

	dce->sock = usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP,
				   receive_cb, NULL, 0, dce);
	
	if (dce->sock == NULL) {
		warning("dce: alloc: failed to create socket\n");
		err = ENOTSOCK;
		goto out;
	}
	
	sctp_err = usrsctp_set_non_blocking(dce->sock, 1);
	if (sctp_err < 0) {
		err = -sctp_err;
		warning("dce: alloc: set_non_blocking failed: %m\n", err);
		goto out;
	}
	
	err = init_peer_connection(&dce->pc);
	if (err)
		goto out;
    
	struct linger linger_opt; // This makes sctp stop immediately after usrsctp_close
	linger_opt.l_onoff = 1;
	linger_opt.l_linger = 0;
	sctp_err = usrsctp_setsockopt(dce->sock, SOL_SOCKET, SO_LINGER, &linger_opt,
                                  sizeof(linger_opt));
	if (sctp_err < 0) {
		warning("dce: alloc: Failed to set SO_LINGER.");
		goto out;
	}
    
	sctp_err = usrsctp_setsockopt(dce->sock, IPPROTO_SCTP,
				      SCTP_RECVRCVINFO, &on, sizeof(int));
	if (sctp_err < 0) {
		warning("dce: alloc: RECVRCVINFO failed\n");
		err = errno;
		goto out;
	}
	sctp_err = usrsctp_setsockopt(dce->sock, IPPROTO_SCTP,
				      SCTP_EXPLICIT_EOR, &on, sizeof(int));
	if (sctp_err < 0) {
		warning("dce: alloc: EXPLICIT_EOR failed\n");
		err = errno;
		goto out;
	}
	/* Allow resetting streams. */
	av.assoc_id = SCTP_ALL_ASSOC;
	av.assoc_value = SCTP_ENABLE_RESET_STREAM_REQ
		       | SCTP_ENABLE_CHANGE_ASSOC_REQ;
	sctp_err = usrsctp_setsockopt(dce->sock, IPPROTO_SCTP,
				      SCTP_ENABLE_STREAM_RESET,
				      &av, sizeof(av));
	if (sctp_err < 0) {
		warning("dce: alloc: STREAM_RESET failed\n");
		err = errno;
		goto out;
	}
	/* Enable the events of interest. */
	memset(&event, 0, sizeof(event));
	event.se_assoc_id = SCTP_ALL_ASSOC;
	event.se_on = 1;
	for (i = 0; i < sizeof(event_types)/sizeof(uint16_t); i++) {
		event.se_type = event_types[i];
		sctp_err = usrsctp_setsockopt(dce->sock, IPPROTO_SCTP,
					      SCTP_EVENT,
					      &event, sizeof(event));
		if (sctp_err < 0) {
			warning("dce: alloc: SCTP_EVENT failed\n");
			err = errno;
			goto out;
		}
	}
	memset(&initmsg, 0, sizeof(initmsg));
	initmsg.sinit_num_ostreams = 5;
	initmsg.sinit_max_instreams = 65535;
	sctp_err = usrsctp_setsockopt(dce->sock, IPPROTO_SCTP, SCTP_INITMSG,
				      &initmsg, sizeof(initmsg));
	if (sctp_err < 0) {
		warning("dce: alloc: SCTP_INITMSG failed\n");
		err = errno;
		goto out;
	}
	
	sctp_err = usrsctp_setsockopt(dce->sock, IPPROTO_SCTP,
				      SCTP_NODELAY, &on, sizeof(int));
	if (sctp_err < 0) {
		err = errno;
		warning("dce: alloc: SCTP_NODELAY failed\n");
		goto out;
	}
	
	memset(&sconn, 0, sizeof(sconn));
	sconn.sconn_family = AF_CONN;
#ifdef HAVE_SCONN_LEN
	sconn.sconn_len = sizeof(sconn);
#endif
	sconn.sconn_port = htons(port);
	sconn.sconn_addr = dce;
	info("dce: alloc: binding: %p:%d\n", dce, port);
	sctp_err = usrsctp_bind(dce->sock,
				(struct sockaddr *)&sconn, sizeof(sconn));
	if (sctp_err < 0) {
		err = errno;
		warning("dce: alloc: bind failed: %m\n", err);
		goto out;
	}
	
	debug("dce: init done\n");

	lock_peer_connection(&dce->pc);
	dce->pc.sock = dce->sock;
	unlock_peer_connection(&dce->pc);

	dce->snd_dry_event = true;
    
	list_init(&dce->channell);
    
	dce->magic = DCE_MAGIC;

	lock_write_get(g_dce.lock);
	list_append(&g_dce.dcel, &dce->le, dce);
	lock_rel(g_dce.lock);
    
 out:
	if (err)
		mem_deref(dce);
	else
		*dcep = dce;

	return err;
}

int dce_channel_alloc(struct dce_channel **chp,
		      struct dce *dce,
		      const char *label,
		      const char *protocol,
		      dce_estab_h *estabh,
		      dce_open_chan_h *openh,
		      dce_close_chan_h *closeh,
		      dce_data_h *datah,
		      void *arg)
{
	struct dce_channel *ch;
    
	if (!dce)
		return EINVAL;

	assert(DCE_MAGIC == dce->magic);
    
	ch = get_dce_channel(dce, label);
	if (ch != NULL) {
		warning("dce: channel with label %s already allocated\n",
			label);

		return EALREADY;
	}

	ch = mem_zalloc(sizeof(*ch), NULL);
	if (!ch)
		return ENOMEM;
	
	str_ncpy(ch->label, label, sizeof(ch->label));
	str_ncpy(ch->protocol, protocol, sizeof(ch->protocol));
	ch->estabh = estabh;
	ch->openh = openh;
	ch->closeh = closeh;
	ch->datah = datah;
	ch->arg = arg;
	ch->id = -1;
    
	list_append(&dce->channell, &ch->le, ch);

	if (chp)
		*chp = ch;
    
	return 0;
}


bool dce_is_chan_open(const struct dce_channel *ch)
{
	if(!ch){
		return false;
	}
	if(ch->id == -1){
		return false;
	} else {
		return true;
	}
}
