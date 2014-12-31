/*
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

#ifdef _WIN32
	#define WIN32_MEAN_AND_LEAN
	#define _WIN32_WINNT	0x0501
	#include <Ws2tcpip.h>
	#include <Windns.h>
#endif

#include <string.h>

/* Enables 64-bit time functions. */
#ifdef _WIN32
	#if __MSVCRT_VERSION__ <= 0x0601
		#undef __MSVCRT_VERSION__
		#define __MSVCRT_VERSION__	0x0601
	#endif
#endif

#include <time.h>
#include <stdlib.h>	/* For the MIME boundary. */

/*
	Using MinGW's snprintf greatly increases the size of the executable so we don't make use of it.
	They return different return codes though.
*/
#ifdef _WIN32
	#include <stdio.h>
	#define __snprintf	_snprintf
#else
	#define __snprintf	snprintf
#endif

#include "ssmtp.h"
#include "cbuffer.h"
#include "base64.h"

static const char EndOfLine[] = "\r\n";
static const char EndOfData[] = "\r\n.\r\n";

/* Primary used to free the address buffer. */
static int Shutdown(SMTPConn *Conn)
{
	if (Conn->AddressBuffer != NULL)
		free(Conn->AddressBuffer);

	closesocket(Conn->Socket);
	Conn->State = SMTP_DISCONNECTED;

	return 0;
}

static int SendCommand(SMTPConn *Conn, const char *Data, int Size)
{
	unsigned int Offset = 0;
	int Return;

	while (Offset < Size) {

		Return = send(Conn->Socket, Data + Offset, Size - Offset, 0);
		if (Return == SOCKET_ERROR) {
			Shutdown(Conn);
			return -1;
		}

		Conn->TotalSent += Return;
		Offset += Return;
	}

	return 0;
}

static int ReadReply(SMTPConn *Conn, char *Reply, int ReplySize)
{
	int Return, Done, Loop, Char, LineSize, Multiline;
	char Buffer[SMTP_BUFFER_SIZE];

	Done = Char = LineSize = Multiline = 0;

	while (!Done) {

		Return = recv(Conn->Socket, Buffer, sizeof(Buffer), 0);
		switch (Return) {
			case 0:
			case SOCKET_ERROR:
				goto Err;
		}

		Conn->TotalRecv += Return;

		/* Loop through the bytes recevied. */
		for (Loop = 0; Loop < Return; Loop++) {

			/* Copy what we can into the passed buffer. */
			if (ReplySize > 1) {
				*Reply = Buffer[Loop];
				Reply++;
				ReplySize--;
			}

			/* Ensure the first three bytes of a line are digits as per the spec. */
			if (LineSize < 3) {
				if (Buffer[Loop] < '0' || Buffer[Loop] > '9')
					goto Err;
			}
			/* Check for a hypen as the multi-line indicator. */
			else if (LineSize == 3) {
				if (Buffer[Loop] == '-')
					Multiline = 1;
			}

			LineSize++;

			/* Check for the end of line. */
			if (Buffer[Loop] == EndOfLine[Char]) {
				Char++;
				if (Char >= sizeof(EndOfLine) - 1) {
					/* End of line located. */

					Char = LineSize = 0;

					if (!Multiline) {
						Done = 1;
						break;
					}

					Multiline = 0;
				}
			}
			else
				Char = 0;
		}

	}

	*Reply = '\0';
	return 0;

	Err:
	*Reply = '\0';
	Shutdown(Conn);
	return -1;

}

