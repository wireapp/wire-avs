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
 * Conversation management
 */


int engine_save_conv(struct engine_conv *conv);
void engine_send_conv_update(struct engine_conv *conv,
			     enum engine_conv_changes changes);
int engine_fetch_conv(struct engine *engine, const char *id);
void engine_update_conv_unread(struct engine_conv *conv);


/* Module
 */
struct engine_module;
extern struct engine_module engine_conv_module;

