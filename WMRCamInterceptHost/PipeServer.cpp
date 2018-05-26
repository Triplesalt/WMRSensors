#include "stdafx.h"
#include "PipeServer.h"
#include <stdlib.h>
#include <stdio.h>
#include <vector>

HANDLE hServerClosedEvent;
HANDLE hClosePipeEvent;

HANDLE g_hNewPackageEvent;
CRITICAL_SECTION g_packageQueueLock;
std::vector<BYTE*> g_packages;

enum PipePackageIDs
{
	PipePackage_StartStream,
	PipePackage_StreamImage,
	PipePackage_StopStream,
	PipePackage_Keepalive,
};

bool initializedCamServer = false;
void InitializeCamServer()
{
	if (initializedCamServer)
		return;
	initializedCamServer = true;
	hClosePipeEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	hServerClosedEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
	g_hNewPackageEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
	InitializeCriticalSection(&g_packageQueueLock);
}

void CloseCamServer()
{
	if (!initializedCamServer)
		return;
	initializedCamServer = false;
	SetEvent(hClosePipeEvent);
	WaitForSingleObject(hServerClosedEvent, INFINITE);
	CloseHandle(hServerClosedEvent);
	CloseHandle(hClosePipeEvent);
	CloseHandle(g_hNewPackageEvent);
	DeleteCriticalSection(&g_packageQueueLock);
}

struct ConnectThreadData
{
	HANDLE hPipe;
	CRITICAL_SECTION *syncPipeList;
	CRITICAL_SECTION *syncClientList;
	HANDLE *connectedBuffer;
	int connectedCount;
	HANDLE hPipeConnectedEvent;
	HANDLE hDoNotAcceptConnection;
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
				HANDLE *newConnectedPipeBuffer = new HANDLE[d->connectedCount+1];
				memcpy(newConnectedPipeBuffer, d->connectedBuffer, sizeof(HANDLE) * d->connectedCount);
				newConnectedPipeBuffer[d->connectedCount] = d->hPipe;
				delete[] d->connectedBuffer;
				d->connectedBuffer = newConnectedPipeBuffer;
				d->connectedCount++;

				SetEvent(d->hPipeConnectedEvent);
			}
			else
				CloseHandle(d->hPipe);
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

static void OnRequestKeepalive()
{
	BYTE *stopPackage = new BYTE[7];
	*(DWORD*)(&stopPackage[0]) = 5;
	stopPackage[4] = PipePackage_Keepalive;

	EnterCriticalSection(&g_packageQueueLock);
	for (size_t i = 0; i < g_packages.size(); i++)
	{
		if (g_packages[i][4] == PipePackage_Keepalive)
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

bool RunCamServer()
{
	InitializeCamServer();
	if (WaitForSingleObject(hServerClosedEvent, 0) == WAIT_TIMEOUT)
		return false;
	ResetEvent(hServerClosedEvent);
	ResetEvent(hClosePipeEvent);

	HANDLE hDisconnectedPipe = INVALID_HANDLE_VALUE;
	
	HANDLE hPipeConnectEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	CRITICAL_SECTION syncPipeList;
	InitializeCriticalSection(&syncPipeList);
	CRITICAL_SECTION syncClientList;
	InitializeCriticalSection(&syncClientList);

	HANDLE hConnectThread = INVALID_HANDLE_VALUE;
	ConnectThreadData tData; ZeroMemory(&tData, sizeof(ConnectThreadData));
	tData.connectedBuffer = new HANDLE[0];
	tData.connectedCount = 0;
	tData.hDoNotAcceptConnection = hClosePipeEvent;

	HANDLE waitHandleList[3] = {hPipeConnectEvent, hClosePipeEvent, g_hNewPackageEvent};

	while (true)
	{
		if (hDisconnectedPipe == INVALID_HANDLE_VALUE)
		{
			hDisconnectedPipe = CreateNamedPipe(TEXT("\\\\.\\pipe\\wmrcam"), 
				PIPE_ACCESS_OUTBOUND, PIPE_TYPE_BYTE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES, 1024, 128, 0, NULL);
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
		switch (WaitForMultipleObjects(3, waitHandleList, FALSE, INFINITE))
		{
		case WAIT_OBJECT_0: //hPipeConnectEvent
			hDisconnectedPipe = INVALID_HANDLE_VALUE;
			continue;
		case WAIT_OBJECT_0+1: //hClosePipeEvent (shutdown the server)
			doBreak = true;
			break;
		case WAIT_OBJECT_0+2: //g_hNewPackageEvent
			break;
		//case WAIT_TIMEOUT:
		//	OnRequestKeepalive();
		//	continue;
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
				for (int k = 0; k < tData.connectedCount; k++)
				{
					HANDLE hPipe = tData.connectedBuffer[k];
					DWORD numBytesWritten = 0;
					WriteFile(hPipe, buffer, bufferLen, &numBytesWritten, NULL);
				}
				delete[] buffer;
			}
		}
		LeaveCriticalSection(&syncPipeList);
		LeaveCriticalSection(&syncClientList);
	}
	
	if (hConnectThread != INVALID_HANDLE_VALUE)
	{
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
	delete[] tData.connectedBuffer;
	tData.connectedCount = 0;

	SetEvent(hServerClosedEvent);
	
	DeleteCriticalSection(&syncClientList);
	DeleteCriticalSection(&syncPipeList);
	CloseHandle(hPipeConnectEvent);
	
	return true;
}