/* On most systems, strftime() is easier but the %z specifier isn't standardized and deals with the locale. */
static int GenerateDate(SMTPConn *Conn, CSendBuffer *CBuffer)
{
	#ifndef _WIN32
	time_t RawTime;
	#else
	__time64_t RawTime;
	#endif
	struct tm GMT, Local, *TimeInfo;
	char Buffer[38];	/* Can store the largest size possible including the NULL byte. Would require a ten digit year! */
	int Return;

	const char *Days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	const char *Months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

	RawTime =
	#ifndef _WIN32
		time(NULL);
	#else
		_time64(NULL);
	#endif
	if (RawTime == -1)
		return 1;

	TimeInfo =
	#ifndef _WIN32
		gmtime(&RawTime);
	#else
		(struct tm *)_gmtime64(&RawTime);
	#endif
	GMT = *TimeInfo;

	TimeInfo =
	#ifndef _WIN32
		localtime(&RawTime);
	#else
		(struct tm *)_localtime64(&RawTime);
	#endif
	Local = *TimeInfo;

	/* I'm not entirely sure this works correctly for all timezones. */
	if (Local.tm_isdst > 0) {
		GMT.tm_hour += 2;
		if (Local.tm_hour > 12)
			GMT.tm_hour += 24;
	} else if (Local.tm_hour >= 12)
		GMT.tm_hour += 24;

	Return = __snprintf(Buffer, sizeof(Buffer),
		"%s, %.2d %s %d %.2d:%.2d:%.2d %+03i00",
		Days[Local.tm_wday], Local.tm_mday, Months[Local.tm_mon], Local.tm_year + 1900,
		Local.tm_hour, Local.tm_min, Local.tm_sec,
		GMT.tm_hour - Local.tm_hour);
	#ifdef _WIN32
	if (Return <= 0)
	#else
	if (Return >= sizeof(Buffer) || Return <= 0)
	#endif
		return 1;

	if (CSendStrings(CBuffer, "Date: ", Buffer, EndOfLine, NULL) != 0)
		return -1;

	return 0;
}

static int MIMEData(CSendBuffer *CBuffer, const char *Body, SMTPAttach *Attachments)
{
	char BoundaryString[64] = "Boundary";

	char *Char;
	unsigned int Var;

	unsigned char DataBuffer[SMTP_BUFFER_SIZE];
	char Base64Buffer[SMTP_BUFFER_SIZE];

	int Return, Done;
	B64Stream B64S;
	unsigned int PrintAmount;

	/* Generate a boundary string and check that it's not in the body. */

	srand(time(NULL));
	Char = &BoundaryString[strlen(BoundaryString)];

	for (;;) {

		for (Var = 0; Var < SMTP_BOUNDARY_RAND_LENGTH; Var++)
			Char[Var] = rand() % 10 + '0';
		Char[Var] = '\0';

		if (strstr(Body, BoundaryString) == NULL)
			break;

	}

	/* Headers. */

	if (CSendStrings(CBuffer,
		"MIME-Version: 1.0", EndOfLine,
		"Content-Type: multipart/mixed; boundary=", BoundaryString, EndOfLine,
		EndOfLine,
		NULL) != 0)
		return SMTP_ERR_PROTOCOL;

	/* Start with the body. */

	if (CSendStrings(CBuffer, "--", BoundaryString, EndOfLine,
		"Content-Type: text/plain", EndOfLine,
		EndOfLine,
		Body, EndOfLine,
		NULL) != 0)
		return SMTP_ERR_PROTOCOL;

	/* Primary loop for the files. */

	while (Attachments != NULL) {

		/* File header. */
		if (CSendStrings(CBuffer, "--", BoundaryString, EndOfLine,
			"Content-Type: ", (Attachments->MIMEType ? Attachments->MIMEType : "application/octet-stream"), EndOfLine,
			"Content-Disposition: attachment",
			NULL) != 0)
			return SMTP_ERR_PROTOCOL;

		if (Attachments->Filename != NULL) {
			if (CSendStrings(CBuffer, "; filename=", Attachments->Filename,
			NULL) != 0)
			return SMTP_ERR_PROTOCOL;
		}

		if (CSendStrings(CBuffer,
			EndOfLine,
			"Content-Transfer-Encoding: base64",
			EndOfLine,
			EndOfLine,
			NULL) != 0)
			return SMTP_ERR_PROTOCOL;

		InitEncode64(&B64S);
		Done = Var = 0;

		/* Base64 data. */
		while (!Done) {

			Return = Attachments->Read(Attachments->ReadData, DataBuffer, sizeof(DataBuffer));
			switch (Return) {
				case -1:
					return SMTP_ERR_DATA;
				case 0:
					Done = 1;	/* Do one last run to flush. */
			}

			B64S.NextIn = DataBuffer;
			B64S.AvailIn = Return;

			while (B64S.AvailIn > 0 || Done) {

				B64S.NextOut = Base64Buffer;
				B64S.AvailOut = sizeof(Base64Buffer);

				Encode64(&B64S, Done);

				/* Limit the line's length. */
				Return = sizeof(Base64Buffer) - B64S.AvailOut;
				Char = Base64Buffer;

				while (Return > 0) {

					PrintAmount = SMTP_LINE_LENGTH - Var;
					if (PrintAmount > Return)
						PrintAmount = Return;

					if (CSend(CBuffer, Char, PrintAmount) != 0) {
						Attachments->Close(Attachments->ReadData);
						return SMTP_ERR_PROTOCOL;
					}

					Var += PrintAmount;
					if (Var >= SMTP_LINE_LENGTH) {

						if (CSend(CBuffer, EndOfLine, sizeof(EndOfLine) - 1) != 0) {
							Attachments->Close(Attachments->ReadData);
							return SMTP_ERR_PROTOCOL;
						}
						Var = 0;

					}

					Return -= PrintAmount;
					Char += PrintAmount;

				}

				if (Done)
					break;
			}

		}

		if (Var != 0 && CSend(CBuffer, EndOfLine, sizeof(EndOfLine) - 1) != 0)
			return SMTP_ERR_PROTOCOL;

		/* Next. */
		Attachments = Attachments->Next;
	}

	/* End. */
	if (CSendStrings(CBuffer, "--", BoundaryString, "--",
		NULL) != 0)
		return SMTP_ERR_PROTOCOL;

	return 0;
}

