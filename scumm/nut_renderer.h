/* ScummVM - Scumm Interpreter
 * Copyright (C) 2001  Ludvig Strigeus
 * Copyright (C) 2001/2002 The ScummVM project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * $Header$
 */

#ifndef NUT_RENDERER_H
#define NUT_RENDERER_H

#include "common/file.h"

class NutRenderer {
private:
	int32 _offsets[256];
	byte _tmpCodecBuffer[2000];
	byte *_dataSrc;
	byte *_dstPtr;
	bool _initialized;
	bool _loaded;
	int32 _dstWidth, _dstHeight, _dstPitch;

	void decodeCodec44(byte *dst, byte *src, uint32 length);

public:
	NutRenderer();
	~NutRenderer();

	void bindDisplay(byte *dst, int32 width, int32 height, int32 pitch);
	bool loadFont(char *filename, char *dir);
	void drawChar(char c, int32 x, int32 y, byte color);
	void drawString(char *string, int32 x, int32 y, byte color, int32 mode);
	int32 getCharWidth(char c);
	int32 getCharHeight(char c);
	int32 getStringWidth(char *string);
};


#endif
