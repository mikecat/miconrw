/*
multi-byte integers: little endian
CRC-16: same as one used in XMODEM

* requests
  * pin information
    0x20
    * response will be ACK-DATA
      * data: [length: 1-byte] [info]
        length: number of bytes of "info" - 1
  * reset target
    0x21
    * response will be ACK
  * put target to programming mode
    0x22
    * response will be ACK
  * put target to execution mode
    0x23
    * response will be ACK
  * bulk erase
    0x30 [option: 2-byte] [crc: 2-byte]
    option: datafor 3C0005h:3C0004h
    crc: CRC-16 of 0x30 and option
    * response will be ACK or NACK
  * read memory
    [type: 1-byte] [length: 1-byte] [address: 3-byte] [crc: 2-byte]
    type:
      * 0x40: Device ID
      * 0x41-0x47: program flash
      * 0x48: configuration
      * 0x49: data EEPROM
      * 0x4a: ID
      * 0x4b-0x4f: reserved (read parameters and send NACK)
    length: number of bytes to read - 1
    address: address to start reading (start from zero in each space)
    crc: CRC-16 of type to address
    * response will be ACK-DATA or NACK
  * write memory
    [type: 1-byte] [length: 1-byte] [address: 3-byte] [data] [crc: 2-byte]
    type:
      * 0x50: reserved (read parameters and send NACK)
      * 0x51-0x57: program flash (Write Buffer Size = 1 << (lower nibble) Bytes)
      * 0x58: configuration
      * 0x59: data EEPROM
      * 0x5a: ID
      * 0x5b-0x5f: reserved (read parameters and send NACK)
    length: number of bytes to write (length of "data") - 1
    address: address to start writing (start from zero in each space)
    crc: CRC-16 of type to data
    * response will be ACK or NACK
  * direct write command
    [delay: 1-byte] [command: 1-byte] [payload: 2-byte] [crc: 2-byte]
    delay: 0x60 + (length of 4th clock [ms] (0-15))
    command: 4-Bit Command as lower nibble (upper nibble is reserved, should be 0)
    payload: payload to output
    crc: CRC-16 of delay to payload
    * response will be ACK or NACK
  * direct read command
    [delay: 1-byte] [command: 1-byte] [payload: 1-byte] [crc: 2-byte]
    delay: 0x70 + (length of 4th clock [ms] (0-15))
    command: 4-Bit Command as lower nibble (upper nibble is reserved, should be 0)
    payload: payload to output
    crc: CRC-16 of delay to payload
    * response will be ACK-DATA (with 1-byte data) or NACK
  * invalid command
    (value not defined as the first byte of commands)
    * response will be NACK

* responces
  * ACK (command executed without detecting error)
    0x20
  * ACK-DATA
    0x21 [data: requested length] [crc: 2-byte]
    data: data read
    crc: CRC-16 of 0x21 and data
  * NACK (command refused or error detected)
    0x30

*/

#include <avr/pgmspace.h>

const int PGC_pin = 5;
const int PGD_pin = 6;
const int MCLR_pin = 7;
const int PGM_pin = 8;

const char pinInformationString[] PROGMEM =
  "programmer     target\n"
  "       D5 ----> PGC\n"
  "       D6 <---> PGD\n"
  "       D7 ----> MCLR\n"
  "       D8 ----> PGM";

const uint16_t crcTable[] PROGMEM = {
  0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
  0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef
};

uint16_t updateCRC(uint16_t crc, uint8_t byte) {
  crc = (crc << 4) ^ pgm_read_word(&crcTable[((crc >> 12) ^ (byte >> 4)) & 0xf]);
  crc = (crc << 4) ^ pgm_read_word(&crcTable[((crc >> 12) ^ byte) & 0xf]);
  return crc;
}

uint16_t updateCRCAndSerialWrite(uint16_t crc, uint8_t byte) {
  Serial.write(byte);
  return updateCRC(crc, byte);
}

void serialWriteUint16(uint16_t crc) {
  Serial.write(crc & 0xff);
  Serial.write((crc >> 8) & 0xff);
}

