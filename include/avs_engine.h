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
/* libavs -- simple sync engine
 *
 */

#ifndef ZCLIENTCALL__ENGINE_H
#define ZCLIENTCALL__ENGINE_H 1


/************* Forward Declarations ****************************************/

struct engine;
struct engine_user;
struct engine_conv;

struct flowmgr; /* see avs_flowmgr.h  */
struct store;   /* see avs_store.h  */
struct ztime;


/************* Useful Handlers *********************************************/

/* Generic notification handler.
 *
 * A handler that only receives the argument passed upon registration but
 * nothing else.
 */
typedef void (engine_ping_h)(void *arg);


/* Generic status handler.
 *
 * This type is used when there really only ever is success or error to
 * be reported.
 */
typedef void (engine_status_h)(int err, void *arg);


/* Missing client from OTR handler.
 *
 * This type is used to inform the user that an OTR message was missing
 * a recipient client (412 precondition failed).
 */
typedef void (engine_missing_client_h)(const char *userid, const char *clientid, void *arg);


/************* House Keeping ***********************************************/

/* Initialize the engine module.
 */
int engine_init(const char *msys);

/* Close the engine module.
 */
void engine_close(void);

/* Create an engine and start log in.
 *
 * Pass the base URI for requests in *request_uri* and the full URI for
 * establishing a websocket notification channel in *notification_uri*.
 * The *email* and *password* arguments should contain the email and
 * password for the account to log in as.
 *
 * If you pass in a pointer to a store in *store*, it will be used to store
 * information for the logged in user. The user name used in the store will
 * be a hash over *request_uri* and *email*. If *flush_store* is set to true,
 * the user part of the store will be flushed out so that syncing starts from
 * scratch.
 *
 * If you set *clear_cookies* to true, the engine will delete all existing
 * login cookies before asking for a new one. Do this if you are logging
 * in frequently.
 *
 * Optionally, you can pass in string for *user_agent* which wil be used
 * as the User-Agent header field in HTTP request. If you don't, the engine
 * will use its own string.
 *
 * You can pass in three handlers. The *readyh* will be called when the
 * engine has finished its startup phase and is ready for use. If the engine
 * needs to sync, *readyh* is called before sync is finished.
 *
 * Should the engine encounter a fatal error that makes it pointless to
 * continue, it will call the *errorh* and initiate shutdown. Ie., you
 * do not need to call engine_shutdown() in response to *errorh*, that will
 * already have happend.
 *
 * Finally, *shuth* is called when orderly shutdown of the engine in
 * response to engine_shutdown() or a fatal error has finished. You should
 * not call re_cancel() before *shuth* has been called.
 *
 * The function returns 0 on success and a pointer to the newly created
 * engine in *enginep*. Upon error, an appropriate errno is returned.
 */
int engine_alloc(struct engine **enginep, const char *request_uri,
		 const char *notification_uri, const char *email,
		 const char *password, struct store *store, bool flush_store,
		 bool clear_cookies, const char *user_agent,
		 engine_ping_h *readyh, engine_status_h *errorh,
		 engine_ping_h *shuth, void *arg);
int engine_restart(struct engine *eng);

/* Request a full sync.
 *
 * You can only call this function on an active engine. It will then go
 * off and check all its cached information against the backend.
 */
int engine_start_sync(struct engine *engine);

/* Request engine shutdown.
 *
 * Once you have called the function, the engine is unusable. Calling
 * functions is likely to result in EINVAL return values. Basically, you
 * can't do anything anymore but wait for the shutdown handler to be
 * called. Only then should you terminate the main loop and deref the
 * engine.
 */
void engine_shutdown(struct engine *engine);


struct http_cli *engine_get_httpc(struct engine *engine);

/* Get the login token.
 */
const char *engine_get_login_token(struct engine *engine);

/************* Users *******************************************************/

enum engine_conn_status {
	ENGINE_CONN_NONE      = 0,
	ENGINE_CONN_ACCEPTED,
	ENGINE_CONN_BLOCKED,
	ENGINE_CONN_PENDING,
	ENGINE_CONN_IGNORED,
	ENGINE_CONN_SENT,
	ENGINE_CONN_CANCELLED
};

const char *engine_conn_status_string(enum engine_conn_status status);


struct engine_user {
	struct engine *engine;
	char *id;

