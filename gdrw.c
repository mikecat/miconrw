#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>
#include "serial.h"

#define ACK 0x79
#define NACK 0x1f

int errorRet(int ret, const char* messageFormat, ...) {
	va_list args;
	va_start(args, messageFormat);
	vfprintf(stderr, messageFormat, args);
	va_end(args);
	return ret;
}

int errorCloseRet(int ret, FILE* fpToClose, const char* messageFormat, ...) {
	int ret2;
	va_list args;
	if (fpToClose != NULL) fclose(fpToClose);
	va_start(args, messageFormat);
	ret2 = errorRet(ret, messageFormat, args);
	va_end(args);
	return ret2;
}

int initCommand(SerialPort* port, int argc, char* argv[]) {
	char outBuf, inBuf;
	(void)argc;
	(void)argv;
	outBuf = 0x7f;
	if (serialSend(port, &outBuf, 1, 1) != 1) return errorRet(1, "failed to send\n");
	if (serialRecv(port, &inBuf, 1, 1) != 1) return errorRet(1, "failed to receive\n");
	if (inBuf != ACK) return errorRet(1, "what is returned is not ACK\n");
	return 0;
}

int readCommand(SerialPort* port, int argc, char* argv[]) {
	unsigned long address;
	unsigned long length;
	FILE* fpOut;
	char* end;
	unsigned char buffer[256];
	if (argc < 3) return errorRet(1, "required argument(s) not given\n");
	errno = 0;
	address = strtoul(argv[0], &end, 0);
	if (argv[0][0] == '\0' || *end != '\0' || errno == ERANGE) return errorRet(1, "invalid address\n");
	length = strtoul(argv[1], &end, 0);
	if (argv[1][0] == '\0' || *end != '\0' || errno == ERANGE) return errorRet(1, "invalid length\n");
	fpOut = fopen(argv[2], "wb");
	if (fpOut == NULL) return errorRet(1, "failed to open output file\n");
	while (length > 0) {
		int lengthToRead = length > 256 ? 256 : (int)length;
		if (serialSend(port, "\x11\xee", 2, 1) != 2) return errorCloseRet(1, fpOut, "failed to send\n");
		if (serialRecv(port, buffer, 1, 1) != 1) return errorCloseRet(1, fpOut, "failed to receive\n");
		if (buffer[0] != ACK) return errorCloseRet(1, fpOut, "what is returned is not ACK\n");
		buffer[0] = (unsigned char)(address >> 24);
		buffer[1] = (unsigned char)(address >> 16);
		buffer[2] = (unsigned char)(address >> 8);
		buffer[3] = (unsigned char)(address);
		buffer[4] = buffer[0] ^ buffer[1] ^ buffer[2] ^ buffer[3];
		if (serialSend(port, buffer, 5, 1) != 5) return errorCloseRet(1, fpOut, "failed to send\n");
		if (serialRecv(port, buffer, 1, 1) != 1) return errorCloseRet(1, fpOut, "failed to receive\n");
		if (buffer[0] != ACK) return errorCloseRet(1, fpOut, "what is returned is not ACK\n");
		buffer[0] = (unsigned char)(lengthToRead - 1);
		buffer[1] = ~buffer[0];
		if (serialSend(port, buffer, 2, 1) != 2) return errorCloseRet(1, fpOut, "failed to send\n");
		if (serialRecv(port, buffer, 1, 1) != 1) return errorCloseRet(1, fpOut, "failed to receive\n");
		if (buffer[0] != ACK) return errorCloseRet(1, fpOut, "what is returned is not ACK\n");
		if (serialRecv(port, buffer, lengthToRead, 1) != lengthToRead) return errorCloseRet(1, fpOut, "failed to receive\n");
		if (fwrite(buffer, 1, lengthToRead, fpOut) != (size_t)lengthToRead) return errorCloseRet(1, fpOut, "failed to write file\n");
		address += lengthToRead;
		length -= lengthToRead;
	}
	fclose(fpOut);
	return 0;
}