// read "length" bytes and store to "buffer"
// update CRC-16 by data read and return new CRC-16 value
uint16_t serialRead(void* buffer, int length, uint16_t crc) {
  for (int i = 0; i < length; ) {
    int c = Serial.read();
    if (c >= 0) {
      ((uint8_t*)buffer)[i++] = c;
      crc = updateCRC(crc, c);
    }
  }
  return crc;
}

uint16_t serialReadUint16() {
  int c1, c2;
  do {
    c1 = Serial.read();
  } while (c1 < 0);
  do {
    c2 = Serial.read();
  } while (c2 < 0);
  return (uint16_t)c1 | ((uint16_t)c2 << 8);
}

const int ACK = 0x20;
const int ACK_DATA = 0x21;
const int NACK = 0x30;

bool prevIsRead;

// delayTime > 0 : delay *before* clearing 4th clock
// delayTime < 0 : delay *after* clearing 4th clock
int communicate(int command, uint16_t payload, int delayTime, bool isRead) {
  for (int i = 0; i < 4; i++) {
    digitalWrite(PGC_pin, HIGH);
    if (prevIsRead) {
      delayMicroseconds(1);
      pinMode(PGD_pin, OUTPUT);
      prevIsRead = false;
    }
    digitalWrite(PGD_pin, (command >> i) & 1 ? HIGH : LOW);
    delayMicroseconds(1);
    if (i == 3 && delayTime > 0) delay(delayTime);
    digitalWrite(PGC_pin, LOW);
    if (i == 3 && delayTime > 0) delayMicroseconds(100);
    delayMicroseconds(1);
  }
  if (delayTime < 0) delay(-delayTime);
  const int outBits = isRead ? 8 : 16;
  for (int i = 0; i < outBits; i++) {
    digitalWrite(PGC_pin, HIGH);
    digitalWrite(PGD_pin, (payload >> i) & 1 ? HIGH : LOW);
    delayMicroseconds(1);
    digitalWrite(PGC_pin, LOW);
    delayMicroseconds(1);
  }
  if (isRead) {
    int readValue = 0;
    pinMode(PGD_pin, INPUT);
    for (int i = 0; i < 8; i++) {
      digitalWrite(PGC_pin, HIGH);
      delayMicroseconds(1);
      if (digitalRead(PGD_pin) != LOW) readValue |= 1 << i;
      digitalWrite(PGC_pin, LOW);
      delayMicroseconds(1);
    }
    prevIsRead = true;
    return readValue;
  } else {
    return 0;
  }
}

void setTBLPTR(long address) {
  communicate(0x0, 0x0e00 | ((address >> 16) & 0xff), 0, false); // MOVLW xx
  communicate(0x0, 0x6ef8, 0, false); // MOVWF TBLPTRU
  communicate(0x0, 0x0e00 | ((address >> 8) & 0xff), 0, false); // MOVLW xx
  communicate(0x0, 0x6ef7, 0, false); // MOVWF TBLPTRH
  communicate(0x0, 0x0e00 | (address & 0xff), 0, false); // MOVLW xx
  communicate(0x0, 0x6ef6, 0, false); // MOVWF TBLPTRL
}

void setup() {
  pinMode(PGC_pin, OUTPUT);
  pinMode(PGD_pin, OUTPUT);
  pinMode(MCLR_pin, OUTPUT);
  pinMode(PGM_pin, OUTPUT);
  digitalWrite(PGC_pin, LOW);
  digitalWrite(PGD_pin, LOW);
  digitalWrite(MCLR_pin, LOW);
  digitalWrite(PGM_pin, LOW);
  prevIsRead = false;
  Serial.begin(115200);
}

uint8_t ioBuffer[300];

void pinInformation() {
  uint16_t crc = updateCRCAndSerialWrite(0, ACK_DATA);
  // exclude terminating null-character
  int length = sizeof(pinInformationString) / sizeof(*pinInformationString) - 1;
  crc = updateCRCAndSerialWrite(crc, length - 1);
  for (int i = 0; i < length; i++) {
    crc = updateCRCAndSerialWrite(crc, pgm_read_byte(&pinInformationString[i]));
  }
  serialWriteUint16(crc);
}

