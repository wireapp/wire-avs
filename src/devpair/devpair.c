/*
 * Wire
 * Copyright (C) 2017 Wire Swiss GmbH
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

#include <re/re.h>
#include <avs.h>

#include "avs_devpair.h"

static struct {
	bool initialized;
	struct dict *pairings;

	struct msystem *msys;
	struct list ecalll;
} devpair = {
	.initialized = false,
	.pairings = NULL,
};

struct devpair_entry {
	char *pairid;
	char *username;
	struct ecall *ecall;
	struct econn_message *msg;

	struct zapi_ice_server turnv[MAX_TURN_SERVERS];
	size_t turnc;

	devpair_send_h *sendh;
	devpair_estab_h *estabh;
	devpair_data_h *datah;
	devpair_close_h *closeh;
	devpair_file_rcv_h *file_rcvh;
	devpair_file_snd_h *file_sndh;
	void *arg;
};


int devpair_init(void)
{
	int err;

	if (devpair.initialized)
		return EALREADY;

	err = dict_alloc(&devpair.pairings);
	if (err)
		return err;

	devpair.initialized = true;

	return err;
}


void devpair_close(void)
{
	devpair.initialized = false;
	devpair.pairings = mem_deref(devpair.pairings);
	mem_deref(devpair.msys);
}


static int set_turnservers(struct devpair_entry *dpe,
			   struct zapi_ice_server *turnv,
			   size_t turnc)
{
	int err;
	size_t i;

	for (i = 0; i < turnc; ++i) {
		err = ecall_add_turnserver(dpe->ecall, &turnv[i]);
		if (err) {
			warning("devpair: failed to set turn: %s\n",
				turnv[i].url);
			goto out;
		}

		dpe->turnv[i] = turnv[i];
	}
	dpe->turnc = turnc;

 out:
	return err;
}

static void dpe_destructor(void *arg)
{
	struct devpair_entry *dpe = arg;

	mem_deref(dpe->pairid);
	mem_deref(dpe->ecall);
	mem_deref(dpe->msg);
	mem_deref(dpe->username);
}


static void ecall_datachan_estab_handler(struct icall *icall, const char *userid,
				   const char *clientid, bool update, void *arg)
{
	struct devpair_entry *dpe = arg;
	(void)icall;
	(void)userid;
	(void)clientid;
	(void)update;

	info("devpair: datachan established for pairid: %s\n", dpe->pairid);

	if (dpe->estabh) {
		msystem_leave(devpair.msys);
		dpe->estabh(dpe->pairid, dpe->arg);
		msystem_enter(devpair.msys);
	}
}
	


static void ecall_close_handler(int err, const char *metrics_json,
				struct icall *icall,
				uint32_t msg_time,
				const char *userid,
				const char *clientid,
				void *arg)
{
	struct devpair_entry *dpe = arg;

	info("devpair: close_handler: pairid: %s err=%m\n",
	     dpe->pairid, err);

	if (dpe->closeh) {
		msystem_leave(devpair.msys);
		dpe->closeh(err, dpe->pairid, dpe->arg);
		msystem_enter(devpair.msys);
	}
}


static int ecall_send_handler(const char *userid_sender,
			      struct econn_message *msg, void *arg)
{
	struct devpair_entry *dpe = arg;
	struct econn_message *dpmsg = NULL;
	char *raw_msg = NULL;
	size_t i;
	int err;

	debug("devpair: send_handler: %H\n", econn_message_brief, msg);

	dpmsg = econn_message_alloc();

	if (msg->msg_type != ECONN_SETUP)
		return EPROTO;

	if (msg->resp) {
		err = econn_message_init(dpmsg,
					 ECONN_DEVPAIR_ACCEPT,
					 msg->sessid_sender);
		if (err) {
			warning("devpair: send_handler: message init failed: "
				"%m\n",
				err);
			goto out;
		}

		str_dup(&dpmsg->u.devpair_accept.sdp, msg->u.setup.sdp_msg);
	}
	else {
		err = econn_message_init(dpmsg,
					 ECONN_DEVPAIR_PUBLISH,
					 msg->sessid_sender);
		if (err) {
			warning("devpair: send_handler: message init failed: "
				"%m\n",
				err);
			goto out;
		}

		for (i = 0; i < dpe->turnc; ++i)
			dpmsg->u.devpair_publish.turnv[i] = dpe->turnv[i];
		dpmsg->u.devpair_publish.turnc = dpe->turnc;
		str_dup(&dpmsg->u.devpair_publish.sdp, msg->u.setup.sdp_msg);
		str_dup(&dpmsg->u.devpair_publish.username, dpe->username);
	}

	info("devpair: send_handler: remapped: %H\n",
		  econn_message_brief, dpmsg);

	err = econn_message_encode(&raw_msg, dpmsg);
	if (err) {
		warning("devpair: send_handler: encode failed: %m\n", err);
		goto out;
	}

	if (dpe->sendh) {
		debug("devpair: calling devpair send_handler: %p data=%s\n",
		      dpe->sendh, raw_msg);

		msystem_leave(devpair.msys);
		dpe->sendh(dpe->pairid,
			   (const uint8_t *)raw_msg, strlen(raw_msg),
			   dpe);
		msystem_enter(devpair.msys);
	}

 out:
	mem_deref(dpmsg);
	mem_deref(raw_msg);

	return err;
}


static void ecall_conn_handler(struct icall *icall,
			       uint32_t msg_time, const char *userid_sender,
			       bool video_call, bool should_ring, void *arg)
{
	struct devpair_entry *dpe = arg;

	info("devpair: conn_handler: ecall=%p\n", dpe->ecall);

	/* Auto-answer */
	ICALL_CALL(icall, answer,
		false, false, NULL);
}


