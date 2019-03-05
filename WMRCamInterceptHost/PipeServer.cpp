#include "stdafx.h"
#include "PipeServer.h"
#include <stdlib.h>
#include <stdio.h>
#include <vector>

static HANDLE g_hServerClosedEvent;
static HANDLE g_hClosePipeEvent;

static HANDLE g_hNewPackageEvent;
static CRITICAL_SECTION g_packageQueueLock;
static std::vector<BYTE*> g_packages;
static std::vector<BYTE*> g_errorLog;

enum PipePackageIDs
{
	PipePackage_StartStream,
	PipePackage_StreamImage,
	PipePackage_StopStream,
	PipePackage_Log,
};

static bool g_started = false;
void InitializeCamServer()
{
	if (g_started)
		return;
	g_started = true;
	g_hClosePipeEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	g_hServerClosedEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
	g_hNewPackageEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
	InitializeCriticalSection(&g_packageQueueLock);
}

void CloseCamServer()
{
	if (!g_started)
		return;
	g_started = false;
	SetEvent(g_hClosePipeEvent);
	WaitForSingleObject(g_hServerClosedEvent, INFINITE);

	EnterCriticalSection(&g_packageQueueLock);
	for (int i = 0; i < g_packages.size(); i++)
		delete[] g_packages[i];
	g_packages.clear();
	for (int i = 0; i < g_errorLog.size(); i++)
		delete[] g_errorLog[i];
	g_errorLog.clear();
	LeaveCriticalSection(&g_packageQueueLock);

	CloseHandle(g_hServerClosedEvent);
	CloseHandle(g_hClosePipeEvent);
	CloseHandle(g_hNewPackageEvent);
	DeleteCriticalSection(&g_packageQueueLock);
}

class ConnectThreadData
{
public:
	HANDLE hPipe = INVALID_HANDLE_VALUE;
	CRITICAL_SECTION *syncPipeList = nullptr;
	CRITICAL_SECTION *syncClientList = nullptr;
	std::vector<HANDLE> connectedBuffer;
	size_t connectedCount = 0;
	HANDLE hPipeConnectedEvent = NULL;
	HANDLE hDoNotAcceptConnection = NULL;
};
DWORD __stdcall ConnectThread(ConnectThreadData *d)
{
	while (true)
	{
		BOOL result = ConnectNamedPipe(d->hPipe, NULL);
		DWORD lastError = GetLastError();
		EnterCriticalSection(d->syncClientList);
		if (result || (lastError == ERROR_PIPE_CONNECTED))
		{
			if (WaitForSingleObject(d->hDoNotAcceptConnection, 0) == WAIT_TIMEOUT)
			{
				d->connectedBuffer.push_back(d->hPipe);

				LeaveCriticalSection(d->syncClientList);

				std::vector<BYTE*> errorLogLocal;
				EnterCriticalSection(&g_packageQueueLock);
				errorLogLocal.swap(g_errorLog);
				LeaveCriticalSection(&g_packageQueueLock);

				for (size_t i = 0; i < errorLogLocal.size(); i++)
				{
					DWORD numBytesWritten = 0;
					WriteFile(d->hPipe, errorLogLocal[i], *(DWORD*)&(errorLogLocal[i][0]), &numBytesWritten, NULL);
				}

				EnterCriticalSection(&g_packageQueueLock);
				errorLogLocal.swap(g_errorLog);
				LeaveCriticalSection(&g_packageQueueLock);

				EnterCriticalSection(d->syncClientList);

				d->connectedCount++;
				SetEvent(d->hPipeConnectedEvent);
			}
			else
			{
				CloseHandle(d->hPipe);
			}
			LeaveCriticalSection(d->syncClientList);
			break;
		}
		else if (!result)
		{
			char dbgStrBuf[100];
			LeaveCriticalSection(d->syncClientList);
			sprintf_s(dbgStrBuf, "ConnectNamedPipe failed with %d.\r\n", GetLastError());
			OutputDebugStringA(dbgStrBuf);
		}
		else
			LeaveCriticalSection(d->syncClientList);
		if (WaitForSingleObject(d->hDoNotAcceptConnection, 0) == WAIT_OBJECT_0)
			break;
	}
	return 0;
}

