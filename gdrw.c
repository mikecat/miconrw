#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

int main(int argc, char* argv[]) {
	SerialPort* port;
	int ret = 0;
	if (argc < 3) {
		fprintf(stderr, "Usage: %s port command\n\n", argc > 0 ? argv[0] : "gdrw");
		fprintf(stderr, "command:\n");
		fprintf(stderr, "  init\n");
		fprintf(stderr, "  read <start_address> <length> <out_file>\n");
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
	} else {
		fprintf(stderr, "unknown command\n");
		ret = 1;
	}
	serialEnd(port);
	return ret;
}