void resetTarget() {
  digitalWrite(MCLR_pin, LOW);
  digitalWrite(PGM_pin, LOW);
  delayMicroseconds(6);
  pinMode(PGD_pin, OUTPUT);
  prevIsRead = false;
  Serial.write(ACK);
}

void putTargetToProgrammingMode() {
  digitalWrite(MCLR_pin, LOW);
  delayMicroseconds(6);
  pinMode(PGD_pin, OUTPUT);
  prevIsRead = false;
  digitalWrite(PGM_pin, HIGH);
  delayMicroseconds(3);
  digitalWrite(MCLR_pin, HIGH);
  delayMicroseconds(71);
  Serial.write(ACK);
}

void putTargetToExecutionMode() {
  digitalWrite(MCLR_pin, LOW);
  delayMicroseconds(6);
  pinMode(PGD_pin, OUTPUT);
  prevIsRead = false;
  digitalWrite(PGM_pin, LOW);
  delayMicroseconds(3);
  digitalWrite(MCLR_pin, HIGH);
  Serial.write(ACK);
}

void bulkErase() {
  uint16_t crcCalculated = updateCRC(0, 0x30);
  crcCalculated = serialRead(ioBuffer, 2, crcCalculated);
  uint16_t crcReceived = serialReadUint16();
  if (crcCalculated != crcReceived) {
    Serial.write(NACK);
    return;
  }
  setTBLPTR(0x3c0005L);
  communicate(0xc, ioBuffer[1] | ((uint16_t)ioBuffer[1] << 8), 0, false);
  setTBLPTR(0x3c0004L);
  communicate(0xc, ioBuffer[0] | ((uint16_t)ioBuffer[0] << 8), 0, false);
  communicate(0x0, 0x0000, 0, false); // NOP
  communicate(0x0, 0x0000, -6, false); // delay while erase complete
  Serial.write(ACK);
}

void readMemory(int type) {
  uint16_t crcCalculated = updateCRC(0, 0x40 | (type & 0xf));
  int length;
  do {
    length = Serial.read();
  } while (length < 0);
  crcCalculated = updateCRC(crcCalculated, length);
  length++;
  crcCalculated = serialRead(ioBuffer, 3, crcCalculated);
  uint16_t crcReceived = serialReadUint16();
  if (crcCalculated != crcReceived) {
    Serial.write(NACK);
    return;
  }
  long address = ioBuffer[0] | ((long)ioBuffer[1] << 8) | ((long)ioBuffer[2] << 16);
  if (type == 9) { // data EEPROM
    if (address + length > 0x10000L) {
      Serial.write(NACK);
      return;
    }
    communicate(0x0, 0x9ea6, 0, false); // BCF EECOON1, EEPGD
    communicate(0x0, 0x9ca6, 0, false); // BCF EECON1, CFGS
    Serial.write(ACK_DATA);
    uint16_t crcSend = updateCRC(0, ACK_DATA);
    for (int i = 0; i < length; i++) {
      communicate(0x0, 0x0e00 | ((address + i) & 0xff), 0, false); // MOVLW xx
      communicate(0x0, 0x6ea9, 0, false); // MOVWF EEADR
      communicate(0x0, 0x0e00 | (((address + i) >> 8) & 0xff), 0, false); // MOVLW xx
      communicate(0x0, 0x6eaa, 0, false); // MOVWF EEADRH
      communicate(0x0, 0x80a6, 0, false); // BSF EECON1, RD
      communicate(0x0, 0x50a8, 0, false); // MOVF EEDATA, W, 0
      communicate(0x0, 0x6ef5, 0, false); // MOVWF TABLAT
      communicate(0x0, 0x0000, 0, false); // NOP
      int c = communicate(0x2, 0, 0, true);
      crcSend = updateCRC(crcSend, c);
      Serial.write(c);
    }
    serialWriteUint16(crcSend);
  } else if (type < 0xb) { // Device ID / program flash / configuration / ID
    long addressOffset;
    if (type == 0x0) { // Device ID
      addressOffset = 0x3ffffeL;
      if (address + length > 2) {
        Serial.write(NACK);
        return;
      }
    } else if (type == 0x8) { // configuration
      addressOffset = 0x300000L;
      if (address + length > 0x10) {
        Serial.write(NACK);
        return;
      }
    } else if (type == 0xa) { // ID
      addressOffset = 0x200000L;
      if (address + length > 8) {
        Serial.write(NACK);
        return;
      }
    } else { // program flash
      addressOffset = 0;
      if (address + length > 0x100000L) {
        Serial.write(NACK);
        return;
      }
    }
    setTBLPTR(addressOffset + address);
    Serial.write(ACK_DATA);
    uint16_t crcSend = updateCRC(0, ACK_DATA);
    for (int i = 0; i < length; i++) {
      int c = communicate(0x9, 0, 0, true);
      crcSend = updateCRC(crcSend, c);
      Serial.write(c);
    }
    serialWriteUint16(crcSend);
  } else {
    Serial.write(NACK);
  }
}

