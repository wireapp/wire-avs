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

#include <re.h>
#include <rew.h>
#include "avs_log.h"
#include "avs_aucodec.h"
#include "avs_uuid.h"
#include "avs_zapi.h"
#include "avs_media.h"
#include "avs_vidcodec.h"
#include "priv_mediaflow.h"


int dtls_print_sha256_fingerprint(struct re_printf *pf, const struct tls *tls)
{
	uint8_t md[32];
	unsigned int i;
	int err = 0;

	if (!tls)
		return EINVAL;

	err = tls_fingerprint(tls, TLS_FINGERPRINT_SHA256, md, sizeof(md));
	if (err)
		return err;

	for (i=0; i<sizeof(md); i++) {
		err |= re_hprintf(pf, "%s%02X", i==0 ? "" : ":", md[i]);
	}

	return err;
}
