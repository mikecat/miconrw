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
	unsigned char pagesToErase[0x10000 / 8] = {0};
	unsigned char sendBuffer[3], recvBuffer;
	int isGlobal = 0;
	int i;
	int erasePageCount, highPageEraseRequested = 0;
	int getDataSize, eraseSupported = 0, extendedEraseSupported = 0;
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
				if (errno == ERANGE || no > 0xffff) return errorRet(1, "invalid page number\n");
				pagesToErase[no / 8] |= 1 << (no % 8);
			} else if (status == 3) {
				char* end;
				unsigned long noStart, noEnd, no;
				errno = 0;
				noStart = strtoul(argv[i], &end, 10);
				if (errno == ERANGE) return errorRet(1, "invalid page range\n");
				noEnd = strtoul(end + 1, &end, 10);
				if (errno == ERANGE || noEnd < noStart || noEnd > 0xffff) return errorRet(1, "invalid page range\n");
				for (no = noStart; no <= noEnd; no++) {
					pagesToErase[no / 8] |= 1 << (no % 8);
				}
			} else {
				return errorRet(1, "invalid argument: %s\n", argv[i]);
			}
		}
	}
	erasePageCount = 0;
	for (i = 0; i < 0x10000; i++) {
		if ((pagesToErase[i / 8] >> (i % 8)) & 1) {
			erasePageCount++;
			if (i >= 0x100) highPageEraseRequested = 1;
		}
	}
	if (!isGlobal && erasePageCount == 0) {
		return errorRet(1, "no page to erase\n");
	}

	/* Get command to check (Extended) Erase Memory command is supported */
	if (serialSend(port, "\x00\xff", 2, 1) != 2) return errorRet(1, "failed to send\n");
	if (serialRecv(port, &recvBuffer, 1, 1) != 1) return errorRet(1, "failed to receive\n");
	if (recvBuffer != ACK) return errorRet(1, "what is returned is not ACK\n");
	if (serialRecv(port, &recvBuffer, 1, 1) != 1) return errorRet(1, "failed to receive\n");
	getDataSize = recvBuffer;
	for (i = 0; i <= getDataSize; i++) {
		if (serialRecv(port, &recvBuffer, 1, 1) != 1) return errorRet(1, "failed to receive\n");
		if (i > 0) { /* exclude the bootloader version */
			if (recvBuffer == 0x43) eraseSupported = 1;
			else if (recvBuffer == 0x44) extendedEraseSupported = 1;
		}
	}
	if (serialRecv(port, &recvBuffer, 1, 1) != 1) return errorRet(1, "failed to receive\n");
	if (recvBuffer != ACK) return errorRet(1, "what is returned is not ACK\n");

	if (extendedEraseSupported) {
		if (erasePageCount > 0xfff0) {
			return errorRet(1, "too many pages to erase\n");
		}
		if (serialSend(port, "\x44\xbb", 2, 1) != 2) return errorRet(1, "failed to send\n");
		if (serialRecv(port, &recvBuffer, 1, 1) != 1) return errorRet(1, "failed to receive\n");
		if (recvBuffer != ACK) return errorRet(1, "what is returned is not ACK\n");
		if (isGlobal) {
			sendBuffer[0] = 0xff;
			sendBuffer[1] = 0xff;
			sendBuffer[2] = 0x00;
			if (serialSend(port, sendBuffer, 3, 1) != 3) return errorRet(1, "failed to send\n");
		} else {
			unsigned char checksum;
			sendBuffer[0] = (unsigned char)((erasePageCount - 1) >> 8);
			sendBuffer[1] = (unsigned char)(erasePageCount - 1);
			checksum = sendBuffer[0] ^ sendBuffer[1];
			if (serialSend(port, sendBuffer, 2, 1) != 2) return errorRet(1, "failed to send\n");
			for (i = 0; i < 0x10000; i++) {
				if ((pagesToErase[i / 8] >> (i % 8)) & 1) {
					sendBuffer[0] = (unsigned char)(i >> 8);
					sendBuffer[1] = (unsigned char)i;
					checksum ^= sendBuffer[0] ^ sendBuffer[1];
					if (serialSend(port, sendBuffer, 2, 1) != 2) return errorRet(1, "failed to send\n");
				}
			}
			sendBuffer[0] = checksum;
			if (serialSend(port, sendBuffer, 1, 1) != 1) return errorRet(1, "failed to send\n");
		}
		if (serialRecv(port, &recvBuffer, 1, 1) != 1) return errorRet(1, "failed to receive\n");
		if (recvBuffer != ACK) return errorRet(1, "what is returned is not ACK\n");
	} else if (eraseSupported) {
		if (highPageEraseRequested) {
			return errorRet(1, "erase page >= 0x100 not supported\n");
		}
		if (erasePageCount > 0xff) {
			return errorRet(1, "too many pages to erase\n");
		}
		if (serialSend(port, "\x43\xbc", 2, 1) != 2) return errorRet(1, "failed to send\n");
		if (serialRecv(port, &recvBuffer, 1, 1) != 1) return errorRet(1, "failed to receive\n");
		if (recvBuffer != ACK) return errorRet(1, "what is returned is not ACK\n");
		if (isGlobal) {
			sendBuffer[0] = 0xff;
			sendBuffer[1] = 0x00;
			if (serialSend(port, sendBuffer, 2, 1) != 2) return errorRet(1, "failed to send\n");
		} else {
			unsigned char checksum;
			sendBuffer[0] = (unsigned char)(erasePageCount - 1);
			checksum = sendBuffer[0];
			if (serialSend(port, sendBuffer, 1, 1) != 1) return errorRet(1, "failed to send\n");
			for (i = 0; i < 0x100; i++) {
				if ((pagesToErase[i / 8] >> (i % 8)) & 1) {
					sendBuffer[0] = (unsigned char)i;
					if (serialSend(port, sendBuffer, 1, 1) != 1) return errorRet(1, "failed to send\n");
					checksum ^= i;
				}
			}
			sendBuffer[0] = checksum;
			if (serialSend(port, sendBuffer, 1, 1) != 1) return errorRet(1, "failed to send\n");
		}
		if (serialRecv(port, &recvBuffer, 1, 1) != 1) return errorRet(1, "failed to receive\n");
		if (recvBuffer != ACK) return errorRet(1, "what is returned is not ACK\n");
	} else {
		return errorRet(1, "erase is not supported\n");
	}
	return 0;
}

