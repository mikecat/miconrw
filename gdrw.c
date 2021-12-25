#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "serial.h"

#define ACK 0x79
#define NACK 0x1f

int initCommand(SerialPort* port, int argc, char* argv[]) {
	char outBuf, inBuf;
	(void)argc;
	(void)argv;
	outBuf = 0x7f;
	if (serialSend(port, &outBuf, 1, 1) != 1) {
		fprintf(stderr, "failed to send\n");
		return 1;
	}
	if (serialRecv(port, &inBuf, 1, 1) != 1) {
		fprintf(stderr, "failed to receive\n");
		return 1;
	}
	if (inBuf != ACK) {
		fprintf(stderr, "what is returned is not ACK\n");
		return 1;
	}
	return 0;
}

int readCommand(SerialPort* port, int argc, char* argv[]) {
	unsigned long address;
	unsigned long length;
	FILE* fpOut;
	char* end;
	unsigned char buffer[256];
	if (argc < 3) {
		fprintf(stderr, "required argument(s) not given\n");
		return 1;
	}
	errno = 0;
	address = strtoul(argv[0], &end, 0);
	if (argv[0][0] == '\0' || *end != '\0' || errno == ERANGE) {
		fprintf(stderr, "invalid address\n");
		return 1;
	}
	length = strtoul(argv[1], &end, 0);
	if (argv[1][0] == '\0' || *end != '\0' || errno == ERANGE) {
		fprintf(stderr, "invalid length\n");
		return 1;
	}
	fpOut = fopen(argv[2], "wb");
	if (fpOut == NULL) {
		fprintf(stderr, "failed to open output file\n");
		return 1;
	}
	while (length > 0) {
		int lengthToRead = length > 256 ? 256 : (int)length;
		if (serialSend(port, "\x11\xee", 2, 1) != 2) {
			fprintf(stderr, "failed to send\n");
			fclose(fpOut);
			return 1;
		}
		if (serialRecv(port, buffer, 1, 1) != 1) {
			fprintf(stderr, "failed to receive\n");
			fclose(fpOut);
			return 1;
		}
		if (buffer[0] != ACK) {
			fprintf(stderr, "what is returned is not ACK\n");
			return 1;
		}
		buffer[0] = (unsigned char)(address >> 24);
		buffer[1] = (unsigned char)(address >> 16);
		buffer[2] = (unsigned char)(address >> 8);
		buffer[3] = (unsigned char)(address);
		buffer[4] = buffer[0] ^ buffer[1] ^ buffer[2] ^ buffer[3];
		if (serialSend(port, buffer, 5, 1) != 5) {
			fprintf(stderr, "failed to send\n");
			fclose(fpOut);
			return 1;
		}
		if (serialRecv(port, buffer, 1, 1) != 1) {
			fprintf(stderr, "failed to receive\n");
			fclose(fpOut);
			return 1;
		}
		if (buffer[0] != ACK) {
			fprintf(stderr, "what is returned is not ACK\n");
			return 1;
		}
		buffer[0] = (unsigned char)(lengthToRead - 1);
		buffer[1] = ~buffer[0];
		if (serialSend(port, buffer, 2, 1) != 2) {
			fprintf(stderr, "failed to send\n");
			fclose(fpOut);
			return 1;
		}
		if (serialRecv(port, buffer, 1, 1) != 1) {
			fprintf(stderr, "failed to receive\n");
			fclose(fpOut);
			return 1;
		}
		if (buffer[0] != ACK) {
			fprintf(stderr, "what is returned is not ACK\n");
			return 1;
		}
		if (serialRecv(port, buffer, lengthToRead, 1) != lengthToRead) {
			fprintf(stderr, "failed to receive\n");
			fclose(fpOut);
			return 1;
		}
		if (fwrite(buffer, 1, lengthToRead, fpOut) != (size_t)lengthToRead) {
			fprintf(stderr, "failed to write file\n");
			fclose(fpOut);
			return 1;
		}
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
