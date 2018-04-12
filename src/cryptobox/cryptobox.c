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


#include <assert.h>
#include <re.h>
#include <avs.h>
#include <cbox.h>


struct cryptobox {
	CBox *cbox;
	struct list sessionl;  /* (struct session) populated from disk */
};


/**
 * This object is always atomically initialized
 *
 * The session IDs used for cryptobox should be composed of (at least)
 * the remote user ID and client ID.
 */
struct session {
	struct le le;

	/* aka "sid" or Session-ID: */
	char *remote_userid;
	char *remote_clientid;
	char *local_clientid;

	CBoxSession *cbox_sess;
};


static void mk_sessid(char *sessid, size_t sz,
		      const char *remote_userid,
		      const char *remote_clientid,
		      const char *local_clientid)
{
	re_snprintf(sessid, sz, "%s_%s_%s", local_clientid, remote_userid, remote_clientid);
}


static void cryptobox_destructor(void *data)
{
	struct cryptobox *cb = data;

	list_flush(&cb->sessionl);

	if (cb->cbox) {
		cbox_close(cb->cbox);
		cb->cbox = NULL;
	}
}


int cryptobox_alloc(struct cryptobox **cbp, const char *store_dir)
{
	struct cryptobox *cb;
	CBoxResult r;
	CBoxVec *fp;
	int err = 0;

	if (!cbp || !store_dir)
		return EINVAL;

	info("cryptobox init (%s)\n", store_dir);

	cb = mem_zalloc(sizeof(*cb), cryptobox_destructor);
	if (!cb)
		return ENOMEM;

	if (!cb->cbox) {

		r = cbox_file_open(store_dir, &cb->cbox);
		if (CBOX_SUCCESS != r) {
			warning("cryptobox: could not open file"
				" (result=%d)\n", r);
			err = ENOENT;
			goto out;
		}
	}

	r = cbox_fingerprint_local(cb->cbox, &fp);
	if (CBOX_SUCCESS != r) {
		warning("cryptobox: could not get local fingerprint"
			" (result=%d)\n", r);
		err = ENOENT;
		goto out;
	}

	cbox_vec_free(fp);

 out:
	if (err)
		mem_deref(cb);
	else
		*cbp = cb;

	return err;
}


int cryptobox_generate_prekey(struct cryptobox *cb,
			      uint8_t *key, size_t *sz, uint16_t id)
{
	CBoxVec *prekey = NULL;
	CBoxResult r;

	if (!cb || !key || !sz)
		return EINVAL;

	if (!cb->cbox)
		return EINVAL;

	r = cbox_new_prekey(cb->cbox, id, &prekey);
	if (CBOX_SUCCESS != r) {
		warning("cryptobox: new_prekey failed (result=%d)\n",
			r);
		return ENOENT;
	}

	if (cbox_vec_len(prekey) > *sz)
		return EOVERFLOW;

	memcpy(key, cbox_vec_data(prekey), cbox_vec_len(prekey));
	*sz = cbox_vec_len(prekey);

	cbox_vec_free(prekey);

	return 0;
}


static void session_destructor(void *data)
{
	struct session *sess = data;

	list_unlink(&sess->le);
	mem_deref(sess->local_clientid);
	mem_deref(sess->remote_clientid);
	mem_deref(sess->remote_userid);

	if (sess->cbox_sess)
		cbox_session_close(sess->cbox_sess);
}