void writeMemory(int type) {
  uint16_t crcCalculated = updateCRC(0, 0x50 | (type & 0xf));
  int length;
  do {
    length = Serial.read();
  } while (length < 0);
  crcCalculated = updateCRC(crcCalculated, length);
  length++;
  crcCalculated = serialRead(ioBuffer, 3 + length, crcCalculated);
  uint16_t crcReceived = serialReadUint16();
  if (crcCalculated != crcReceived) {
    Serial.write(NACK);
    return;
  }
  long address = ioBuffer[0] | ((long)ioBuffer[1] << 8) | ((long)ioBuffer[2] << 16);
  if ((0x1 <= type && type <= 0x7) || type == 0xa) { // program flash / ID
    long addressOffset;
    int bufferSize;
    if(type == 0xa) { // ID
      addressOffset = 0x200000L;
      bufferSize = 8;
    } else {
      addressOffset = 0;
      bufferSize = 1 << type;
    }
    uint8_t* dataToWrite = &ioBuffer[3];
    if (address % 2 != 0) {
      dataToWrite--;
      *dataToWrite = 0xff;
      length++;
    }
    if (length % 2 != 0) {
      dataToWrite[length] = 0xff;
      length++;
    }
    if (address + length > (type == 0xa ? 8 : 0x100000L)) {
      Serial.write(NACK);
      return;
    }
    communicate(0x0, 0x8ea6, 0, false); // BSF EECON1, EEPGD
    communicate(0x0, 0x9ca6, 0, false); // BCF EECON1, CFGS
    communicate(0x0, 0x84a6, 0, false); // BSF EECON1, WREN
    for (int i = 0; i < length; i += bufferSize) {
      setTBLPTR(addressOffset + address + i);
      for (int j = 0; j < bufferSize && i + j < length; j += 2) {
        bool isLastIteration = !(j + 2 < bufferSize && i + j + 2 < length);
        communicate(isLastIteration ? 0xf : 0xd,
          dataToWrite[i + j] | ((uint16_t)dataToWrite[i + j + 1] << 8), 0, false);
      }
      communicate(0x0, 0, 1, false);
    }
    communicate(0x0, 0x94a6, 0, false); // BCF EECON1, WREN
  } else if (type == 0x8) { // configuration
    if (address + length > 0x10) {
      Serial.write(NACK);
      return;
    }
    communicate(0x0, 0x8ea6, 0, false); // BSF EECON1, EEPGD
    communicate(0x0, 0x8ca6, 0, false); // BSF EECON1, CFGS
    communicate(0x0, 0x84a6, 0, false); // BSF EECON1, WREN
    int WRTC_idx = -1;
    for (int i = 0; i < length; i++) {
      if (address + i == 0xb) { // delay programming of WRTC to last
        WRTC_idx = i;
        continue;
      }
      setTBLPTR(0x300000L + address + i);
      uint16_t data = ioBuffer[3 + i];
      communicate(0xf, data | (data << 8), 0, false);
      communicate(0x0, 0, 5, false);
    }
    if (WRTC_idx >= 0) {
      setTBLPTR(0x300000L + address + WRTC_idx);
      uint16_t data = ioBuffer[3 + WRTC_idx];
      communicate(0xf, data | (data << 8), 0, false);
      communicate(0x0, 0, 5, false);
    }
    communicate(0x0, 0x94a6, 0, false); // BCF EECON1, WREN
  } else if (type == 0x9) { // data EEPROM
    if (address + length > 0x10000L) {
      Serial.write(NACK);
      return;
    }
    communicate(0x0, 0x9ea6, 0, false); // BCF EECON1, EEPGD
    communicate(0x0, 0x9ca6, 0, false); // BCF EECON1, CFGS
    communicate(0x0, 0x84a6, 0, false); // BSF EECON1, WREN
    for (int i = 0; i < length; i++) {
      // set address
      uint16_t addr = address + i;
      communicate(0x0, 0x0e00 | (addr & 0xff), 0, false); // MOVLW xx
      communicate(0x0, 0x6ea9, 0, false); // MOVWF EEADR
      communicate(0x0, 0x0e00 | ((addr >> 8) & 0xff), 0, false); // MOVLW xx
      communicate(0x0, 0x6eaa, 0, false); // MOVWF EEADRH
      // set data
      communicate(0x0, 0x0e00 | ioBuffer[3 + i], 0, false); // MOVLW xx
      communicate(0x0, 0x6ea8, 0, false); // MOVWF EEDATA
      // start writing
      communicate(0x0, 0x82a6, 0, false); // BSF EECON1, WR
      communicate(0x0, 0x0000, 0, false); // NOP
      communicate(0x0, 0x0000, 0, false); // NOP
      // wait until writing done
      for (;;) {
        communicate(0x0, 0x50a6, 0, false); // MOVF EECON1, W, 0
        communicate(0x0, 0x6ef5, 0 ,false); // MOVWF TABLAT
        communicate(0x0, 0x0000, 0 ,false); // NOP
        int res = communicate(0x2, 0, 0, true);
        if (!(res & 2)) break;
      }
      // delay P10
      delayMicroseconds(100);
    }
    communicate(0x0, 0x94a6, 0, false); // BCF EECON1, WREN
  } else {
    Serial.write(NACK);
    return;
  }
  Serial.write(ACK);
}

