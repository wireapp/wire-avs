
#include <string.h>
#include <re.h>
#include "avs_log.h"
#include "avs_jzon.h"
#include "avs_uuid.h"
#include "avs_zapi.h"


int zapi_prekey_encode(struct json_object *jobj, const struct zapi_prekey *pk)
{
	int err;

	if (!jobj || !pk)
		return EINVAL;

	if (!pk->key_len || pk->key_len > ZAPI_PREKEY_MAX)
		return EINVAL;

	err  = jzon_add_base64(jobj, "key", pk->key, pk->key_len);
	err |= jzon_add_int(jobj, "id", pk->id);

	return err;
}


int zapi_prekey_decode(struct zapi_prekey *pk, struct json_object *jobj)
{
	const char *key;
	size_t len;
	int id;
	int err;

	if (!pk || !jobj)
		return EINVAL;

	key = jzon_str(jobj, "key");
	err = jzon_int(&id, jobj, "id");
	if (!key || err)
		return EBADMSG;

	len = str_len(key) * 3 / 4;

	if (len > ZAPI_PREKEY_MAX) {
		warning("zapi: prekey_decode: buffer too small\n");
		return EOVERFLOW;
	}

	err = base64_decode(key, str_len(key), pk->key, &len);
	if (err)
		return err;

	pk->key_len = len;
	pk->id = id;

	return 0;
}
