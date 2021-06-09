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

#ifndef AVS_PACKET_QUEUE_H
#define AVS_PACKET_QUEUE_H

#include <stdlib.h>
#include <stdbool.h>

typedef struct locked_queue_t packet_queue_t;


#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	PACKET_TYPE_RTP = 0,
	PACKET_TYPE_RTCP = 1
} packet_type_t;

struct packet_queue_item_t {
	struct le list_elem;
	packet_type_t packet_type;
	uint8_t *packet_data;
	size_t packet_size;
};

int packet_queue_alloc(packet_queue_t **pqp, bool blocking);

int packet_queue_push(packet_queue_t *q, packet_type_t packet_type,
		      const uint8_t *packet_data, size_t packet_size);

int packet_queue_pop(packet_queue_t *q, packet_type_t *packet_type,
		      uint8_t **packet_data, size_t *packet_size);

#ifdef __cplusplus
}
#endif


#endif //AVS_PACKET_QUEUE_H