int SMTPData(SMTPConn *Conn, const char *Subject, const char *Body, SMTPAttach *Attachments)
{
	char Buffer[SMTP_BUFFER_SIZE] = "DATA\r\n";
	CSendBuffer CBuffer;
	int AddressType, Var;
	unsigned int Offset;

	if (Conn->State != SMTP_READY)
		return SMTP_ERR_INVALID_STATE;

	if (strstr(Body, EndOfData) != NULL)
		return SMTP_ERR_DATA;

	if (SendCommand(Conn, Buffer, strlen(Buffer)) != 0 ||
		ReadReply(Conn, Buffer, sizeof(Buffer)) != 0)
		return SMTP_ERR_PROTOCOL;

	if (atoi(Buffer) != 354)
		return SMTP_ERR_FAILURE;

	/* Set up the cached buffer. */
	CInit(&CBuffer, Buffer, sizeof(Buffer), (int (*)(void *, char *, unsigned int))SendCommand, Conn);

	/* Ready to send, so generate the headers. */

	/* Date. */
	GenerateDate(Conn, &CBuffer);

	/* Addresses. */
	AddressType = -1;
	Offset = 0;

	while (Offset < Conn->AddressBufferCursor) {

		if (Conn->AddressBuffer[Offset] != AddressType) {

			if (AddressType != -1) {
				if (CSend(&CBuffer, EndOfLine, sizeof(EndOfLine) - 1) != 0)
					return SMTP_ERR_PROTOCOL;
			}

			AddressType = Conn->AddressBuffer[Offset];
			switch (AddressType) {
				case SMTP_ADDRESS_FROM:
					Var = CSendStrings(&CBuffer, "From: ", NULL);
					break;
				case SMTP_ADDRESS_TO:
					Var = CSendStrings(&CBuffer, "To: ", NULL);
					break;
				case SMTP_ADDRESS_CC:
					Var = CSendStrings(&CBuffer, "Cc: ", NULL);
					break;
				default:
					return SMTP_ERR_BUFFER;
			}

		}
		else
			Var = CSendStrings(&CBuffer, ",", EndOfLine, " ", NULL);

		if (Var != 0)
			return SMTP_ERR_PROTOCOL;

		Var = strlen(&Conn->AddressBuffer[Offset + 1]);
		if (CSend(&CBuffer, &Conn->AddressBuffer[Offset + 1], Var) != 0)
			return SMTP_ERR_PROTOCOL;
		Offset += Var + 2;
	}

	if (CSend(&CBuffer, EndOfLine, sizeof(EndOfLine) - 1) != 0)
		return SMTP_ERR_PROTOCOL;

	/* Subject line if one was provided. */
	if (Subject) {
		if (CSendStrings(&CBuffer, "Subject: ", Subject, EndOfLine, NULL) != 0)
			return SMTP_ERR_PROTOCOL;
	}

	if (!Attachments) {
		/* Main body. */
		if (CSendStrings(&CBuffer, EndOfLine, Body, NULL) != 0)
			return SMTP_ERR_PROTOCOL;
	}
	else
	{
		Var = MIMEData(&CBuffer, Body, Attachments);
		if (Var != 0)
			return Var;
	}

	/* End data. */
	if (CSendStrings(&CBuffer, EndOfData, NULL) != 0)
		return SMTP_ERR_PROTOCOL;

	/* Flush the buffer. */
	if (CFlush(&CBuffer) != 0 ||
		ReadReply(Conn, Buffer, sizeof(Buffer)) != 0)
		return SMTP_ERR_PROTOCOL;

	if (atoi(Buffer) != 250)
		return SMTP_ERR_FAILURE;

	return SMTP_ERR_SUCCESS;
}

