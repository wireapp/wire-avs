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

#ifndef TEST_CAPTURER_H
#define TEST_CAPTURER_H

void test_capturer_init(void);

void test_capturer_start_static(const char *fname, uint32_t fps);
/* octotunnel: worst case content for the encoder (stress mode) */
void test_capturer_start_dynamic(uint32_t w, uint32_t h, uint32_t fps);
/* procedural talking head: webcam-like content */
void test_capturer_start_head(uint32_t w, uint32_t h, uint32_t fps);
void test_capturer_stop(void);

int32_t test_capturer_framenum(uint8_t *buf, uint32_t w);

struct avs_vidframe;

/* Stamp/read a 16 bit sender id into/from an I420 frame. The stamp is a
 * band of large black/white cells (relative to the frame width) below the
 * frame number, with a sync and a check byte, so it survives encoding and
 * downscaling. test_capturer_read_id returns -1 if no valid stamp found.
 */
void test_capturer_stamp_id(struct avs_vidframe *frame, uint16_t id);
int32_t test_capturer_read_id(const struct avs_vidframe *frame);

#endif

