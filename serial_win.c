#include <Windows.h>
#include <stdlib.h>
#include "serial.h"

struct SerialPort {
	HANDLE hPort;
};

SerialPort* serialInit(const char* name, int speed, int xonChar, int xoffChar, int pality) {
	SerialPort* port;
	DCB portConfig = {0};
	BYTE palityAPIValue;
	if (name == NULL || speed <= 0) return NULL;
	switch (pality) {
		case PALITY_NONE: palityAPIValue = NOPARITY; break;
		case PALITY_EVEN: palityAPIValue = EVENPARITY; break;
		case PALITY_ODD: palityAPIValue = ODDPARITY; break;
		case PALITY_FIX_0: palityAPIValue = SPACEPARITY; break;
		case PALITY_FIX_1: palityAPIValue = MARKPARITY; break;
		default: return NULL;
	}
	port = malloc(sizeof(*port));
	if (port == NULL) return NULL;
	port->hPort = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (port->hPort == INVALID_HANDLE_VALUE) {
		free(port);
		return NULL;
	}
	portConfig.DCBlength = sizeof(portConfig);
	portConfig.BaudRate = speed;
	portConfig.fBinary = TRUE;
	if (palityAPIValue != NOPARITY) portConfig.fParity = TRUE;
	portConfig.ByteSize = 8;
	portConfig.Parity = palityAPIValue;
	portConfig.StopBits = ONESTOPBIT;
	if (xonChar != xoffChar) {
		portConfig.fOutX = TRUE;
		portConfig.fInX = TRUE;
		portConfig.XonChar = (char)xonChar;
		portConfig.XoffChar = (char)xoffChar;
	}
	if (!SetCommState(port->hPort, &portConfig)) {
		CloseHandle(port->hPort);
		free(port);
		return NULL;
	}
	return port;
}

int serialSend(SerialPort* port, const void* data, int dataSize, int force) {
	const char* dataPtr = data;
	int dataSizeSent = 0;
	if (port == NULL || data == NULL || dataSize < 0) return -1;
	if (dataSize == 0) return 0;
	do {
		DWORD sizeWritten;
		if (!WriteFile(port->hPort, dataPtr, dataSize - dataSizeSent, &sizeWritten, NULL)) {
			return -1;
		}
		dataSizeSent += sizeWritten;
	} while (force && dataSizeSent < dataSize);
	return dataSizeSent;
}

int serialRecv(SerialPort* port, void* data, int dataSizeMax, int force) {
	char* dataPtr = data;
	int dataSizeReceived = 0;
	if (port == NULL || data == NULL || dataSizeMax < 0) return -1;
	if (dataSizeMax == 0) return 0;
	do {
		DWORD sizeRead;
		if (!ReadFile(port->hPort, dataPtr, dataSizeMax - dataSizeReceived, &sizeRead, NULL)) {
			return -1;
		}
		dataSizeReceived += sizeRead;
	} while (force && dataSizeReceived < dataSizeMax);
	return dataSizeReceived;
}

void serialEnd(SerialPort* port) {
	if (port != NULL) {
		CloseHandle(port->hPort);
		free(port);
	}
}
