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


/* Codecs should be registered before module is initialized */
int  extcodec_audio_register(struct aucodec *ac);
void extcodec_audio_unregister(struct aucodec *ac);

int  extcodec_video_register(struct vidcodec *vc);
void extcodec_video_unregister(struct vidcodec *vc);


int  extcodec_audio_init(struct list *aucodecl);
int  extcodec_video_init(struct list *vidcodecl);
void extcodec_audio_close(void);
void extcodec_video_close(void);

