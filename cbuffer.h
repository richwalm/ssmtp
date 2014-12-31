#ifndef CBUFFER_H
#define CBUFFER_H

typedef struct CSendBuffer {
	char *Data;
	unsigned int Size, Cursor;
	void *CallbackData;
	int (*Callback)(void *, char *, unsigned int);
} CSendBuffer;

int CFlush(CSendBuffer *Buffer);
int CSend(CSendBuffer *Buffer, const char *Data, unsigned int Size);
int CSendStrings(CSendBuffer *Buffer, ...);
void CInit(CSendBuffer *Buffer, char *Data, unsigned int Size, int (*Callback)(void *, char *, unsigned int), void *CallbackData);

#endif
