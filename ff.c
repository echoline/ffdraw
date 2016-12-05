/* borrows from:
 * FFplay : Simple Media Player based on the ffmpeg libraries
 * Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "ff.h"

void
initff(char *file){
	int i;
	AVFormatContext *pFormatCtx = NULL;
	AVCodecContext *pCodecCtx = NULL;

	av_register_all();

	// Open video file
	if(av_open_input_file(&pFormatCtx, file, NULL, 0, NULL)!=0)
		exit(-2); // Couldn't open file

	// Dump information about file onto standard error
	dump_format(pFormatCtx, 0, file, 0);

	// Find the first video stream
	for(i=0; i<pFormatCtx->nb_streams; i++){
		pCodecCtx=&pFormatCtx->streams[i]->codec;
		if(pCodecCtx->codec_type == CODEC_TYPE_VIDEO) {
			break;
		}
	}
	if(i==pFormatCtx->nb_streams)
		exit(-3); // Didn't find a video stream
}