int SMTPAddress(SMTPConn *Conn, int Type, const char *Address)
{
	size_t Length;
	unsigned int Loop;

	int InQuotes = 0, ReachedEnd = 0;
	const char *AddressStart;
	unsigned int AddressLength;

	char Buffer[SMTP_BUFFER_SIZE];
	int Return;

	unsigned int Size, AllocSize;
	char *AllocBuffer;

	if (Conn->State == SMTP_DISCONNECTED ||
		(Conn->State == SMTP_CONNECTED && Type != SMTP_ADDRESS_FROM) ||
		(Conn->State >= SMTP_AWAITING_RECIPIENT && Type == SMTP_ADDRESS_FROM))
		return SMTP_ERR_INVALID_STATE;

	/* Locate the e-mail address incase the string passed contains a name as well. */
	Length = strlen(Address);

	AddressStart = NULL;
	AddressLength = 0;

	for (Loop = 0; Loop < Length; Loop++) {

		if (AddressStart)
			AddressLength++;

		if (!(InQuotes & 1)) {

			switch (Address[Loop]) {
				case '<':
					if (AddressStart)
						return SMTP_ERR_DATA;
					AddressStart = &Address[Loop] + 1;
					break;
				case '>':
					if (!AddressStart)
						return SMTP_ERR_DATA;
					Loop = Length;	/* Break out the loop. */
					AddressLength--;
					ReachedEnd = 1;
					break;
			}

		}

		if (Address[Loop] == '"')
			InQuotes++;
	}

	/* If there was no brackets found, we'll use the entire string passed. */
	if (!AddressStart) {
		AddressStart = Address;
		AddressLength = Length;
	}
	else if (!ReachedEnd)
		return SMTP_ERR_DATA;

	if (!memchr(AddressStart, '@', AddressLength))
		return SMTP_ERR_DATA;

	/* If we're here, we may have a valid e-mail address. */

	if (Type == SMTP_ADDRESS_FROM)
		Return = __snprintf(Buffer, sizeof(Buffer), "MAIL FROM:<%.*s>\r\n", AddressLength, AddressStart);
	else
		Return = __snprintf(Buffer, sizeof(Buffer), "RCPT TO:<%.*s>\r\n", AddressLength, AddressStart);
	#ifdef _WIN32
	if (Return <= 0)
	#else
	if (Return >= sizeof(Buffer) || Return <= 0)
	#endif
		return SMTP_ERR_BUFFER;

	if (SendCommand(Conn, Buffer, Return) != 0 ||
		ReadReply(Conn, Buffer, sizeof(Buffer)) != 0)
		return SMTP_ERR_PROTOCOL;

	Return = atoi(Buffer);
	if (Return != 250 && Return != 251)
		return SMTP_ERR_FAILURE;

	/* If not a BCC address, add it to the address buffer. */
	if (Type != SMTP_ADDRESS_BCC) {

		Size = Length + 2;	/* Two extra bytes are added for the end and type bytes. */
		if (Size + Conn->AddressBufferCursor > Conn->AddressBufferSize) {

			AllocSize = Size;
			if (AllocSize < SMTP_BUFFER_SIZE)
				AllocSize = SMTP_BUFFER_SIZE;
			Conn->AddressBufferSize += AllocSize;

			AllocBuffer = realloc(Conn->AddressBuffer, Conn->AddressBufferSize);
			if (!AllocBuffer)
				return SMTP_ERR_BUFFER;
			Conn->AddressBuffer = AllocBuffer;

		}

		Conn->AddressBuffer[Conn->AddressBufferCursor] = Type;
		memcpy(&Conn->AddressBuffer[Conn->AddressBufferCursor + 1], Address, Length);
		Conn->AddressBuffer[Conn->AddressBufferCursor + Size - 1] = '\0';
		Conn->AddressBufferCursor += Size;
	}

	if (Conn->State < SMTP_READY)
		Conn->State++;

	return SMTP_ERR_SUCCESS;
}

