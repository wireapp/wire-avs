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
//
//  AudioTest.h
//  AudioTest
//
//  Created by Vladimir Darmin on 11/25/14.
//  Copyright (c) 2014 Vladimir Darmin. All rights reserved.
//

#ifndef AudioTest_AudioTest_h
#define AudioTest_AudioTest_h


int acm_test ( int argc, char *argv[] );

int apm_test(int argc, char *argv[], const char *path);

int resampler_test(int argc, char *argv[], const char *path);

int opus_demo(int argc, char *argv[], const char *path);

int voe_conf_test(int argc, char *argv[], const char *path);

int voe_conf_test_dec(const char *path, bool use_build_in_aec, int num_channels);

int start_stop_stress_test(int argc, char *argv[], const char *path);

int voe_loopback_test(int argc, char *argv[], const char *path);

namespace voe_dec_test{
    int voe_dec_test(int argc, char *argv[], const char *path);
}
#endif
