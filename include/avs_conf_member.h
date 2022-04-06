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

#ifdef __cplusplus
extern "C" {
#endif

struct iflow;

struct conf_member {
	char *userid;
	char *clientid;

	char *userid_hash;

	uint32_t ssrca;
	uint32_t ssrcv;

	uint32_t mida;
	uint32_t midv;
	
	char *cname;
	char *msid;
	char *label;

	bool active;

	struct iflow *flow;

	struct le le;

	uint8_t audio_level;
	uint8_t audio_level_smooth;

	uint32_t audio_frames;
	uint32_t video_frames;

	uint64_t last_ts;
};

int conf_member_alloc(struct conf_member **cmp,
		      struct list *membl,
		      struct iflow *flow,
		      const char *userid,
		      const char *clientid,
		      const char *userid_hash,
		      uint32_t ssrca,
		      uint32_t ssrcv,
		      const char *label);

struct conf_member *conf_member_find_by_userclient(struct list *membl,
						   const char *userid,
						   const char *clientid);

struct conf_member *conf_member_find_active_by_userclient(struct list *membl,
							  const char *userid,
							  const char *clientid);

struct conf_member *conf_member_find_by_label(struct list *membl,
					      const char *label);

struct conf_member *conf_member_find_by_ssrca(struct list *membl,
					      uint32_t ssrc);

struct conf_member *conf_member_find_by_ssrcv(struct list *membl,
					      uint32_t ssrc);

void conf_member_set_audio_level(struct conf_member *cm, int level);

#ifdef __cplusplus
}
#endif