int cryptobox_session_add_recv(struct cryptobox *cb,
			       const char *remote_userid,
			       const char *remote_clientid,
			       const char *local_clientid,
			       uint8_t *plain, size_t *plain_len,
			       const uint8_t *cipher, size_t cipher_len)
{
	struct session *sess;
	CBoxVec *vec_plain = NULL;
	CBoxResult r;
	char sid[256];
	int err = 0;

	if (!cb || !plain || !plain_len || !cipher)
		return EINVAL;

	assert(cb->cbox != NULL);

	if (!str_isset(remote_userid) || !str_isset(remote_clientid) || !str_isset(local_clientid))
		return EINVAL;

	sess = mem_zalloc(sizeof(*sess), session_destructor);

	err  = str_dup(&sess->remote_userid, remote_userid);
	err |= str_dup(&sess->remote_clientid, remote_clientid);
	err |= str_dup(&sess->local_clientid, local_clientid);
	if (err)
		goto out;

	mk_sessid(sid, sizeof(sid), remote_userid, remote_clientid, local_clientid);

	r = cbox_session_init_from_message(cb->cbox,
					   sid,
					   cipher,
					   cipher_len,
					   &sess->cbox_sess,
					   &vec_plain);
	if (CBOX_SUCCESS != r) {
		warning("cryptobox: cbox_session_init_from_message"
			" failed (result=%d)\n", r);
		err = EBADMSG;
		goto out;
	}

	info("cryptobox: New cryptobox session created for remote"
		  " userid=%s clientid=%s local_cid=%s\n",
		  remote_userid, remote_clientid, local_clientid);

	/*
	 * Save the session. (ignore any errors)
	 */
	r = cbox_session_save(cb->cbox, sess->cbox_sess);
	if (CBOX_SUCCESS == r) {
		info("cryptobox: session saved (%s)\n", sid);
	}
	else {
		warning("cryptobox: could not save session (result=%d)\n", r);
	}

	if (cbox_vec_len(vec_plain) > *plain_len) {
		warning("cryptobox: plain buffer too small\n");
		*plain_len = 0;
	}
	else {
		memcpy(plain, cbox_vec_data(vec_plain),
		       cbox_vec_len(vec_plain));
		*plain_len = cbox_vec_len(vec_plain);
	}

	list_append(&cb->sessionl, &sess->le, sess);

 out:
	if (vec_plain)
		cbox_vec_free(vec_plain);
	if (err)
		mem_deref(sess);

	return err;
}


/* XXX: uint16_t id is missing in the API, is it needed? */
int cryptobox_session_add_send(struct cryptobox *cb,
			       const char *remote_userid,
			       const char *remote_clientid,
			       const char *local_clientid,
			       const uint8_t *peer_key, size_t peer_key_len)
{
	struct session *sess;
	char sid[256];
	CBoxResult r;
	int err = 0;

	if (!cb)
		return EINVAL;

	assert(cb->cbox != NULL);

	if (!str_isset(remote_userid) || !str_isset(remote_clientid) || !str_isset(local_clientid) ||
	    !peer_key || !peer_key_len)
		return EINVAL;

	sess = mem_zalloc(sizeof(*sess), session_destructor);

	err = str_dup(&sess->remote_userid, remote_userid);
	err = str_dup(&sess->remote_clientid, remote_clientid);
	err = str_dup(&sess->local_clientid, local_clientid);
	if (err)
		goto out;

	mk_sessid(sid, sizeof(sid), remote_userid, remote_clientid, local_clientid);

	r = cbox_session_init_from_prekey(cb->cbox,
					  sid,
					  peer_key,
					  peer_key_len,
					  &sess->cbox_sess);
	if (CBOX_SUCCESS != r) {
		warning("cryptobox: cbox_session_init_from_prekey"
			" failed (result=%d)\n", r);
		err = EBADMSG;
		goto out;
	}

	info("cryptobox: send New crypto session successfully created\n");

	list_append(&cb->sessionl, &sess->le, sess);

 out:
	if (err)
		mem_deref(sess);

	return err;
}


