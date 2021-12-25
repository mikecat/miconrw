#ifndef SERIAL_H_GUARD_2815A4D4_8AD6_4FA7_B6A0_EC3DDDCB3939
#define SERIAL_H_GUARD_2815A4D4_8AD6_4FA7_B6A0_EC3DDDCB3939

#include <stddef.h>

typedef struct SerialPort SerialPort;

enum {
	PALITY_NONE = 0,
	PALITY_EVEN,
	PALITY_ODD,
	PALITY_FIX_0,
	PALITY_FIX_1
};

/* シリアルポートを初期化し、ハンドルを返す。失敗したらNULLを返す。 */
SerialPort* serialInit(const char* name, int speed, int xonChar, int xoffChar, int pality);

/*
dataSizeバイト送信しようとする。
forceが真(非零)の場合、dataSizeバイトの送信を完了するか失敗するまで送信を続ける。
成功したら送信したバイト数を返す。失敗したら -1 を返す。
*/
int serialSend(SerialPort* port, const void* data, int dataSize, int force);

/*
dataSizeMaxバイト受信しようとする。
forceが真(非零)の場合、dataSizeMaxバイトの受信を完了するか失敗するまで受信を続ける。
成功したら受信したバイト数を返す。失敗したら -1 を返す。
*/
int serialRecv(SerialPort* port, void* data, int dataSizeMax, int force);

/* シリアルポートの使用を終了する。 */
void serialEnd(SerialPort* port);

#endif
