/*
	An error-free in-memory Base64 encoder.

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

#include <string.h>	/* For memcpy() */

#include "base64.h"

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void EncodeBlock(unsigned char In[BASE64_IN_SIZE], unsigned char Out[BASE64_OUT_SIZE], unsigned int Length)
{
	unsigned int Index;

	for (Index = Length; Index < BASE64_IN_SIZE; Index++)
		In[Index] = 0;

    Out[0] = B64[In[0] >> 2];
    Out[1] = B64[((In[0] & 0x03) << 4) | ((In[1] & 0xF0) >> 4)];
    Out[2] = (unsigned char)(Length > 1 ? B64[((In[1] & 0x0F) << 2) | ((In[2] & 0xC0) >> 6)] : '=');
    Out[3] = (unsigned char)(Length > 2 ? B64[In[2] & 0x3F] : '=');

	return;
}

void InitEncode64(B64Stream *Stream)
{
	Stream->AvailIn = Stream->AvailOut = \
		Stream->TotalIn = Stream->TotalOut = \
		Stream->BlockSize = Stream->BlockOut = 0;

	return;
}

void Encode64(B64Stream *Stream, int Finished)
{
	unsigned char OutBlock[BASE64_OUT_SIZE];

	/* Loop until we have no more input or unable to output. */
	while (Stream->AvailIn != 0 || Stream->BlockSize != 0) {

		/* If anything is in our block for output, dump it. */
		if (Stream->BlockOut && Stream->BlockSize != 0) {

			for (; Stream->BlockSize != 0; Stream->BlockSize--) {

				if (Stream->AvailOut == 0)
					return;

				*Stream->NextOut = Stream->Block[BASE64_OUT_SIZE - Stream->BlockSize];
				Stream->NextOut++;
				Stream->TotalOut++;
				Stream->AvailOut--;
			}

		}

		/* Now that our buffer is empty, fill the input. */
		Stream->BlockOut = 0;
		for (; Stream->BlockSize < BASE64_IN_SIZE; Stream->BlockSize++) {

			/* Out of input. This may be expected if there is no data left. */
			if (Stream->AvailIn == 0) {
				if (!Finished || Stream->BlockSize == 0)
					return;
				break;
			}

			Stream->Block[Stream->BlockSize] = *Stream->NextIn;
			Stream->NextIn++;
			Stream->TotalIn++;
			Stream->AvailIn--;
		}

		/* Encode the input we have. */
		EncodeBlock(Stream->Block, OutBlock, Stream->BlockSize);

		Stream->BlockSize = BASE64_OUT_SIZE;
		memcpy(Stream->Block, OutBlock, Stream->BlockSize);

		Stream->BlockOut = 1;
	}

	return;
}