static int Connect(SMTPConn *Conn, const char *Server, const char *HeloLine)
{
	struct addrinfo Hints, *Results, *Next;
	char Buffer[SMTP_BUFFER_SIZE];
	int Return;
	#ifdef _WIN32
	CONST DWORD TimeoutLength = SMTP_BLOCKING_TIME;
	#endif

	memset(&Hints, 0, sizeof(Hints));
	Hints.ai_family = AF_UNSPEC;
	Hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(Server, SMTP_DEFAULT_PORT, &Hints, &Results) != 0)
		return -1;

	for (Next = Results; Next != NULL; Next = Next->ai_next) {

		Conn->Socket = socket(Next->ai_family, Next->ai_socktype, Next->ai_protocol);
		if (Conn->Socket == INVALID_SOCKET)
			continue;

		#ifdef _WIN32
		setsockopt(Conn->Socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&TimeoutLength, sizeof(DWORD));
		#endif

		if (connect(Conn->Socket, Next->ai_addr, Next->ai_addrlen) != 0) {
			closesocket(Conn->Socket);
			Conn->Socket = INVALID_SOCKET;
			continue;
		}

		/* Read the header to ensure that it's working. */
		if (ReadReply(Conn, Buffer, sizeof(Buffer)) != 0) {
			Conn->Socket = INVALID_SOCKET;
			continue;
		}
		if (atoi(Buffer) != 220) {
			closesocket(Conn->Socket);
			Conn->Socket = INVALID_SOCKET;
			continue;
		}

		/* Send our HELO string. */
		Return = __snprintf(Buffer, sizeof(Buffer), "HELO %s\r\n", HeloLine);
		#ifdef _WIN32
		if (Return <= 0)
		#else
		if (Return >= sizeof(Buffer) || Return <= 0)
		#endif
		{
			closesocket(Conn->Socket);
			Conn->Socket = INVALID_SOCKET;
			continue;
		}
		if (SendCommand(Conn, Buffer, Return) != 0) {
			Conn->Socket = INVALID_SOCKET;
			continue;
		}

		/* Check its reply. */
		if (ReadReply(Conn, Buffer, sizeof(Buffer)) != 0) {
			Conn->Socket = INVALID_SOCKET;
			continue;
		}
		if (atoi(Buffer) != 250) {
			closesocket(Conn->Socket);
			Conn->Socket = INVALID_SOCKET;
			continue;
		}

		break;
	}

	freeaddrinfo(Results);

	if (Conn->Socket == INVALID_SOCKET)
		return -2;

	Conn->State = SMTP_CONNECTED;

	return 0;
}

/* NOTE: Win32 code. Not portable. */
#ifdef _WIN32
static int CompareMXRecord(const void *First, const void *Second)
{
	return (*(DNS_RECORD **)First)->Data.MX.wPreference - (*(DNS_RECORD **)Second)->Data.MX.wPreference;
}

