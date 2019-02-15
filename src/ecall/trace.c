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
#include "avs_log.h"
#include "avs_msystem.h"
#include "avs_zapi.h"
#include "avs_media.h"
#include "avs_econn.h"
#include "avs_econn_fmt.h"
#include "avs_icall.h"
#include "avs_ecall.h"
#include "avs_jzon.h"
#include "avs_ztime.h"
#include "ecall.h"


static void entry_destructor(void *arg)
{
	struct trace_entry *entry = arg;

	list_unlink(&entry->le);
}


void ecall_trace(struct ecall *ecall, const struct econn_message *msg,
		 bool tx, enum econn_transport tp,
		 const char *fmt, ...)
{
	struct trace_entry *entry;
	va_list ap;

	if (!ecall)
		return;

	/* save the ECONN message in the trace queue */
	entry = mem_zalloc(sizeof(*entry), entry_destructor);
	if (entry) {
		entry->ts = tmr_jiffies() - ecall->ts_start;
		entry->tx = tx;
		entry->tp = tp;
		entry->msg_type = msg->msg_type;
		entry->resp = msg->resp;
		entry->age = msg->age;

		list_append(&ecall->tracel, &entry->le, entry);
	}

	if (!ecall->conf.trace)
		return;

	va_start(ap, fmt);

	re_fprintf(stderr,
		   "\033[1;35m"       /* Magenta */
		   "[ %s.%s ] %s %v"
		   "\x1b[;m"
		   ,
		   ecall->userid_self, ecall->clientid_self,
		   tx ? "send --->" : "recv <---",
		   fmt, &ap
		   );

	va_end(ap);
}


int ecall_show_trace(struct re_printf *pf, const struct ecall *ecall)
{
	struct le *le;
	int err;

	if (!ecall)
		return 0;

	err = re_hprintf(pf, "Ecall message trace (%u messages):\n",
			 list_count(&ecall->tracel));

	for (le = list_head(&ecall->tracel); le; le = le->next) {
		struct trace_entry *ent = le->data;

		err = re_hprintf(pf, "* %.3fs  %s via %7s  %10s %-8s"
				 ,
				 .001 * ent->ts,
				 ent->tx ? "send --->" : "recv <---",
				 econn_transp_name(ent->tp),
				 econn_msg_name(ent->msg_type),
				 ent->resp ? "Response" : "Request");
		if (err)
			break;

		if (!ent->tx && ent->tp == ECONN_TRANSP_BACKEND) {
			err |= re_hprintf(pf, "    age=%usec", ent->age);
		}

		err |= re_hprintf(pf, "\n");

		if (err)
			break;
	}

	return err;
}
