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
/* libavs
 *
 * Dictionary
 */

struct dict;

typedef bool (dict_apply_h)(char *key, void *val, void *arg);

int   dict_alloc(struct dict **dictp);
int   dict_add(struct dict *dict, const char *key, void *val);
void  dict_remove(struct dict *dict, const char *key);
void *dict_lookup(const struct dict *dict, const char *key);
void *dict_apply(const struct dict *dict, dict_apply_h *ah, void *arg);
void  dict_flush(struct dict *dict);
uint32_t dict_count(const struct dict *dict);
void dict_dump(const struct dict *dict);