struct session *cryptobox_session_find(struct cryptobox *cb,
				       const char *remote_userid,
				       const char *remote_clientid,
				       const char *local_clientid)
{
	struct session *sess = NULL;
	struct le *le;
	CBoxResult r;
	char sessid[256];
	CBoxSession *cbox_sess = NULL;
	int err = 0;

	if (!cb)
		return NULL;

	assert(cb->cbox != NULL);

	for (le = cb->sessionl.head; le; le = le->next) {
		sess = le->data;

		if (0 == str_casecmp(sess->remote_userid, remote_userid) &&
		    0 == str_casecmp(sess->remote_clientid, remote_clientid) &&
		    0 == str_casecmp(sess->local_clientid, local_clientid))
		{
			return sess;
		}
	}

	mk_sessid(sessid, sizeof(sessid), remote_userid, remote_clientid, local_clientid);

	r = cbox_session_load(cb->cbox, sessid, &cbox_sess);
	if (CBOX_SUCCESS != r) {
		re_printf("cryptobox: find: session %s not found on disk\n",
			sessid);
		return NULL;
	}

	sess = mem_zalloc(sizeof(*sess), session_destructor);

	err = str_dup(&sess->remote_userid, remote_userid);
	err = str_dup(&sess->remote_clientid, remote_clientid);
	err = str_dup(&sess->local_clientid, local_clientid);
	if (err)
		goto out;

	sess->cbox_sess = cbox_sess;

	info("cryptobox: New crypto session successfully"
	     " loaded from disk\n");

	list_append(&cb->sessionl, &sess->le, sess);

 out:
	if (err)
		sess = mem_deref(sess);

	return sess;
}


int cryptobox_session_encrypt(struct cryptobox *cb, struct session *sess,
			      uint8_t *cipher, size_t *cipher_len,
			      const uint8_t *plain, size_t plain_len)
{
	CBoxVec *vec_cipher = NULL;
	CBoxResult r;

	if (!cb || !sess || !cipher || !cipher_len || !plain || !plain_len)
		return EINVAL;

	r = cbox_encrypt(sess->cbox_sess,
			 plain,
			 plain_len,
			 &vec_cipher);
	if (CBOX_SUCCESS != r) {
		warning("cryptobox: encrypt failed (result=%d)\n", r);
		return EBADMSG;
	}

	/*
	 * Save the session. (ignore any errors)
	 */
	r = cbox_session_save(cb->cbox, sess->cbox_sess);
	if (CBOX_SUCCESS != r) {
		warning("cryptobox: could not save session (result=%d)\n", r);
	}

	if (cbox_vec_len(vec_cipher) > *cipher_len) {
		warning("cryptobox: encrypt: buffer too small (%zu > %zu)\n",
			cbox_vec_len(vec_cipher), *cipher_len);
		return EINVAL;
	}

	*cipher_len = cbox_vec_len(vec_cipher);
	memcpy(cipher, cbox_vec_data(vec_cipher), *cipher_len);

	if (vec_cipher)
		cbox_vec_free(vec_cipher);
	return 0;
}


int cryptobox_session_decrypt(struct cryptobox *cb, struct session *sess,
			      uint8_t *plain, size_t *plain_len,
			      const uint8_t *cipher, size_t cipher_len)
{
	CBoxVec *vec_plain = NULL;
	CBoxResult r;

	if (!cb || !sess || !cipher || !cipher_len || !plain || !plain_len)
		return EINVAL;

	r = cbox_decrypt(sess->cbox_sess,
			 cipher, cipher_len,
			 &vec_plain);
	if (CBOX_SUCCESS != r) {
		warning("cryptobox: decrypt failed (result=%d)\n", r);
		return EBADMSG;
	}

	/*
	 * Save the session. (ignore any errors)
	 */
	r = cbox_session_save(cb->cbox, sess->cbox_sess);
	if (CBOX_SUCCESS != r) {
		warning("cryptobox: could not save session (result=%d)\n", r);
	}

	if (cbox_vec_len(vec_plain) > *plain_len) {
		warning("cryptobox: decrypt: buffer too small (%zu > %zu)\n",
			cbox_vec_len(vec_plain), *plain_len);
		goto out;
	}

	*plain_len = cbox_vec_len(vec_plain);
	memcpy(plain, cbox_vec_data(vec_plain), *plain_len);

 out:
	if (vec_plain)
		cbox_vec_free(vec_plain);

	return 0;
}


void cryptobox_dump(const struct cryptobox *cb)
{
	struct le *le;

	if (!cb)
		return;

	re_printf("Cryptobox sessions: (%u)\n", list_count(&cb->sessionl));

	for (le = cb->sessionl.head; le; le = le->next) {
		struct session *sess = le->data;

		re_printf("....user=%s  cli=%s lcli=%s %p\n",
			  sess->remote_userid,
			  sess->remote_clientid,
			  sess->local_clientid,
			  sess->cbox_sess);
	}
}
