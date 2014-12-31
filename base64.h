#ifndef BASE64_H
#define BASE64_H

#define BASE64_OUT_SIZE		4
#define BASE64_IN_SIZE		3

typedef struct B64Stream {
	unsigned int AvailIn;
	unsigned int TotalIn;
	unsigned char *NextIn;

	unsigned int AvailOut;
	unsigned int TotalOut;
	char *NextOut;

	/* Internal cache. */
	unsigned int BlockSize;
	int BlockOut;	/* Direction of block. (Input or Output) */
	unsigned char Block[BASE64_OUT_SIZE];
} B64Stream;

void InitEncode64(B64Stream *Stream);
void Encode64(B64Stream *Stream, int Finished);

#endif
