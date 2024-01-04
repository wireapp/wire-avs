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
void test_capturer_start_dynamic(uint32_t w, uint32_t h, uint32_t fps);
void test_capturer_stop(void);

int32_t test_capturer_framenum(uint8_t *buf, uint32_t w);

#endif

