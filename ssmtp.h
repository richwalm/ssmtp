#ifndef SMTP_H
#define SMTP_H

#define SMTP_DEFAULT_PORT	"25"
#ifndef SMTP_BUFFER_SIZE
	#define SMTP_BUFFER_SIZE		2048
#endif

#ifndef SMTP_BLOCKING_TIME
	#define SMTP_BLOCKING_TIME	15000
#endif

/* The follow is only used for MIME data. */
#define SMTP_BOUNDARY_RAND_LENGTH	5
#define SMTP_LINE_LENGTH			76

typedef struct SMTPConn {
	int Socket;
	unsigned int State;

	unsigned int AddressBufferSize, AddressBufferCursor;
	char *AddressBuffer;

	unsigned int TotalSent;
	unsigned int TotalRecv;
} SMTPConn;

typedef struct SMTPAttach {
	char *Filename;
	char *MIMEType;

	void *ReadData;
	int (*Read)(void *, void *, unsigned int);
	void (*Close)(void *);

	struct SMTPAttach *Next;
} SMTPAttach;

int SMTPConnect(SMTPConn *Conn, const char *Domain, const char *HeloLine);
int SMTPAddress(SMTPConn *Conn, int Type, const char *Address);
int SMTPData(SMTPConn *Conn, const char *Subject, const char *Body, SMTPAttach *Attachments);
int SMTPReset(SMTPConn *Conn);
int SMTPDisconnect(SMTPConn *Conn);

enum SMTPStates {
	SMTP_DISCONNECTED,
	SMTP_CONNECTED,
	SMTP_AWAITING_RECIPIENT,
	SMTP_READY
};

enum SMTPAddressType {
	SMTP_ADDRESS_FROM,
	SMTP_ADDRESS_TO,
	SMTP_ADDRESS_CC,
	SMTP_ADDRESS_BCC
};

enum SMTPErrors {
	SMTP_ERR_INVALID_STATE = -1,
	SMTP_ERR_SUCCESS,
	SMTP_ERR_FAILURE,
	SMTP_ERR_BUFFER,	/* Out of memory or static buffer too small. */
	SMTP_ERR_PROTOCOL,	/* Protocol error. Server not following the spec or transfer error. */
	SMTP_ERR_DATA		/* The data passed to the function is invalid. */
};

#endif