	bool collected;

	char *email;
	char *phone;
	uint32_t accent_id;
	char *name;
	/* XXX picture asset.  */

	char *display_name;

	enum engine_conn_status conn_status;
	char *conn_message;

	struct engine_conv *conv;	/* One-on-one with this user.  */

	/* User data. Will be derefed on shutdown.
	 */
	void *arg;
};

int engine_lookup_user(struct engine_user **userp, struct engine *engine,
		       const char *id, bool collect);


/* Get yourself
 */
struct engine_user *engine_get_self(struct engine *engine);
bool engine_is_self(struct engine *engine, const char *id);

/* Iterate over all known users.
 */
typedef bool (engine_user_apply_h)(struct engine_user *user, void *arg);
struct engine_user *engine_apply_users(struct engine *engine,
				       engine_user_apply_h *applyh,
				       void *arg);

/* User updates
 */
enum engine_user_changes {
	ENGINE_USER_EMAIL      = 1<<0,
	ENGINE_USER_PHONE      = 1<<1,
	ENGINE_USER_ACCENT_ID  = 1<<2,
	ENGINE_USER_NAME       = 1<<3,
};


/************* Connections *************************************************/

/* Change connection status.
 */
int engine_update_conn(struct engine_user *user,
		       enum engine_conn_status status,
		       engine_status_h *statush, void *arg);


/************* Conversations ***********************************************/

/* Conversation type.
 */
enum engine_conv_type {
	ENGINE_CONV_UNKNOWN = 0,
	ENGINE_CONV_REGULAR,
	ENGINE_CONV_SELF,
	ENGINE_CONV_ONE,
	ENGINE_CONV_CONNECT
};

struct wcall;
struct engine_conv {
	struct engine *engine;
	char *id;
	enum engine_conv_type type;
	char *name;
	struct list memberl;         /* struct engine_conv_member */

	bool active;
	bool archived;
	bool muted;

	/* Event-related information
	 */
	char *last_event;
	char *last_read;
	bool unread;

	/* User data. Will be derefed on shutdown.
	 */
	void *arg;
};


/* Conversation member.
 *
 * These are in struct engine_conv's memberl.
 */
struct engine_conv_member {
	struct le le;
	struct engine_user *user;	/* identifies the user  */
	bool active;			/* currently a member?   */
};

/* Get conversation from its ID.
 */
int engine_lookup_conv(struct engine_conv **convp, struct engine *engine,
		       const char *id);

/* Iterate over conversations
 */
typedef bool (engine_conv_apply_h)(struct engine_conv *conv, void *arg);
struct engine_conv *engine_apply_convs(struct engine *engine,
				       engine_conv_apply_h *applyh,
				       void *arg);

/* printf handler for a conversation's name
 */
int engine_print_conv_name(struct re_printf *pf, struct engine_conv *conv);

int engine_conv_debug(struct re_printf *pf, const struct engine_conv *conv);


/* Conversation updates
 */
enum engine_conv_changes {
	ENGINE_CONV_TYPE = 1 << 0,
	ENGINE_CONV_NAME = 1 << 1,
	ENGINE_CONV_MEMBERS = 1 << 2,
	ENGINE_CONV_ACTIVE = 1 << 3,
	ENGINE_CONV_ARCHIVED = 1 << 4,
	ENGINE_CONV_MUTED = 1 << 5,
	ENGINE_CONV_LAST_EVENT = 1 << 6,
	ENGINE_CONV_LAST_READ = 1 << 7
};


/************* Calls *******************************************************/

struct flowmgr *engine_get_flowmgr(struct engine *engine);


/************* Messages ****************************************************
 *
 * The backend actually calls the things you see in a conversation "events",
 * too. We shall call them "messages," even the things that aren't text
 * messages, in the vain hope to produce less confusion.
 *
 */


/* Set last read message.
 */
int engine_set_last_read(struct engine_conv *conv, const char *msg_id);


struct client_msg {
	char clientid[64];
	uint8_t *cipher;
	size_t cipher_len;

	struct le le; /* member of recipient_msg list */
};


int engine_client_msg_alloc(struct client_msg **msgp, struct list *msgl);

struct recipient_msg {
	char userid[64];
	struct list msgl; /* List of client_msg for this recipient */

	struct le le; /* member of recipient list */
};


