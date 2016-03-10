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
#include <re.h>
#include "avs_packetqueue.h"
#include "avs_lockedqueue.h"


int packet_queue_alloc(packet_queue_t **pqp, bool blocking)
{
	return locked_queue_alloc(pqp, blocking);
}


static void packet_queue_item_destructor(void *arg)
{
	struct packet_queue_item_t *i = (struct packet_queue_item_t *)arg;

	if (i->packet_data) {
		mem_deref(i->packet_data);
	}
}


int packet_queue_push(packet_queue_t* q, packet_type_t packet_type,
		      const uint8_t *packet_data, size_t packet_size)
{
	struct packet_queue_item_t *item;

	if (!q || !packet_data || !packet_size)
		return EINVAL;

	item = mem_zalloc(sizeof(*item), packet_queue_item_destructor);
	if (!item)
		return ENOMEM;

	item->packet_data = (uint8_t *)mem_zalloc(packet_size, NULL);
	if (!item->packet_data) {
		mem_deref(item);
		return ENOMEM;
	}
	item->packet_type = packet_type;
	item->packet_size = packet_size;
	memcpy(item->packet_data, packet_data, packet_size);

	return locked_queue_push(q, &item->list_elem, item);
}


int packet_queue_pop(packet_queue_t* q, packet_type_t *packet_type,
		      uint8_t **packet_data, size_t *packet_size)
{
	struct le *list_elem;
	struct packet_queue_item_t *item;
	int err;

	if (!q)
		return EINVAL;

	err = locked_queue_pop(q, &list_elem);
	if (err != 0) {
		return err;
	}

	if (!list_elem) {
		return ENODATA;
	}

	item = (struct packet_queue_item_t*)list_elem->data;
	*packet_size = item->packet_size;
	*packet_data = mem_ref(item->packet_data);
	*packet_type = item->packet_type;

	mem_deref(item);

	return 0;
}

