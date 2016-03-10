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
#ifndef VIE_TIMEOUT_IMAGE_DATA_H
#define VIE_TIMEOUT_IMAGE_DATA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif
    
#define TIMEOUT_IMAGE_H 60
#define TIMEOUT_IMAGE_W 268
    
#define TIMEOUT_FULL_IMAGE_H 1280
#define TIMEOUT_FULL_IMAGE_W 720
    
extern const uint8_t vie_timeout_image_Y[ TIMEOUT_IMAGE_H * TIMEOUT_IMAGE_W ];
extern const uint8_t vie_timeout_image_U[ TIMEOUT_IMAGE_H/2 * TIMEOUT_IMAGE_W/2 ];
extern const uint8_t vie_timeout_image_V[ TIMEOUT_IMAGE_H/2 * TIMEOUT_IMAGE_W/2 ];
    
#ifdef __cplusplus
}
#endif

#endif