static void user_data_ready_handler(int size, void *arg)
{
	struct devpair_entry *dpe = arg;

	info("devpair(%p): user_data_ready_handler called size = %d bytes \n",
	     dpe, size);

	// TODO: do something here
}


static void user_data_rcv_handler(uint8_t *data, size_t len, void *arg)
{
	struct devpair_entry *dpe = arg;

	info("devpair(%p) user_data_rcv_handler called data = %s \n",
	     dpe, data);

	if (dpe->datah) {
		debug("devpair: calling devpair data_handler: %p \n",
		      dpe->sendh);

		msystem_leave(devpair.msys);
		dpe->datah(dpe->pairid, data, len, arg);
		msystem_enter(devpair.msys);
	}
}

static int alloc_dpe(struct devpair_entry **dpep, const char *pairid)
{
	struct devpair_entry *dpe;
	int err = 0;

	dpe = mem_zalloc(sizeof(*dpe), dpe_destructor);
	if (!dpe)
		return ENOMEM;

	if (!devpair.msys) {
		err = msystem_get(&devpair.msys, "voe", NULL);
		if (err)
			return err;
	}

	err = ecall_alloc(&dpe->ecall, &devpair.ecalll,
			  ICALL_CONV_TYPE_ONEONONE,
			  NULL, devpair.msys,
			  "devpair",
			  "devpair",
			  pairid);

	if (err) {
		warning("devpair: could not allocate ecall: %m\n", err);
		goto out;
	}

	icall_set_callbacks(ecall_get_icall(dpe->ecall),
			 ecall_send_handler,
			 ecall_conn_handler,
			 NULL,
			 NULL,
			 NULL,
			 ecall_datachan_estab_handler,
			 NULL,
			 NULL,
			 NULL,
			 ecall_close_handler,
			 NULL,
			 NULL,
			 NULL,
			 NULL,
			 dpe);

	ecall_set_devpair(dpe->ecall, true);

	err = ecall_add_user_data(dpe->ecall,
			  user_data_ready_handler,
			  user_data_rcv_handler, dpe);
	if (err) {
		warning("devpair: could not add user data: %m\n", err);
		goto out;
	}

	str_dup(&dpe->pairid, pairid);

 out:
	if (err)
		mem_deref(dpe);
	else
		*dpep = dpe;

	return err;
}


