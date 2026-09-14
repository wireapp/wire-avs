/*
* Wire
* Copyright (C) 2026 Wire Swiss GmbH
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

/* Role-specific callbacks for the optional publish/subscribe transports. */

#include <re.h>
#include <avs.h>
#include "ccall.h"

static bool valid_role(enum ccall_ecall_role role)
{
	return role == CCALL_ECALL_PUBLISHER || role == CCALL_ECALL_SUBSCRIBER;
}

static struct ccall_ecall_context *current(struct icall *icall, void *arg)
{
	struct ccall_ecall_context *ctx;
	struct ecall *ecall;

	/* Check the live callback argument before dereferencing a saved context:
	 * release detaches it, including when an external reference keeps the
	 * old ecall alive after the conference has been destroyed.
	 */
	if (!icall || !arg || icall->arg != arg)
		return NULL;
	ctx = arg;
	if (!ctx->ccall || !ctx->ccall->enable_publish_subscribe)
		return NULL;
	ecall = ccall_get_ecall(ctx->ccall, ctx->role);
	return ecall && ecall_get_icall(ecall) == icall ? ctx : NULL;
}

static void emit(struct ccall_ecall_context *ctx, enum ccall_transport_event event)
{
	struct ccall *ccall = ctx->ccall;
	struct ccall_transport_state state = ctx->state;

	/* The observer may end or destroy the conference; do not touch it after. */
	if (ccall->transport_handlers.eventh)
		ccall->transport_handlers.eventh(ccall, ctx->role, event, &state,
						 ccall->transport_arg);
}

static int send_handler(struct icall *icall, const char *userid,
		struct econn_message *msg, struct list *targets,
		bool my_clients_only, void *arg)
{
	struct ccall_ecall_context *ctx = current(icall, arg);
	(void)userid;
	(void)targets;
	(void)my_clients_only;
	if (!ctx)
		return ENOENT;
	if (!msg)
		return EINVAL;
	if (!ctx->ccall->transport_handlers.sendh)
		return ENOSYS;
	return ctx->ccall->transport_handlers.sendh(ctx->ccall, ctx->role,
						   msg, ctx->ccall->transport_arg);
}

static void setup_handler(struct icall *icall, uint32_t msg_time,
		const char *userid, const char *clientid, bool video,
		bool should_ring, enum icall_conv_type conv_type, void *arg)
{
	struct ccall_ecall_context *ctx = current(icall, arg);
	(void)msg_time;
	(void)userid;
	(void)clientid;
	(void)video;
	(void)should_ring;
	(void)conv_type;
	if (!ctx)
		return;
	ctx->state.offer_received = true;
	/* The adapter decides when to answer the offer accepted by econn. */
	emit(ctx, CCALL_TRANSPORT_OFFER_RECEIVED);
}

static void answer_handler(struct icall *icall, void *arg)
{
	struct ccall_ecall_context *ctx = current(icall, arg);
	if (!ctx)
		return;
	ctx->state.answer_received = true;
	emit(ctx, CCALL_TRANSPORT_ANSWER_RECEIVED);
}

static void media_handler(struct icall *icall, const char *userid,
		const char *clientid, bool update, void *arg)
{
	struct ccall_ecall_context *ctx = current(icall, arg);
	(void)userid;
	(void)clientid;
	(void)update;
	if (!ctx)
		return;
	/* Acknowledge readiness on this ecall, cancelling its media-start timer.
	 * The legacy conference callback must not start the other role instead.
	 */
	ecall_media_start(ccall_get_ecall(ctx->ccall, ctx->role));
	ctx->state.media_ready = true;
	emit(ctx, CCALL_TRANSPORT_MEDIA_READY);
}

static void audio_handler(struct icall *icall, const char *userid,
		const char *clientid, bool update, void *arg)
{
	struct ccall_ecall_context *ctx = current(icall, arg);
	(void)userid;
	(void)clientid;
	(void)update;
	if (!ctx)
		return;
	ctx->state.audio_ready = true;
	emit(ctx, CCALL_TRANSPORT_AUDIO_READY);
}

static void data_handler(struct icall *icall, const char *userid,
		const char *clientid, bool update, void *arg)
{
	struct ccall_ecall_context *ctx = current(icall, arg);
	(void)userid;
	(void)clientid;
	(void)update;
	if (!ctx)
		return;
	ctx->state.data_ready = true;
	emit(ctx, CCALL_TRANSPORT_DATA_READY);
}

static void stopped_handler(struct icall *icall, void *arg)
{
	struct ccall_ecall_context *ctx = current(icall, arg);
	if (!ctx)
		return;
	ctx->state.media_ready = false;
	ctx->state.audio_ready = false;
	emit(ctx, CCALL_TRANSPORT_MEDIA_STOPPED);
}

