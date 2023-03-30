/*
* Wire
* Copyright (C) 2020 Wire Swiss GmbH
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

#ifndef AVS_AUDIO_LEVEL_H
#define AVS_AUDIO_LEVEL_H

struct audio_level;

#define AUDIO_LEVEL_FLOOR   2
#define AUDIO_LEVEL_CEIL   30
#define AUDIO_LEVEL_UPDATE_CYCLE 3

int audio_level_alloc(struct audio_level **levelp,
		      struct list *levell, bool is_self,
		      const char *userid, const char *clientid,
		      uint8_t level, uint8_t level_smooth);

const char *audio_level_userid(const struct audio_level *a);
const char *audio_level_clientid(const struct audio_level *a);
int audio_level(struct audio_level *a);

bool audio_level_eq(struct audio_level *a, struct audio_level *b);
bool audio_level_list_cmp(struct le *le1, struct le *le2, void *arg);
uint8_t audio_level_smoothen(uint8_t level, uint8_t new_level);
uint8_t audio_level_smoothen_withk(uint8_t level, uint8_t new_level, float k);

int audio_level_json(struct list *levell,
		     const char *userid_self, const char *clientid_self,
		     char **jsonp, char **anon_p);
int audio_level_json_print(struct re_printf *pf, const struct audio_level *a);
int audio_level_list_debug(struct re_printf *pf, const struct list *levell);

#endif