int eraseCommand(SerialPort* port, int argc, char* argv[]) {
	char pagesToErase[256] = {0};
	unsigned char sendBuffer[256 + 2], recvBuffer;
	int isGlobal = 0;
	int i;
	int erasePageCount;
	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "global") == 0) {
			isGlobal = 1;
			break;
		} else {
			/* DFA that accepts strings like "123" or "12-34" */
			int status = 0;
			int j;
			for (j = 0; argv[i][j] != '\0'; j++) {
				switch (status) {
					case 0:
						if (isdigit((unsigned char)argv[i][j])) status = 1;
						else status = 4;
						break;
					case 1:
						if (isdigit((unsigned char)argv[i][j])) status = 1;
						else if (argv[i][j] == '-') status = 2;
						else status = 4;
						break;
					case 2:
						if (isdigit((unsigned char)argv[i][j])) status = 3;
						else status = 4;
						break;
					case 3:
						if (isdigit((unsigned char)argv[i][j])) status = 3;
						else status = 4;
						break;
				}
			}
			if (status == 1) {
				char* end;
				unsigned long no;
				errno = 0;
				no = strtoul(argv[i], &end, 10);
				if (errno == ERANGE || no > 0xff) return errorRet(1, "invalid page number\n");
				pagesToErase[no] = 1;
			} else if (status == 3) {
				char* end;
				unsigned long noStart, noEnd, no;
				errno = 0;
				noStart = strtoul(argv[i], &end, 10);
				if (errno == ERANGE) return errorRet(1, "invalid page range\n");
				noEnd = strtoul(end + 1, &end, 10);
				if (errno == ERANGE || noEnd < noStart || noEnd > 0xff) return errorRet(1, "invalid page range\n");
				for (no = noStart; no <= noEnd; no++) pagesToErase[no] = 1;
			} else {
				return errorRet(1, "invalid argument: %s\n", argv[i]);
			}
		}
	}
	erasePageCount = 0;
	for (i = 0; i < 256; i++) {
		if (pagesToErase[i]) {
			sendBuffer[++erasePageCount] = (unsigned char)i;
		}
	}
	if (!isGlobal && erasePageCount == 0) {
		return errorRet(1, "no page to erase\n");
	}
	if (serialSend(port, "\x43\xbc", 2, 1) != 2) return errorRet(1, "failed to send\n");
	if (serialRecv(port, &recvBuffer, 1, 1) != 1) return errorRet(1, "failed to receive\n");
	if (recvBuffer != ACK) return errorRet(1, "what is returned is not ACK\n");
	if (isGlobal) {
		sendBuffer[0] = 0xff;
		sendBuffer[1] = 0x00;
		if (serialSend(port, sendBuffer, 2, 1) != 2) return errorRet(1, "failed to send\n");
	} else {
		unsigned char checksum = 0;
		sendBuffer[0] = (unsigned char)(erasePageCount - 1);
		for (i = 0; i <= erasePageCount; i++) {
			checksum ^= sendBuffer[i];
		}
		sendBuffer[erasePageCount + 1] = checksum;
		if (serialSend(port, sendBuffer, erasePageCount + 2, 1) != erasePageCount + 2) return errorRet(1, "failed to send\n");
	}
	if (serialRecv(port, &recvBuffer, 1, 1) != 1) return errorRet(1, "failed to receive\n");
	if (recvBuffer != ACK) return errorRet(1, "what is returned is not ACK\n");
	return 0;
}

int main(int argc, char* argv[]) {
	SerialPort* port;
	int ret = 0;
	if (argc < 3) {
		fprintf(stderr, "Usage: %s port command\n\n", argc > 0 ? argv[0] : "gdrw");
		fprintf(stderr, "command:\n");
		fprintf(stderr, "  init\n");
		fprintf(stderr, "  read <start_address> <length> <out_file>\n");
		fprintf(stderr, "  erase global\n");
		fprintf(stderr, "  erase <page number> [<page number> ...]\n");
		return 1;
	}
	port = serialInit(argv[1], 57600, 0, 0, PALITY_EVEN);
	if (port == NULL) {
		fprintf(stderr, "failed to open serial port\n");
		return 1;
	}
	if (strcmp(argv[2], "init") == 0) {
		ret = initCommand(port, argc - 3, argv + 3);
	} else if (strcmp(argv[2], "read") == 0) {
		ret = readCommand(port, argc - 3, argv + 3);
	} else if (strcmp(argv[2], "erase") == 0) {
		ret = eraseCommand(port, argc - 3, argv + 3);
	} else {
		fprintf(stderr, "unknown command\n");
		ret = 1;
	}
	serialEnd(port);
	return ret;
}