int writeCommand(SerialPort* port, int argc, char* argv[]) {
	unsigned long address;
	FILE* fpIn;
	char* end;
	if (argc < 2) return errorRet(1, "required argument(s) not given\n");
	errno = 0;
	address = strtoul(argv[0], &end, 0);
	if (argv[0][0] == '\0' || *end != '\0' || errno == ERANGE) return errorRet(1, "invalid address\n");
	fpIn = fopen(argv[1], "rb");
	if (fpIn == NULL) return errorRet(1, "failed to open input file\n");
	for (;;) {
		unsigned char commBuffer[8], fileBuffer[256];
		size_t sizeRead, i;
		sizeRead = fread(fileBuffer, 1, 256, fpIn);
		while (sizeRead < 256) {
			size_t sizeRead2;
			if (ferror(fpIn)) return errorCloseRet(1, fpIn, "failed to read file at write address 0x%lx\n", address);
			if (feof(fpIn)) break;
			sizeRead2 = fread(fileBuffer + sizeRead, 1, 256 - sizeRead, fpIn);
			sizeRead += sizeRead2;
		}
		if (sizeRead == 0) break;
		while (sizeRead % 4 != 0) {
			fileBuffer[sizeRead++] = 0xff;
		}
		if (serialSend(port, "\x31\xce", 2, 1) != 2) return errorCloseRet(1, fpIn, "command: failed to send at write address 0x%lx\n", address);
		if (serialRecv(port, commBuffer, 1, 1) != 1) return errorCloseRet(1, fpIn, "command: failed to receive at write address 0x%lx\n", address);
		if (commBuffer[0] != ACK) return errorCloseRet(1, fpIn, "command: what is returned is not ACK at write address 0x%lx\n", address);
		commBuffer[0] = (unsigned char)(address >> 24);
		commBuffer[1] = (unsigned char)(address >> 16);
		commBuffer[2] = (unsigned char)(address >> 8);
		commBuffer[3] = (unsigned char)(address);
		commBuffer[4] = commBuffer[0] ^ commBuffer[1] ^ commBuffer[2] ^ commBuffer[3];
		if (serialSend(port, commBuffer, 5, 1) != 5) return errorCloseRet(1, fpIn, "address: failed to send at write address 0x%lx\n", address);
		if (serialRecv(port, commBuffer, 1, 1) != 1) return errorCloseRet(1, fpIn, "address: failed to receive at write address 0x%lx\n", address);
		if (commBuffer[0] != ACK) return errorCloseRet(1, fpIn, "address: what is returned is not ACK at write address 0x%lx\n", address);
		commBuffer[0] = (unsigned char)(sizeRead - 1);
		if (serialSend(port, commBuffer, 1, 1) != 1) return errorCloseRet(1, fpIn, "data: failed to send at write address 0x%lx\n", address);
		if (serialSend(port, fileBuffer, sizeRead, 1) != (int)sizeRead) return errorCloseRet(1, fpIn, "data: failed to send at write address 0x%lx\n", address);
		for (i = 0; i < sizeRead; i++) {
			commBuffer[0] ^= fileBuffer[i];
		}
		if (serialSend(port, commBuffer, 1, 1) != 1) return errorCloseRet(1, fpIn, "data: failed to send at write address 0x%lx\n", address);
		if (serialRecv(port, commBuffer, 1, 1) != 1) return errorCloseRet(1, fpIn, "data: failed to receive at write address 0x%lx\n", address);
		if (commBuffer[0] != ACK) return errorCloseRet(1, fpIn, "data: what is returned is not ACK at write address 0x%lx\n", address);
		address += sizeRead;
	}
	fclose(fpIn);
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
		fprintf(stderr, "  write <start_address> <in_file>\n");
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
	} else if (strcmp(argv[2], "write") == 0) {
		ret = writeCommand(port, argc - 3, argv + 3);
	} else {
		fprintf(stderr, "unknown command\n");
		ret = 1;
	}
	serialEnd(port);
	return ret;
}