//id : stream id; count : amount of images; sizeX/sizeY : size of each image;
void OnStartCameraStream(WORD id, unsigned char count, unsigned short sizeX, unsigned short sizeY)
{
	BYTE *openPackage = new BYTE[12];
	*(DWORD*)(&openPackage[0]) = 12;
	openPackage[4] = PipePackage_StartStream;
	*(WORD*)(&openPackage[5]) = id;
	openPackage[7] = count;
	*(unsigned short*)(&openPackage[8]) = sizeX;
	*(unsigned short*)(&openPackage[10]) = sizeY;
	
	EnterCriticalSection(&g_packageQueueLock);
	for (size_t i = 0; i < g_packages.size(); i++)
	{
		if (g_packages[i][4] == PipePackage_StartStream && *(WORD*)(&(g_packages[i][5])) == id)
		{
			delete[] g_packages[i];
			g_packages.erase(g_packages.begin() + i);
			i--;
		}
	}
	g_packages.push_back(openPackage);
	SetEvent(g_hNewPackageEvent);
	LeaveCriticalSection(&g_packageQueueLock);
}

//id : stream id; buf : image buffer (count images one after another); count : amount of images; sizeX/sizeY : size of each image;
void OnGetStreamImage(WORD id, const BYTE *buf, unsigned char count, unsigned short sizeX, unsigned short sizeY)
{
	DWORD packageSize = 12 + (DWORD)sizeX * (DWORD)sizeY * (DWORD)count;
	BYTE *imagePackage = new BYTE[packageSize];
	*(DWORD*)(&imagePackage[0]) = packageSize;
	imagePackage[4] = PipePackage_StreamImage;
	*(WORD*)(&imagePackage[5]) = id;
	imagePackage[7] = count;
	*(unsigned short*)(&imagePackage[8]) = sizeX;
	*(unsigned short*)(&imagePackage[10]) = sizeY;
	memcpy(&imagePackage[12], buf, (DWORD)sizeX * (DWORD)sizeY * (DWORD)count);
	
	EnterCriticalSection(&g_packageQueueLock);
	for (size_t i = 0; i < g_packages.size(); i++)
	{
		if (g_packages[i][4] == PipePackage_StreamImage && *(WORD*)(&(g_packages[i][5])) == id)
		{
			delete[] g_packages[i];
			g_packages.erase(g_packages.begin() + i);
			i--;
		}
	}
	g_packages.push_back(imagePackage);
	SetEvent(g_hNewPackageEvent);
	LeaveCriticalSection(&g_packageQueueLock);
}

//640*480
void OnGetStreamImageDefault(WORD id, const BYTE *imageBuf)
{
	DWORD packageSize = 12 + 2*640*480;
	BYTE *imagePackage = new BYTE[packageSize];
	*(DWORD*)(&imagePackage[0]) = packageSize;
	imagePackage[4] = PipePackage_StreamImage;
	*(WORD*)(&imagePackage[5]) = id;
	imagePackage[7] = 2;
	*(unsigned short*)(&imagePackage[8]) = 640;
	*(unsigned short*)(&imagePackage[10]) = 480;

	memcpy(&imagePackage[12], imageBuf, 2 * 640 * 480);
	/*for (int y = 0; y < 480; y++)
	{
		for (int img = 0; img < 2; img++)
		{
			memcpy(&imagePackage[12 + img*640*480 + 640*y], &interlacedBuf[640*(2*y+img)], 640);
		}
	}*/
	
	EnterCriticalSection(&g_packageQueueLock);
	for (size_t i = 0; i < g_packages.size(); i++)
	{
		if (g_packages[i][4] == PipePackage_StreamImage && *(WORD*)(&(g_packages[i][5])) == id)
		{
			delete[] g_packages[i];
			g_packages.erase(g_packages.begin() + i);
			i--;
		}
	}
	g_packages.push_back(imagePackage);
	SetEvent(g_hNewPackageEvent);
	LeaveCriticalSection(&g_packageQueueLock);
}

