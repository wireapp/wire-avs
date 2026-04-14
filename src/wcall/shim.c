AVS_EXPORT
int  wcall_recv_msg(WUSER_HANDLE wuser, const uint8_t *buf, size_t len,
		    uint32_t curr_time,
		    uint32_t msg_time,
		    const char *convid,
		    const char *userid,
		    const char *clientid,
		    int conv_type,
		    int meeting)
{
	struct calling_instance *inst;
	struct econn_message *msg = NULL;
	char convid_anon[ANON_ID_LEN];
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	char dest_userid_anon[ANON_ID_LEN];
	char dest_clientid_anon[ANON_CLIENT_LEN];
	int err = 0;

	if (!buf || len == 0 || !convid || !userid || !clientid)
		return EINVAL;

	err = econn_message_decode(&msg, curr_time, msg_time,
				   (const char *)buf, len);
	if (err == EPROTONOSUPPORT) {
		warning("wcall: recv_msg: uknown message type, ask user to update client\n");
		return WCALL_ERROR_UNKNOWN_PROTOCOL;
	}
	else if (err) {
		warning("wcall: recv_msg: failed to decode\n");
		return err;
	}


	info("wcall(%p): c3_message_recv: convid=%s from=%s.%s to=%s.%s "
	     "msg=%H age=%u seconds inst=%p\n",
	     wcall, anon_id(convid_anon, convid),
	     anon_id(userid_anon, userid), anon_client(clientid_anon, clientid),
	     strlen(msg->dest_userid) > 0 ? anon_id(dest_userid_anon, msg->dest_userid) : "ALL",
	     strlen(msg->dest_clientid) > 0 ?
	         anon_client(dest_clientid_anon, msg->dest_clientid) : "ALL",
	     econn_message_brief, msg, msg->age, inst);

	if (econn_is_creator(inst->userid, userid, msg) &&
	    (msg->age * 1000) > inst->config.econf.timeout_setup) {
		bool is_video = false;

		if (msg->u.setup.props) {
			const char *vr;

			vr = econn_props_get(msg->u.setup.props, "videosend");
			is_video = vr ? streq(vr, "true") : false;

			if (inst->missedh) {
				uint64_t now = tmr_jiffies();
				inst->missedh(convid, msg_time,
					      userid, clientid,
					      is_video ? 1 : 0,
					      inst->arg);

				info("wcall(%p): inst->missedh (%s) "
				     "took %llu ms\n",
				     wcall, is_video ? "video" : "audio",
				     tmr_jiffies() - now);
			}
		}

		return;
	}
	
	if (!wcall) {
		if (msg->msg_type == ECONN_GROUP_START
		    && econn_message_isrequest(msg)) {
		  // Incoming call
		}
		else if (msg->msg_type == ECONN_CONF_START
		    && econn_message_isrequest(msg)) {
		  // A CONFSTART-Request can be if someone joins a call
		}
		else if (econn_is_creator(inst->userid, userid, msg)) {
		  // This is a 1:1 call
		}
		else {
			err = EPROTO;
		}
		if (err) {
			warning("wcall(%p): recv_msg: could not add call: "
				"%m\n", wcall, err);
			goto out;
		}
	}


	err = ICALL_CALLE(wcall->icall, msg_recv,
			  curr_time, msg_time, userid, clientid, msg);
	if (err) {
		warning("wcall(%p): recv_msg: recv_msg returned error: "
			"%m\n", wcall, err);
	}

 out:
	return;
}