static int ConnectToMXServer(SMTPConn *Conn, const char *Domain, const char *HeloLine)
{
	DNS_RECORD *DNSResults, *DNSNext, **DNSMXSortArrary;
	HANDLE ProcessHeap;
	unsigned int Amount, Loop;

	if (DnsQuery_A(Domain, DNS_TYPE_MX, DNS_QUERY_STANDARD, NULL, &DNSResults, NULL) == NOERROR) {

		/* Count the number we have and create an array for them. */
		Amount = 0;
		for (DNSNext = DNSResults; DNSNext != NULL; DNSNext = DNSNext->pNext) {
			if (DNSNext->wType == DNS_TYPE_MX)
				Amount++;
		}

		ProcessHeap = GetProcessHeap();
		if (!ProcessHeap) {
			DnsRecordListFree(DNSResults, DnsFreeRecordList);
			return -2;
		}

		DNSMXSortArrary = HeapAlloc(ProcessHeap, 0, Amount * sizeof(DNS_RECORD*));
		if (!DNSMXSortArrary) {
			DnsRecordListFree(DNSResults, DnsFreeRecordList);
			return -2;
		}

		Amount = 0;
		for (DNSNext = DNSResults; DNSNext != NULL; DNSNext = DNSNext->pNext) {
			if (DNSNext->wType == DNS_TYPE_MX) {
				DNSMXSortArrary[Amount] = DNSNext;
				Amount++;
			}
		}

		qsort(&DNSMXSortArrary[0], Amount, sizeof(DNS_RECORD*), CompareMXRecord);

		for (Loop = 0; Loop < Amount; Loop++) {
			if (Connect(Conn, DNSMXSortArrary[Loop]->Data.MX.pNameExchange, HeloLine) == 0)
				break;
		}

		HeapFree(ProcessHeap, 0, DNSMXSortArrary);

		DnsRecordListFree(DNSResults, DnsFreeRecordList);

	}

	/* As per a spec, in a last attempt, try to connect to the A record. */
	if (Conn->State == SMTP_DISCONNECTED) {
		if (Connect(Conn, Domain, HeloLine) != 0)
			return -1;
	}

	return 0;
}
#endif

int SMTPDisconnect(SMTPConn *Conn)
{
	char Buffer[SMTP_BUFFER_SIZE] = "QUIT\r\n";

	if (Conn->State == SMTP_DISCONNECTED)
		return SMTP_ERR_INVALID_STATE;

	if (SendCommand(Conn, Buffer, strlen(Buffer)) != 0)
		return SMTP_ERR_PROTOCOL;

	shutdown(Conn->Socket, SD_SEND);

	/* As per RFC2821, it's recommended that we read the reply. */
	if (ReadReply(Conn, Buffer, sizeof(Buffer)) != 0)
		return SMTP_ERR_PROTOCOL;

	Shutdown(Conn);

	return SMTP_ERR_SUCCESS;
}

int SMTPReset(SMTPConn *Conn)
{
	char Buffer[SMTP_BUFFER_SIZE] = "RSET\r\n";

	if (Conn->State <= SMTP_CONNECTED)
		return SMTP_ERR_INVALID_STATE;

	if (SendCommand(Conn, Buffer, strlen(Buffer)) != 0 ||
		ReadReply(Conn, Buffer, sizeof(Buffer)) != 0)
		return SMTP_ERR_PROTOCOL;

	if (atoi(Buffer) != 250)
		return SMTP_ERR_FAILURE;

	Conn->AddressBufferCursor = 0;
	Conn->State = SMTP_CONNECTED;

	return SMTP_ERR_SUCCESS;
}

int SMTPConnect(SMTPConn *Conn, const char *Domain, const char *HeloLine)
{
	if (Conn->State != SMTP_DISCONNECTED)
		return SMTP_ERR_INVALID_STATE;

	Conn->TotalRecv = Conn->TotalSent = 0;

	if (ConnectToMXServer(Conn, Domain, HeloLine) != 0)
		return SMTP_ERR_FAILURE;

	Conn->AddressBufferSize = Conn->AddressBufferCursor = 0;
	Conn->AddressBuffer = NULL;

	return SMTP_ERR_SUCCESS;
}