int devpair_publish(const char *pairid,
		    const char *username,
		    devpair_send_h *sendh,
		    void *arg)
{
	struct devpair_entry *dpe;
	int err = 0;

	dpe = dict_lookup(devpair.pairings, pairid);
	if (dpe)
		return EALREADY;

	err = alloc_dpe(&dpe, pairid);
	if (err)
		goto out;

	// TODO: use new config framework
#if 0
	cfg = msystem_get_call_config(devpair.msys);
	if (!cfg)
		return ENOSYS;

	turnc = msystem_get_turn_servers(&turnv, devpair.msys);
	if (turnc > 0)
		set_turnservers(dpe, cfg->iceserverv, cfg->iceserverc);
#endif

	err = str_dup(&dpe->username, username);
	if (err)
		goto out;

	dpe->sendh = sendh;
	dpe->arg = arg;

	err = ecall_devpair_start(dpe->ecall);
	if (err) {
		warning("devpair: ecall_start failed: %m\n", err);
		goto out;
	}

	err = dict_add(devpair.pairings, pairid, dpe);
	if (err)
		goto out;

	/* Dictionary is now the owner */
	mem_deref(dpe);

 out:
	if (err)
		mem_deref(dpe);

	return err;
}


int devpair_ack(const char *pairid,
		const uint8_t *data, size_t len,
		devpair_estab_h *estabh,
		devpair_data_h *datah,
		devpair_close_h *closeh)
{
	struct devpair_entry *dpe;
	struct econn_message *msg;
	int err = 0;

	if (!pairid || !data)
		return EINVAL;

	msystem_enter(devpair.msys);

	dpe = dict_lookup(devpair.pairings, pairid);
	if (!dpe) {
		err = ENOENT;
		goto out;
	}

	dpe->estabh = estabh;
	dpe->datah = datah;
	dpe->closeh = closeh;

	err = econn_message_decode(&msg, 0, 0, (const char *)data, len);
	if (err) {
		warning("devpair: ack: failed to decode message\n");
		goto out;
	}

	info("devpair: ack msg=%H\n", econn_message_brief, msg);

	dpe->msg = msg;

	if (msg->msg_type != ECONN_DEVPAIR_ACCEPT) {
		warning("devpair: ack: not a valid message: %s\n",
			econn_msg_name(msg->msg_type));

		err = EPROTO;
		goto out;
	}

	err = ecall_devpair_ack(dpe->ecall, msg, pairid);
	if (err) {
		warning("devpair: ack: failed %m\n", err);
		goto out;
	}

 out:
	msystem_leave(devpair.msys);

	return err;
}


const char *devpair_create(const char *pairid,
			   const uint8_t *data, size_t len)
{
	struct devpair_entry *dpe;
	struct econn_message *msg;
	char *username = NULL;
	int err = 0;

	if (!pairid)
		return NULL;

	info("devpair_create: %zubytes %b\n", len, data, len);

	msystem_enter(devpair.msys);

	dpe = dict_lookup(devpair.pairings, pairid);
	if (dpe) {
		warning("devpair: pairing id: %s already in use\n", pairid);
		err = EALREADY;
		goto out;
	}

	err = alloc_dpe(&dpe, pairid);
	if (err)
		goto out;

	err = econn_message_decode(&msg, 0, 0, (const char *)data, len);
	if (err) {
		warning("devpair: failed to decode message\n");
		goto out;
	}

	info("devpair: msg=%H\n", econn_message_brief, msg);

	dpe->msg = msg;

	if (msg->msg_type != ECONN_DEVPAIR_PUBLISH) {
		warning("devpair: create: not a valid message: %s\n",
			econn_msg_name(msg->msg_type));

		err = EPROTO;
		goto out;
	}

	username = msg->u.devpair_publish.username;

	err = set_turnservers(dpe,
			      msg->u.devpair_publish.turnv,
			      msg->u.devpair_publish.turnc);
	if (err)
		goto out;

	err = dict_add(devpair.pairings, pairid, dpe);
	if (err)
		goto out;

	/* Now owned by the dictionary */
	mem_deref(dpe);

 out:
	msystem_leave(devpair.msys);
	if (err)
		mem_deref(dpe);

	return username;
}