int engine_recipient_msg_alloc(struct recipient_msg **rmsgp);

/* OTR high-level APIs */

typedef void (otr_resp_h)(int err, void *arg);

int engine_send_otr_message(struct engine *engine,
			    void *cb,
			    struct engine_conv *conv,
			    const char *target_userid,
			    const char *target_clientid,
			    const char *local_clientid,
			    const uint8_t *data, size_t data_len,
			    bool transient,
			    bool ignore_missing,
			    otr_resp_h *resph, void *arg);
int engine_send_data(struct engine_conv *conv, const char *ctype,
		     uint8_t *data, size_t len);
int engine_send_file(struct engine_conv *conv, const char *ctype,
		     const char *path);
int engine_fetch_asset(struct engine *engine,
		       const char *cid, const char *aid, const char *path);

/************* Listening to Events *****************************************/

typedef void (engine_estab_h)(bool estab, void *arg);
typedef void (engine_user_h)(struct engine_user *user,
			     enum engine_user_changes changes,
			     void *arg);
typedef void (engine_conn_ev_h)(struct engine_user *user,
				enum engine_conn_status old_status,
				void *arg);
typedef void (engine_conv_add_h)(struct engine_conv *conv, void *arg);
typedef void (engine_conv_update_h)(struct engine_conv *conv,
				    enum engine_conv_changes, void *arg);

typedef void (engine_otr_add_msg_h)(struct engine_conv *conv,
				    struct engine_user *from,
				    const struct ztime *timestamp,
				    const uint8_t *cipher, size_t cipher_len,
				    const char *sender, const char *recipient,
				    void *arg);
typedef void (engine_syncdone_h)(void *arg);


struct engine_lsnr {
	struct le le;
	engine_estab_h *estabh;  /* Websocket is established */
	engine_user_h *userh;
	engine_conn_ev_h *connh;
	engine_conv_add_h *addconvh;
	engine_conv_update_h *convupdateh;
	engine_otr_add_msg_h *otraddmsgh;
	engine_syncdone_h *syncdoneh;       /* sync done */
	void *arg;
};

int engine_lsnr_register(struct engine *engine,
			 struct engine_lsnr *lsnr);
void engine_lsnr_unregister(struct engine_lsnr *lsnr);


struct rest_cli *engine_get_restcli(struct engine *engine);

const char *engine_get_msys(void);
int engine_set_trace(struct engine *engine,
		      const char *path, bool use_stdout);
struct trace *engine_get_trace(struct engine *engine);


/************* Clients ******************************************************/

struct zapi_prekey;

typedef void (engine_prekey_h)(const char *userid,
			       const uint8_t *key, size_t key_len,
			       uint16_t id, const char *clientid,
			       bool last, void *arg);

struct prekey_handler {
	engine_prekey_h *prekeyh;
	void *arg;
};

typedef void (engine_client_reg_h)(int err, const char *clientid, void *arg);
typedef void (engine_client_h)(const char *clientid, const char *model,
			       void *arg);
typedef void (engine_get_client_h)(int err, void *arg);

struct client_handler {
	engine_client_reg_h *clientregh;
	engine_client_h *clienth;
	engine_get_client_h *getclih;
	void *arg;
};

typedef void (engine_user_clients_h)(int err, const char *clientidv[],
				     size_t clientidc, void *arg);

int engine_get_clients(struct engine *eng,
		       const struct client_handler *clih);
int engine_get_prekeys(struct engine *eng, const char *userid,
		       const struct prekey_handler *pkh);
int engine_get_client_prekeys(struct engine *eng, const char *userid, const char *clientid,
		       const struct prekey_handler *pkh);
int engine_get_user_clients(struct engine *eng, const char *userid,
			    engine_user_clients_h *uch, void *arg);
int engine_register_client(struct engine *eng,
			   const struct zapi_prekey *lastkey,
			   const struct zapi_prekey *prekeyv, size_t prekeyc,
			   const struct client_handler *clih);
int engine_delete_client(struct engine *eng, const char *clientid);


typedef void (engine_call_shutdown_h)(void *arg);
void engine_call_set_shutdown_handler(struct engine *engine,
				      engine_call_shutdown_h *shuth,
				      void *arg);
void engine_call_shutdown(struct engine *engine);


#endif  /* ZCLIENTCALL__ENGINE_H */