void OnStopCameraStream(WORD id)
{
	BYTE *stopPackage = new BYTE[7];
	*(DWORD*)(&stopPackage[0]) = 7;
	stopPackage[4] = PipePackage_StopStream;
	*(WORD*)(&stopPackage[5]) = id;
	
	EnterCriticalSection(&g_packageQueueLock);
	for (size_t i = 0; i < g_packages.size(); i++)
	{
		if (g_packages[i][4] == PipePackage_StopStream && *(WORD*)(&(g_packages[i][5])) == id)
		{
			delete[] g_packages[i];
			g_packages.erase(g_packages.begin() + i);
			i--;
		}
	}
	g_packages.push_back(stopPackage);
	SetEvent(g_hNewPackageEvent);
	LeaveCriticalSection(&g_packageQueueLock);
}

void OnErrorLog(const char *error)
{
	size_t errorLen = strlen(error);
	DWORD pkLen = (5 + errorLen * sizeof(char)) & (DWORD)~0;

	BYTE *errPackage = new BYTE[pkLen];
	*(DWORD*)(&errPackage[0]) = pkLen;
	errPackage[4] = PipePackage_Log;
	memcpy(&errPackage[5], error, pkLen);

	if (!g_started) //allow initialization error logs
	{
		g_errorLog.push_back(errPackage);
		return;
	}

	//create a copy for g_errorLog since delete[] will be called on errPackage sooner or later
	BYTE *errPackageCopy = new BYTE[pkLen];
	memcpy(errPackageCopy, errPackage, pkLen);

	EnterCriticalSection(&g_packageQueueLock);
	g_errorLog.push_back(errPackageCopy);

	g_packages.push_back(errPackage);
	SetEvent(g_hNewPackageEvent);
	LeaveCriticalSection(&g_packageQueueLock);

	OutputDebugStringA(error);
}