void directWriteCommand(int delayLength) {
  uint16_t crcCalculated = updateCRC(0, 0x60 | (delayLength & 0xf));
  crcCalculated = serialRead(ioBuffer, 3, crcCalculated);
  uint16_t crcReceived = serialReadUint16();
  if (crcCalculated != crcReceived) {
    Serial.write(NACK);
    return;
  }
  communicate(ioBuffer[0], ioBuffer[1] | ((uint16_t)ioBuffer[2] << 8), delayLength, false);
  Serial.write(ACK);
}

void directReadCommand(int delayLength) {
  uint16_t crcCalculated = updateCRC(0, 0x60 | (delayLength & 0xf));
  crcCalculated = serialRead(ioBuffer, 2, crcCalculated);
  uint16_t crcReceived = serialReadUint16();
  if (crcCalculated != crcReceived) {
    Serial.write(NACK);
    return;
  }
  int res = communicate(ioBuffer[0], ioBuffer[1], delayLength, false);
  Serial.write(ACK_DATA);
  Serial.write(res);
  serialWriteUint16(updateCRC(updateCRC(0, ACK_DATA), res));
}

void loop() {
  int command = Serial.read();
  if (command < 0) return;
  if (command == 0x20) {
    pinInformation();
  } else if (command == 0x21) {
    resetTarget();
  } else if (command == 0x22) {
    putTargetToProgrammingMode();
  } else if (command == 0x23) {
    putTargetToExecutionMode();
  } else if (command == 0x30) {
    bulkErase();
  } else if ((command & 0xf0) == 0x40) {
    readMemory(command & 0xf);
  } else if ((command & 0xf0) == 0x50) {
    writeMemory(command & 0xf);
  } else if ((command & 0xf0) == 0x60) {
    directWriteCommand(command & 0xf);
  } else if ((command & 0xf0) == 0x70) {
    directReadCommand(command & 0xf);
  } else {
    Serial.write(NACK);
  }
}
