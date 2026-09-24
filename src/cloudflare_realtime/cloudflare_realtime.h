/*
 * Cloudflare Realtime provider adapter.
 */

#ifndef CLOUDFLARE_REALTIME_H
#define CLOUDFLARE_REALTIME_H

#include <stddef.h>
#include <stdbool.h>

struct cloudflare_realtime;
struct ccall;
struct econn_message;
enum ccall_ecall_role;
enum ccall_transport_event;

enum cloudflare_realtime_operation {
	CLOUDFLARE_REALTIME_OP_NONE = 0,
	CLOUDFLARE_REALTIME_OP_CREATE_SESSION,
	CLOUDFLARE_REALTIME_OP_PUBLISH_TRACKS,
	CLOUDFLARE_REALTIME_OP_SUBSCRIBE_TRACKS,
	CLOUDFLARE_REALTIME_OP_RENEGOTIATE,
};

typedef void (cloudflare_realtime_response_h)(
	struct cloudflare_realtime *adapter,
	enum ccall_ecall_role role,
	int status,
	const char *body,
	void *arg);

/* The application owns the HTTP implementation (native REST or JS fetch). */
typedef int (cloudflare_realtime_request_h)(
	struct cloudflare_realtime *adapter,
	enum ccall_ecall_role role,
	const char *method,
	const char *path,
	const char *app_id,
	const char *app_secret,
	const char *body,
	cloudflare_realtime_response_h *responseh,
	void *arg);

struct cloudflare_realtime_conf {
	const char *app_id;
	const char *app_secret;
	const char *api_base;
	cloudflare_realtime_request_h *requesth;
};

int cloudflare_realtime_alloc(struct cloudflare_realtime **adapterp,
			      struct ccall *ccall,
			      const struct cloudflare_realtime_conf *conf,
			      void *arg);

int cloudflare_realtime_enable(struct cloudflare_realtime *adapter,
			       bool enabled);

int cloudflare_realtime_set_session(struct cloudflare_realtime *adapter,
				    enum ccall_ecall_role role,
				    const char *session_id);

int cloudflare_realtime_set_subscribe_tracks(
				struct cloudflare_realtime *adapter,
				const char *tracks_json);

int cloudflare_realtime_map_message(
				struct cloudflare_realtime *adapter,
				enum ccall_ecall_role role,
				const struct econn_message *msg,
				enum cloudflare_realtime_operation *operation);

/* Feed an HTTP response from the configured request callback back to ECall. */
int cloudflare_realtime_handle_response(
				struct cloudflare_realtime *adapter,
				enum ccall_ecall_role role,
				int status,
				const char *body);

void cloudflare_realtime_close(struct cloudflare_realtime *adapter);

#endif
