#include "stdafx.h"
#include "PipeClient.h"
#include "CamWindow.h"

enum PipePackageIDs
{
	PipePackage_StartStream,
	PipePackage_StreamImage,
	PipePackage_StopStream,
	PipePackage_Log,
};

void SendCloseHostCommand(DWORD timeout)
{
	HANDLE hCloseHostPipe = CreateNamedPipe(TEXT("\\\\.\\pipe\\wmrcam_doclose"),
		PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES, 1024, 128, 0, NULL);
	if (hCloseHostPipe != INVALID_HANDLE_VALUE)
	{
		OVERLAPPED connectOverlapped = {};
		connectOverlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		bool connected = false;
		if (!ConnectNamedPipe(hCloseHostPipe, &connectOverlapped))
		{
			switch (GetLastError())
			{
			case ERROR_PIPE_CONNECTED:
				connected = true;
				break;
			case ERROR_IO_PENDING:
				if (WaitForSingleObject(connectOverlapped.hEvent, timeout) == WAIT_OBJECT_0)
				{
					DWORD dwTransferred;
					connected = GetOverlappedResult(hCloseHostPipe, &connectOverlapped, &dwTransferred, FALSE) != 0;
				}
				else
					CancelIo(hCloseHostPipe);
				break;
			}
		}
		else
			connected = true;
		CloseHandle(connectOverlapped.hEvent);
		if (connected)
		{
			DisconnectNamedPipe(hCloseHostPipe);
			printf("The camera host should shut down now.\n");
		}
		else
			printf("WARNING: Couldn't reach the camera host!\n");
		CloseHandle(hCloseHostPipe);
	}
	else
		printf("ERROR: Can't create the named pipe!\n");
}

void RunPipeClient()
{
	BYTE *data; DWORD dataBufLen; DWORD dataIndex; DWORD numRead; DWORD dataLen; DWORD i;
	DWORD newDataBufLen; void *pNewData;
	LPCTSTR namedPipeName = TEXT("\\\\.\\pipe\\wmrcam");
	HANDLE hPipe;
	while (true)
	{
		printf("Connecting to pipe server... ");
		if (WaitNamedPipe(namedPipeName, NMPWAIT_WAIT_FOREVER))
		{
			hPipe = CreateFile(namedPipeName, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL); 
			if (hPipe == INVALID_HANDLE_VALUE)
			{
				Sleep(10);
				continue;
			}
			printf("Connected\n");
			dataBufLen = 1024; data = (BYTE*)malloc(dataBufLen);
			memset(data, 0, dataBufLen);
			dataIndex = 0;
			numRead = 0;
			while (ReadFile(hPipe, &data[dataIndex], dataBufLen-dataIndex, &numRead, NULL))
			{
				if (numRead > 0)
				{
					dataIndex += numRead;
					while (dataIndex >= 4 && dataIndex >= *(DWORD*)(&data[0]))
					{
						dataLen = *(DWORD*)(&data[0]);
						if (dataLen >= 5)
						{
							switch (data[4])
							{
							case PipePackage_StartStream:
								if (dataLen >= 12)
								{
									WORD id = *(WORD*)(&data[5]);
									unsigned char count = data[7];
									unsigned int sizeX = *(unsigned short*)(&data[8]);
									unsigned int sizeY = *(unsigned short*)(&data[10]);
									printf("Stream %u started : %u times %ux%u.\n", (unsigned int)id, (unsigned int)count, sizeX, sizeY);
								}
								break;
							case PipePackage_StopStream:
								if (dataLen >= 7)
								{
									WORD id = *(WORD*)(&data[5]);
									CamWindow_OnCloseCam(id);
									printf("Stream %u stopped.\n", (unsigned int)id);
								}
								break;
							case PipePackage_StreamImage:
								if (dataLen >= 12)
								{
									WORD id = *(WORD*)(&data[5]);
									unsigned char count = data[7];
									unsigned int sizeX = *(unsigned short*)(&data[8]);
									unsigned int sizeY = *(unsigned short*)(&data[10]);
									if (count == 2 && sizeX == 640 && sizeY == 480)
									{
										if (dataLen >= (12 + (size_t)count * (size_t)sizeX * (size_t)sizeY))
										{
											CamWindow_OnData(id, count, sizeX, sizeY, &data[12]);
										}
									}
									else
										printf("Unsupported image in stream %u : %u times %ux%u.\n", (unsigned int)id, (unsigned int)count, sizeX, sizeY);
								}
								break;
							case PipePackage_Log:
								if (dataLen >= 6)
								{
									char *logTemp = new char[dataLen - 5 + 1];
									memcpy(logTemp, &data[5], (dataLen - 5) * sizeof(char));
									logTemp[dataLen - 5] = 0;
									printf("Host log : %s", logTemp);
									delete[] logTemp;
								}
								break;
							}
						}
						//CamWindow_OnData(/*dataLen - 4*/2*640*480, &data[4]);

						for (i = dataLen; i < dataIndex; i++)
						{
							data[i-dataLen] = data[i];
						}
						dataIndex -= dataLen;
						memset(&data[dataIndex], 0, dataBufLen-dataIndex);
					}
					if ((dataBufLen-dataIndex) < 128)
					{
						newDataBufLen = (dataIndex / 1024 + 1) * 1024;
						pNewData = realloc(data, newDataBufLen);
						if (pNewData != nullptr)
						{
							memset(&((BYTE*)pNewData)[dataBufLen], 0, newDataBufLen-dataBufLen);
							dataBufLen = newDataBufLen;
							data = (BYTE*)pNewData;
						}
					}
				}
			}
			free(data);
			CloseHandle(hPipe);
		}
		else
			Sleep(10);
	}
}