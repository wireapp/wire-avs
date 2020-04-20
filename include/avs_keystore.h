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


struct keystore;

int keystore_alloc(struct keystore **pks);

int keystore_set_key(struct keystore *ks, uint32_t index, uint8_t *key, uint32_t ksz);
int keystore_get_key(struct keystore *ks, uint32_t index, const uint8_t **pkey);

int keystore_set_current(struct keystore *ks, uint32_t index);
int keystore_get_current(struct keystore *ks, uint32_t *pindex);
int keystore_get_current_key(struct keystore *ks,
			     uint32_t *pindex,
			     const uint8_t **pkey);



