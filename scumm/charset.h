/* ScummVM - Scumm Interpreter
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

#ifndef CHARSET_H
#define CHARSET_H

#include "common/scummsys.h"

class Scumm;
struct VirtScreen;

class CharsetRenderer {
public:
	int _strLeft, _strRight, _strTop, _strBottom;
	int _nextLeft, _nextTop;

	int _top;
	int _left, _startLeft;
	int _right;

	byte _color;
	byte _colorMap[16];

	bool _center;
	bool _hasMask;
	bool _ignoreCharsetMask;
	bool _blitAlso;
	bool _firstChar;
	bool _disableOffsX;

	int _bufPos;
	byte _buffer[512];	// TODO - would be really nice to get rid of this

protected:
	Scumm *_vm;

	byte _curId;
	byte *_fontPtr;

	byte _bpp;
	byte *_charPtr;

	void drawBits(VirtScreen *vs, byte *dst, byte *mask, int drawTop, int width, int height);
	byte *getFontPtr(byte id);

public:
	CharsetRenderer(Scumm *vm) : _vm(vm) {}

	void printChar(int chr);
	void printCharOld(int chr);
	int getSpacing(byte chr, byte *charset);
	int getStringWidth(int a, byte *str);
	void addLinebreaks(int a, byte *str, int pos, int maxwidth);
	
	void setCurID(byte id);
	int getCurID() { return _curId; }
	
	int getFontHeight() { return _fontPtr[1]; }
};


#endif