bool RunCamServer()
{
	InitializeCamServer();
	if (WaitForSingleObject(g_hServerClosedEvent, 0) == WAIT_TIMEOUT)
		return false;
	ResetEvent(g_hServerClosedEvent);
	ResetEvent(g_hClosePipeEvent);

	HANDLE hDisconnectedPipe = INVALID_HANDLE_VALUE;
	
	HANDLE hPipeConnectEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	CRITICAL_SECTION syncPipeList;
	InitializeCriticalSection(&syncPipeList);
	CRITICAL_SECTION syncClientList;
	InitializeCriticalSection(&syncClientList);

	HANDLE hConnectThread = INVALID_HANDLE_VALUE;
	ConnectThreadData tData;
	tData.hDoNotAcceptConnection = g_hClosePipeEvent;

	HANDLE waitHandleList[3] = {hPipeConnectEvent, g_hClosePipeEvent, g_hNewPackageEvent};

	while (true)
	{
		//Simple way to close the server. If the server of that pipe was on this end, arbitrary programs connecting to this pipe could cause false-positives.
		HANDLE hCloseNotifyPipe = CreateFile(TEXT("\\\\.\\pipe\\wmrcam_doclose"), GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (hCloseNotifyPipe != INVALID_HANDLE_VALUE)
		{
			CloseHandle(hCloseNotifyPipe);
			break;
		}

		if (hDisconnectedPipe == INVALID_HANDLE_VALUE)
		{
			hDisconnectedPipe = CreateNamedPipe(TEXT("\\\\.\\pipe\\wmrcam"), 
				PIPE_ACCESS_OUTBOUND, PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, PIPE_UNLIMITED_INSTANCES, 622592, 128, 0, NULL);
			if (hDisconnectedPipe != INVALID_HANDLE_VALUE)
			{
				tData.hPipe = hDisconnectedPipe;
				tData.hPipeConnectedEvent = hPipeConnectEvent;
				//tData.connectedBuffer = connectedPipeBuffer;
				//tData.connectedCount = connectedPipeCount;
				tData.syncClientList = &syncClientList;
				tData.syncPipeList = &syncPipeList;
				if (hConnectThread != INVALID_HANDLE_VALUE)
					CloseHandle(hConnectThread);
				hConnectThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ConnectThread, &tData, 0, NULL);
			}
			else
			{
				OutputDebugString(TEXT("Unable to create a named pipe (\"\\\\.\\pipe\\wmrcam\")\r\n!"));
				break;
			}
		}
		bool doBreak = false;
		switch (WaitForMultipleObjects(3, waitHandleList, FALSE, 1000))
		{
		case WAIT_OBJECT_0: //hPipeConnectEvent
			hDisconnectedPipe = INVALID_HANDLE_VALUE;
			continue;
		case WAIT_OBJECT_0+1: //hClosePipeEvent (shutdown the server)
			doBreak = true;
			break;
		case WAIT_OBJECT_0+2: //g_hNewPackageEvent
			break;
		case WAIT_TIMEOUT:
			continue; //allows updating the close event
		default:
			Sleep(10);
			continue;
		}
		if (doBreak)
			break;

		EnterCriticalSection(&syncClientList);
		EnterCriticalSection(&syncPipeList);
		//if (tData.connectedCount > 0)
		{
			EnterCriticalSection(&g_packageQueueLock);
			std::vector<BYTE*> localPackages;
			g_packages.swap(localPackages);
			LeaveCriticalSection(&g_packageQueueLock);
			
			for (int i = 0; i < localPackages.size(); i++)
			{
				BYTE *buffer = localPackages[i];
				DWORD bufferLen = *(DWORD*)&(localPackages[i][0]);
				for (size_t k = 0; k < tData.connectedCount; k++)
				{
					HANDLE hPipe = tData.connectedBuffer[k];
					DWORD numBytesWritten = 0;
					if (!WriteFile(hPipe, buffer, bufferLen, &numBytesWritten, NULL) && GetLastError() == ERROR_NO_DATA)
					{
						DisconnectNamedPipe(hPipe);
						tData.connectedBuffer.erase(tData.connectedBuffer.begin() + k);
						tData.connectedCount--;
					}
				}
				delete[] buffer;
			}
		}
		LeaveCriticalSection(&syncPipeList);
		LeaveCriticalSection(&syncClientList);
	}
	
	if (hConnectThread != INVALID_HANDLE_VALUE)
	{
		SetEvent(tData.hDoNotAcceptConnection);

		//lure the connect thread out of its waiting state
		HANDLE hTempCamHandle = CreateFile(TEXT("\\\\.\\pipe\\wmrcam"), GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (hTempCamHandle != INVALID_HANDLE_VALUE)
			CloseHandle(hTempCamHandle);

		//terminate it in case it does not respond (slightly unsafe)
		if (WaitForSingleObject(hConnectThread, 100) == WAIT_TIMEOUT)
			TerminateThread(hConnectThread, 1);

		CloseHandle(hConnectThread);
	}
	if (hDisconnectedPipe != INVALID_HANDLE_VALUE)
		CloseHandle(hDisconnectedPipe);

	for (int i = 0; i < tData.connectedCount; i++)
	{
		DisconnectNamedPipe(tData.connectedBuffer[i]);
		CloseHandle(tData.connectedBuffer[i]);
	}
	tData.connectedBuffer.clear();
	tData.connectedCount = 0;

	SetEvent(g_hServerClosedEvent);
	
	DeleteCriticalSection(&syncClientList);
	DeleteCriticalSection(&syncPipeList);
	CloseHandle(hPipeConnectEvent);
	
	return true;
}