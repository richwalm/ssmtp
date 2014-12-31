/*
	Simple set of functions used to prevent sending small amounts of data.

	Simple SMTP Mailer.
	Copyright (C) 2013 Richard Walmsley <richwalm@gmail.com>

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in all
	copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
	SOFTWARE.
*/

#include <string.h>
#include <stdarg.h>

#include "cbuffer.h"

int CFlush(CSendBuffer *Buffer)
{
	int Return;

	if (Buffer->Cursor > 0) {

		Return = Buffer->Callback(Buffer->CallbackData, Buffer->Data, Buffer->Cursor);
		if (Return != 0)
			return Return;
		Buffer->Cursor = 0;

	}

	return 0;
}

int CSend(CSendBuffer *Buffer, const char *Data, unsigned int Size)
{
	unsigned int BufferAvailable;
	int Return;

	while (Size > 0) {

		BufferAvailable = Buffer->Size - Buffer->Cursor;
		if (BufferAvailable > Size)
			BufferAvailable = Size;

		memcpy(&Buffer->Data[Buffer->Cursor], Data, BufferAvailable);

		Size -= BufferAvailable;
		Data += BufferAvailable;

		Buffer->Cursor += BufferAvailable;
		if (Buffer->Cursor >= Buffer->Size) {

			Return = Buffer->Callback(Buffer->CallbackData, Buffer->Data, Buffer->Cursor);
			if (Return != 0)
				return Return;
			Buffer->Cursor = 0;
		}

	}

	return 0;
}

int CSendStrings(CSendBuffer *Buffer, ...)
{
	va_list Args;
	char *Arg;
	size_t Length;
	int Return;

	va_start(Args, Buffer);

	Arg = va_arg(Args, char*);
	while (Arg) {

		Length = strlen(Arg);
		Return = CSend(Buffer, Arg, Length);
		if (Return != 0) {
			va_end(Args);
			return Return;
		}
		Arg = va_arg(Args, char*);

	}

	va_end(Args);
	return 0;
}

void CInit(CSendBuffer *Buffer, char *Data, unsigned int Size, int (*Callback)(void *, char *, unsigned int), void *CallbackData)
{
	Buffer->Data = Data;
	Buffer->Size = Size;
	Buffer->Cursor = 0;

	Buffer->Callback = Callback;
	Buffer->CallbackData = CallbackData;

	return;
}
