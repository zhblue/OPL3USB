#include <stdio.h>
#include <windows.h>
#include "serial.h"

PORT OpenPort(int idx)
{
	HANDLE hComm;
	TCHAR comname[100];
	wsprintf(comname, TEXT("\\\\.\\COM%d"), idx);
	hComm = CreateFile(comname,            //port name 
		GENERIC_READ | GENERIC_WRITE, //Read/Write   				 
		0,            // No Sharing                               
		NULL,         // No Security                              
		OPEN_EXISTING,// Open existing port only                     
		0,            // Non Overlapped I/O                           
		NULL);        // Null for Comm Devices
	
	if (hComm == INVALID_HANDLE_VALUE)
		return NULL;
	COMMTIMEOUTS timeouts = { 0 };
	timeouts.ReadIntervalTimeout = 50;
	timeouts.ReadTotalTimeoutConstant = 50;
	timeouts.ReadTotalTimeoutMultiplier = 10;
	timeouts.WriteTotalTimeoutConstant = 50;
	timeouts.WriteTotalTimeoutMultiplier = 10;

	if (SetCommTimeouts(hComm, &timeouts) == FALSE)
		return NULL;

	if (SetCommMask(hComm, EV_RXCHAR) == FALSE)
		return NULL;

	printf("open%d ok\n",idx);

	return hComm;
}

void ClosePort(PORT com_port)
{
	CloseHandle(com_port);
}

int SetPortBoudRate(PORT com_port, int rate)
{
	DCB dcbSerialParams = { 0 };
	BOOL Status;
	dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
	Status = GetCommState(com_port, &dcbSerialParams);
	if (Status == FALSE)
		return FALSE;
	dcbSerialParams.BaudRate = rate;
	Status = SetCommState(com_port, &dcbSerialParams);
	return Status;
}

int SetPortDataBits(PORT com_port, int bits)
{
	DCB dcbSerialParams = { 0 };
	BOOL Status;
	dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
	Status = GetCommState(com_port, &dcbSerialParams);
	if (Status == FALSE)
		return FALSE;
	dcbSerialParams.ByteSize = bits;
	Status = SetCommState(com_port, &dcbSerialParams);
	return Status;
}

int SetPortStopBits(PORT com_port, int bits)
{
	DCB dcbSerialParams = { 0 };
	BOOL Status;
	dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
	Status = GetCommState(com_port, &dcbSerialParams);
	if (Status == FALSE)
		return FALSE;
	dcbSerialParams.StopBits = bits;
	Status = SetCommState(com_port, &dcbSerialParams);
	return Status;
}

//默认为无校验。NOPARITY 0； ODDPARITY 1；EVENPARITY 2；MARKPARITY 3；SPACEPARITY 4
int SetPortParity(PORT com_port, int parity)
{
	DCB dcbSerialParams = { 0 };
	BOOL Status;
	dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
	Status = GetCommState(com_port, &dcbSerialParams);
	if (Status == FALSE)
		return FALSE;
	dcbSerialParams.Parity = parity;
	Status = SetCommState(com_port, &dcbSerialParams);
	return Status;
}

int GetPortBoudRate(PORT com_port)
{
	DCB dcbSerialParams = { 0 };
	BOOL Status;
	dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
	Status = GetCommState(com_port, &dcbSerialParams);
	if (Status == FALSE)
		return -1;
	return dcbSerialParams.BaudRate;
}

int GetPortDataBits(PORT com_port) 
{
	DCB dcbSerialParams = { 0 };
	BOOL Status;
	dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
	Status = GetCommState(com_port, &dcbSerialParams);
	if (Status == FALSE)
		return -1;
	return dcbSerialParams.ByteSize;
}

int GetPortStopBits(PORT com_port) 
{
	DCB dcbSerialParams = { 0 };
	BOOL Status;
	dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
	Status = GetCommState(com_port, &dcbSerialParams);
	if (Status == FALSE)
		return -1;
	return dcbSerialParams.StopBits;
}

int GetPortParity(PORT com_port)
{
	DCB dcbSerialParams = { 0 };
	BOOL Status;
	dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
	Status = GetCommState(com_port, &dcbSerialParams);
	if (Status == FALSE)
		return -1;
	return dcbSerialParams.Parity;
}

int SendData(PORT com_port, const char * data)
{
	DWORD  dNoOFBytestoWrite = strlen(data);
	DWORD  dNoOfBytesWritten;
	BOOL Status = WriteFile(com_port,
				data,
				dNoOFBytestoWrite,
				&dNoOfBytesWritten,
				NULL);
	if (Status == FALSE)
	{
			return -1;
	}
	else
	{
		printf("%s\n",data);
	}

	return dNoOfBytesWritten;
}

int ReciveData(PORT com_port, char * data,int len)
{
	DWORD dwEventMask;
	DWORD NoBytesRead;

	BOOL Status = WaitCommEvent(com_port, &dwEventMask, NULL);
	if (Status == FALSE) 
    {
		return FALSE;
	}
	Status = ReadFile(com_port, data, len, &NoBytesRead, NULL);
	data[NoBytesRead] = 0;

	if (Status == FALSE) 
    {
		return FALSE;
	}
	else
    {
		printf("%s\n",data);
	}

	return TRUE;
}

PORT serial_init(int idx, int rate, int databits, int stopbits, int parity)
{
    int ret = 0;
   PORT com_port;
   com_port = OpenPort(idx);
   if (com_port == INVALID_HANDLE_VALUE)
   {
       printf("open COM%d fail\n", idx);
       return NULL;
   }
   ret = SetPortBoudRate(com_port, rate);
   if(ret == FALSE)
   {
       printf("set COM%d band fail\n", idx);
       return NULL;
   }
   ret = SetPortDataBits(com_port, databits);
   if(ret == FALSE)
   {
       printf("set COM%d databits fail\n", idx);
       return NULL;
   }
  // stopbits = ONESTOPBIT;
   printf("stopbits %d\n",stopbits);
   ret = SetPortStopBits(com_port, stopbits);
   if(ret == FALSE)
   {
       printf("set COM%d stopbits fail\n", idx);
       return NULL;
   }
   ret = SetPortParity(com_port, parity);
   if(ret == FALSE)
   {
       printf("set COM%d parity fail\n", idx);
       return NULL;
   }

   return com_port;
}

int Serial_SendData(PORT com_port, const char *data, int len)
{
    DWORD  dNoOfBytesWritten;
    BOOL Status = WriteFile(com_port,
				data,
				len,
				&dNoOfBytesWritten,
				NULL);

	if (Status == FALSE)
		return -1;
	else
		//printf("send ok\n");

    return 0;
}

int Serial_ReciveData(PORT com_port, char * data, int len)
{
	DWORD dwEventMask;
	DWORD NoBytesRead;

	BOOL Status = WaitCommEvent(com_port, &dwEventMask, NULL);
	if (Status == FALSE) 
    {
		return -1;
	}
	Status = ReadFile(com_port, data, len, &NoBytesRead, NULL);
	data[NoBytesRead] = 0;

	if (Status == FALSE) 
		return -1;
	else
		printf("rcv ok\n");

	return NoBytesRead;
}

