#ifndef __SERIAL_H
#define __SERIAL_H
#include <windows.h>
typedef HANDLE PORT;

PORT OpenPort(int idx);
void ClosePort(PORT com_port);
int SetPortBoudRate(PORT com_port, int rate);
int SetPortDataBits(PORT com_port, int bits);
int SetPortStopBits(PORT com_port, int bits);
int SetPortParity(PORT com_port, int parity);
int GetPortBoudRate(PORT com_port);
int GetPortDataBits(PORT com_port);
int GetPortStopBits(PORT com_port);
int GetPortParity(PORT com_port);
int SendData(PORT com_port, const char * data);
int ReciveData(PORT com_port, char * data,int len);
PORT serial_init(int idx, int rate, int databits, int stopbits, int parity);
int Serial_SendData(PORT com_port, const char *data, int len);
int Serial_ReciveData(PORT com_port, char *data, int len);

#endif