void ccall_pubsub_release(struct ccall *ccall, enum ccall_ecall_role role)
{
	struct ecall **slot;
	struct ecall *ecall;
	if (!ccall || !valid_role(role))
		return;
	slot = role == CCALL_ECALL_PUBLISHER
		? &ccall->ecall : &ccall->ecall_subscriber;
	ecall = *slot;
	*slot = NULL;
	if (ecall) {
		/* All pubsub handlers tolerate a detached argument. */
		ecall_get_icall(ecall)->arg = NULL;
		mem_deref(ecall);
	}
}

static void close_handler(struct icall *icall, int err,
		struct icall_metrics *metrics, uint32_t msg_time,
		const char *userid, const char *clientid, void *arg)
{
	struct ccall_ecall_context *ctx = current(icall, arg);
	(void)metrics;
	(void)msg_time;
	(void)userid;
	(void)clientid;
	if (!ctx)
		return;
	ctx->state.media_ready = false;
	ctx->state.audio_ready = false;
	ctx->state.data_ready = false;
	ctx->state.closed = true;
	ctx->state.error = err;
	ccall_pubsub_release(ctx->ccall, ctx->role);
	emit(ctx, CCALL_TRANSPORT_CLOSED);
}

static void quality_handler(struct icall *icall, const char *userid,
		const char *clientid, struct stats_report stats,
		enum icall_conv_type peer, void *arg)
{
	struct ccall_ecall_context *ctx = current(icall, arg);
	(void)userid;
	(void)clientid;
	(void)peer;
	if (ctx && ctx->ccall->transport_handlers.qualityh)
		ctx->ccall->transport_handlers.qualityh(ctx->ccall, ctx->role,
						      &stats, ctx->ccall->transport_arg);
}

static void level_handler(struct icall *icall, struct list *levels, void *arg)
{
	struct ccall_ecall_context *ctx = current(icall, arg);
	if (ctx && ctx->ccall->transport_handlers.audio_levelh)
		ctx->ccall->transport_handlers.audio_levelh(ctx->ccall, ctx->role,
						      levels, ctx->ccall->transport_arg);
}

void ccall_pubsub_bind(struct ccall *ccall, struct ecall *ecall,
		       enum ccall_ecall_role role)
{
	struct ccall_ecall_context *ctx;
	if (!ccall || !ecall || !valid_role(role) ||
	    !ccall->enable_publish_subscribe)
		return;
	ctx = &ccall->transport[role];
	ctx->ccall = ccall;
	ctx->role = role;
	memset(&ctx->state, 0, sizeof(ctx->state));
	icall_set_callbacks(ecall_get_icall(ecall), send_handler, NULL,
		setup_handler, answer_handler, media_handler, audio_handler,
		data_handler, stopped_handler, NULL, NULL, close_handler,
		NULL, NULL, NULL, NULL, quality_handler, NULL, NULL,
		level_handler, NULL, ctx);
}

void ccall_pubsub_end(struct ccall *ccall)
{
	struct ecall *calls[2];
	unsigned i;

	if (ccall->transport_ending)
		return;
	mem_ref(ccall);
	ccall->transport_ending = true;
	calls[0] = mem_ref(ccall->ecall);
	calls[1] = mem_ref(ccall->ecall_subscriber);
	for (i = 0; i < 2; ++i) {
		enum ccall_ecall_role role = (enum ccall_ecall_role)i;
		if (calls[i] && ccall_get_ecall(ccall, role) == calls[i]) {
			struct icall *icall = ecall_get_icall(calls[i]);
			ecall_end(calls[i]);
			/* econn may close synchronously, asynchronously, or be absent.
			 * Complete local teardown once; late callbacks are detached.
			 */
			if (ccall_get_ecall(ccall, role) == calls[i])
				close_handler(icall, 0, NULL, 0, NULL, NULL, icall->arg);
		}
		mem_deref(calls[i]);
	}
	ccall->transport_ending = false;
	mem_deref(ccall);
}

int ccall_set_transport_handlers(struct ccall *ccall,
		const struct ccall_transport_handlers *handlers, void *arg)
{
	if (!ccall)
		return EINVAL;
	if (handlers)
		ccall->transport_handlers = *handlers;
	else
		memset(&ccall->transport_handlers, 0, sizeof(ccall->transport_handlers));
	ccall->transport_arg = handlers ? arg : NULL;
	return 0;
}

int ccall_get_transport_state(const struct ccall *ccall,
		enum ccall_ecall_role role, struct ccall_transport_state *state)
{
	if (!ccall || !state || !valid_role(role))
		return EINVAL;
	if (!ccall->enable_publish_subscribe)
		return ENOTSUP;
	*state = ccall->transport[role].state;
	return 0;
}
