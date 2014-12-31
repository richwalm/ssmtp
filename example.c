/*
	SSMTP example program.

	On MinGW, use the following to compile;
	gcc -Wall example.c ssmtp.c cbuffer.c base64.c -lws2_32 -lDnsapi -o example

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

#include <stdio.h>
#include <Winsock2.h>

#include "ssmtp.h"

#define SMTP_SERVER				"localhost"
#define HELO_HOSTNAME			SMTP_SERVER

#define ADDRESS_SENDER			"\"Mrs. From\" <from@example.org>"
#define ADDRESS_RECEIVER		"\"Mr. To\" <to@example.org>"

#define SUBJECT_LINE			"SSMTP Testing E-mail"
#define MESSAGE_BODY			"This is an example e-mail from an example program with its source code attached."

#define ATTACHMENT_FILE			"example.c"

static SMTPConn SMTPConnection;
static SMTPAttach Attachment;

static FILE *AttachmentFile = NULL;

/* Attachment functions. See below. */
static int ReadAttach(FILE **File, void *Buffer, unsigned int BufferSize);
static void CloseAttach(FILE **File);

int main(int argc, char *argv[])
{
	WSADATA WinsockData;

	if (WSAStartup(MAKEWORD(2,2), &WinsockData) != 0) {
		fprintf(stderr, "Couldn't initialize Winsock.\n");
		return 1;
	}

	/*
	   Adjust the state to the inital disconnected state.
	   This needs to be done as a invalid state will prevent SMTPConnect() from running.
	*/
	SMTPConnection.State = 0;

	/* Attachment infomation. */

	/*
	   This is the filename string that's placed in the e-mail.
	   It's optional. It's up to the receiver to choose the filename.
	   In this case, we'll leave it as the actual filename.
	*/
	Attachment.Filename = ATTACHMENT_FILE;
	/*
	   This is also optional too. If NULL, SSMTP will use 'application/octet-stream'.
	*/
	Attachment.MIMEType = "text/x-c";
	/*
	   This data is passed to the read and close functions below.
	   SSMTP makes no use it so it can be NULL, although you would want something to track the data.
	   For this simple example, we'll just place the FILE pointer.
	*/
	Attachment.ReadData = &AttachmentFile;

	/*
	   These two following function pointers do and the actual reading & closing.
	   Reading may be called several times. If an internal error occurs, Close is then called.
	*/
	Attachment.Read = (int (*)(void *, void *, unsigned int))ReadAttach;
	Attachment.Close = (void (*)(void *))CloseAttach;

	/* This is a linked list for multiple attachments. */
	Attachment.Next = NULL;

	/* Connecting. */
	if (SMTPConnect(&SMTPConnection, SMTP_SERVER, HELO_HOSTNAME) != SMTP_ERR_SUCCESS) {
		fprintf(stderr, "Unable to connect to a working mail server for '%s'.\n", SMTP_SERVER);
		WSACleanup();
		return 1;
	}

	/* Sender's address. Must come before the receiving addresses. */
	if (SMTPAddress(&SMTPConnection, SMTP_ADDRESS_FROM, ADDRESS_SENDER) != SMTP_ERR_SUCCESS) {
		fprintf(stderr, "Unable to send from '%s'.\n", ADDRESS_SENDER);
		SMTPDisconnect(&SMTPConnection);
		WSACleanup();
		return 1;
	}

	/* Receiver's address. SMTP_ADDRESS_TO could also be SMTP_ADDRESS_CC or SMTP_ADDRESS_BCC. */
	if (SMTPAddress(&SMTPConnection, SMTP_ADDRESS_TO, ADDRESS_RECEIVER) != SMTP_ERR_SUCCESS) {
		fprintf(stderr, "Unable to send to '%s'.\n", ADDRESS_RECEIVER);
		SMTPDisconnect(&SMTPConnection);
		WSACleanup();
		return 1;
	}

	/* This does the actual sending. */
	if (SMTPData(&SMTPConnection, SUBJECT_LINE, MESSAGE_BODY, &Attachment) != SMTP_ERR_SUCCESS) {
		fprintf(stderr, "Unable to relay the e-mail.\n");
		SMTPDisconnect(&SMTPConnection);
		WSACleanup();
		return 1;
	}

	SMTPDisconnect(&SMTPConnection);
	WSACleanup();

	return 0;
}

/*
   This function is passed a buffer which is to be filled by the function.
   It returns the amount within it.
   Returning 0 represents EOF and -1 represents an error.
   When an error occurs, this function should handle clean up.
*/
static int ReadAttach(FILE **File, void *Buffer, unsigned int BufferSize)
{
	size_t Result;

	if (*File == NULL) {
		*File = fopen(ATTACHMENT_FILE, "rb");
		if (!*File) {
			fprintf(stderr, "Error while reading '%s'.\n", ATTACHMENT_FILE);
			return -1;
		}
	}

	Result = fread(Buffer, 1, BufferSize, *File);

	if (Result != BufferSize && ferror(*File)) {
		CloseAttach(File);
		return -1;
	}

	return Result;
}

/*
   Only is called when an internal error occurs. Usually if the connection is lost.
*/
static void CloseAttach(FILE **File)
{
	fclose(*File);
	*File = NULL;
	return;
}