int devpair_accept(const char *pairid,
		   devpair_send_h *sendh,
		   devpair_estab_h *estabh,
		   devpair_data_h *datah,
		   devpair_close_h *closeh,
		   void *arg)
{
	struct devpair_entry *dpe;
	int err = 0;

	if (!pairid)
		return EINVAL;

	msystem_enter(devpair.msys);

	dpe = dict_lookup(devpair.pairings, pairid);
	if (!dpe) {
		err = ENOENT;
		goto out;
	}

	dpe->sendh = sendh;
	dpe->estabh = estabh;
	dpe->datah = datah;
	dpe->closeh = closeh;
	dpe->arg = arg;

	info("devpair: answer for ecall: %p\n", dpe->ecall);
	err = ecall_devpair_answer(dpe->ecall, dpe->msg, pairid);

 out:
	msystem_leave(devpair.msys);

	return err;
}

int devpair_xfer(const char *pairid,
		   const uint8_t *data, size_t len,
		   devpair_xfer_h *xferh, void *arg)
{
	struct devpair_entry *dpe;
	int err = 0;
    
	if (!pairid)
		return EINVAL;
    
	msystem_enter(devpair.msys);
    
	dpe = dict_lookup(devpair.pairings, pairid);
	if (!dpe) {
		err = ENOENT;
		goto out;
	}
        
	err = ecall_user_data_send(dpe->ecall, data, len);
	if (xferh) {
		msystem_leave(devpair.msys);
		xferh(err, pairid, arg);
		msystem_enter(devpair.msys);
	}
out:
	msystem_leave(devpair.msys);
    
	return err;
}

static void user_data_file_rcv_handler(const char *location, void *arg)
{
	struct devpair_entry *dpe = arg;
    
	if (dpe->file_rcvh) {
		msystem_leave(devpair.msys);
		dpe->file_rcvh(dpe->pairid, location, dpe->arg);
		msystem_enter(devpair.msys);
	}
}

static void user_data_file_snd_handler(const char *name, bool success, void *arg)
{
	struct devpair_entry *dpe = arg;
    
	if (dpe->file_sndh) {
		msystem_leave(devpair.msys);
		dpe->file_sndh(dpe->pairid, name, success, dpe->arg);
		msystem_enter(devpair.msys);
	}
}

int devpair_register_ft_handlers(const char *pairid,
		   const char *rcv_path,
		   devpair_file_rcv_h *f_rcv_h,
		   devpair_file_snd_h *f_snd_h)
{
	struct devpair_entry *dpe;
	int err = 0;
    
	if (!pairid)
		return EINVAL;
    
	msystem_enter(devpair.msys);
    
	dpe = dict_lookup(devpair.pairings, pairid);
	if (!dpe) {
		err = ENOENT;
		goto out;
	}
    
	dpe->file_rcvh = f_rcv_h;
	dpe->file_sndh = f_snd_h;
    
	err = ecall_user_data_register_ft_handlers(dpe->ecall, rcv_path,
						user_data_file_rcv_handler,
						user_data_file_snd_handler);
    
out:
	msystem_leave(devpair.msys);
    
	return err;
}

int devpair_xfer_file(const char *pairid,
		   const char *file,
		   const char *name,
		   int speed_kbps)
{
	struct devpair_entry *dpe;
	int err = 0;
    
	if (!pairid)
		return EINVAL;
    
	msystem_enter(devpair.msys);
    
	dpe = dict_lookup(devpair.pairings, pairid);
	if (!dpe) {
		err = ENOENT;
		goto out;
	}
    
	err = ecall_user_data_send_file(dpe->ecall, file, name, speed_kbps);
out:
	msystem_leave(devpair.msys);
    
	return err;
}
