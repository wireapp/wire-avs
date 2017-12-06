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


#define CREQ_CONFIG "/calls/config"


#define HTTP_GET    "GET"

#define CTYPE_JSON "application/json"


struct flow;



struct call {
	int dummy;
};


struct flow {
	int dummy;
};


struct flowmgr {

	struct list rrl;

	/* Sync engine callbacks
	 */
	flowmgr_req_h *reqh;
	flowmgr_err_h *errh;
	void *sarg;

	struct le le;
};


typedef void (rr_resp_h)(int status, struct rr_resp *rr,
			 struct json_object *jobj, void *arg);

struct rr_resp {
	uint32_t magic;
	struct le le;

	struct flowmgr *fm;
	uint64_t ts_req;
	uint64_t ts_resp;
	char debug[256];
	rr_resp_h *resph;
	void *arg;
};


/* Internal flowmgr */
int flowmgr_post_flows(struct call *call);
int flowmgr_send_request(struct flowmgr *fm, struct call *call,
		         struct rr_resp *rr,
		         const char *path, const char *method,
		         const char *ctype, struct json_object *jobj);
void flowmgr_silencing(bool silenced);
int  flowmgr_update_conf_parts(struct list *decl);





/* Protocol event handlers */
int  flow_act_handler(struct flow *flow, struct json_object *jobj);
int  flow_del_handler(struct flow *flow);
int  flow_sdp_handler(struct flow *flow, struct json_object *jobj,
		      bool replayed);
int  flow_cand_handler(struct flow *flow, struct json_object *jobj);

void flow_local_sdp_req(struct flow *flow, const char *type, const char *sdp);

/* rr */
int  rr_alloc(struct rr_resp **rrp, struct flowmgr *fm, struct call *call,
	      rr_resp_h *resph, void *arg);
void rr_cancel(struct rr_resp *rr);
void rr_response(struct rr_resp *rr);
bool rr_isvalid(const struct rr_resp *rr);

int  marshal_init(void);
void marshal_close(void);


void flowmgr_start_volume(void);
void flowmgr_cancel_volume(void);
bool flowmgr_is_using_voe(void);


