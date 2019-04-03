#include "stdafx.h"
#include "PipeServer.h"
#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <functional>
#include <AclAPI.h>

#pragma comment(lib, "advapi32.lib")

#define SERVER_MAXCONTROLLERSTATEUPDATES 16
#define SERVER_MAXCONTROLLERSTREAMDATA 16

static HANDLE g_hServerClosedEvent;
static HANDLE g_hClosePipeEvent;

static HANDLE g_hNewPackageEvent;
static CRITICAL_SECTION g_packageQueueLock;
static std::vector<std::pair<BYTE*,PipePackageID>> g_packages;
static std::vector<std::pair<BYTE*, PipePackageID>> g_startLog;

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
		delete[] g_packages[i].first;
	g_packages.clear();
	for (int i = 0; i < g_startLog.size(); i++)
		delete[] g_startLog[i].first;
	g_startLog.clear();
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
	std::vector<std::pair<HANDLE, BYTE>> connectedList;
	HANDLE hPipeConnectedEvent = NULL;
	HANDLE hDoNotAcceptConnection = NULL;
};
DWORD __stdcall ConnectThread(ConnectThreadData *d)
{
	while (true)
	{
		OVERLAPPED connectOverlapped = {};
		connectOverlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		BOOL result = ConnectNamedPipe(d->hPipe, &connectOverlapped);
		DWORD lastError = GetLastError();
		bool connected = false;
		if (!result)
		{
			switch (lastError)
			{
			case ERROR_PIPE_CONNECTED:
				connected = true;
				break;
			case ERROR_IO_PENDING:
				{
					HANDLE waitHandles[2] = { connectOverlapped.hEvent, d->hDoNotAcceptConnection};
					DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
					DWORD dwTransferred;
					switch (waitResult)
					{
						case WAIT_OBJECT_0:
						{
							connected = GetOverlappedResult(d->hPipe, &connectOverlapped, &dwTransferred, FALSE) != 0;
							break;
						}
						case WAIT_OBJECT_0+1:
						default:
						{
							CancelIo(d->hPipe);
							if (GetOverlappedResult(d->hPipe, &connectOverlapped, &dwTransferred, FALSE) != 0)
								DisconnectNamedPipe(d->hPipe);
							connected = false;
							break;
						}
					}
					break;
				}
			}
		}
		CloseHandle(connectOverlapped.hEvent);
		BYTE pipeFlags = 0;
		if (connected) //Potentially changes the connected state.
		{
			DWORD flagsReadBytes = 0;
			connected = (ReadFile(d->hPipe, &pipeFlags, 1, &flagsReadBytes, NULL) != FALSE && flagsReadBytes == 1);
			if (!connected)
				DisconnectNamedPipe(d->hPipe);
		}
		if (connected)
		{
			EnterCriticalSection(d->syncClientList);
			if (WaitForSingleObject(d->hDoNotAcceptConnection, 0) == WAIT_TIMEOUT)
			{
				d->connectedList.push_back({ d->hPipe, pipeFlags });

				LeaveCriticalSection(d->syncClientList);
				
				std::vector<std::pair<BYTE*, PipePackageID>> startLogLocal;
				EnterCriticalSection(&g_packageQueueLock);
				startLogLocal.swap(g_startLog);
				LeaveCriticalSection(&g_packageQueueLock);

				for (size_t i = 0; i < startLogLocal.size(); i++)
				{
					DWORD numBytesWritten = 0;
					WriteFile(d->hPipe, startLogLocal[i].first, *(DWORD*)&(startLogLocal[i].first[0]), &numBytesWritten, NULL);
				}

				EnterCriticalSection(&g_packageQueueLock);
				startLogLocal.swap(g_startLog);
				g_startLog.insert(g_startLog.end(), startLogLocal.begin(), startLogLocal.end());
				LeaveCriticalSection(&g_packageQueueLock);

				EnterCriticalSection(d->syncClientList);

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
			sprintf_s(dbgStrBuf, "ConnectNamedPipe failed with %d.\r\n", GetLastError());
			OutputDebugStringA(dbgStrBuf);
		}
		if (WaitForSingleObject(d->hDoNotAcceptConnection, 0) == WAIT_OBJECT_0)
			break;
	}
	return 0;
}

static bool InsertPackageTo(std::vector<std::pair<BYTE*, PipePackageID>> &list, BYTE* package, PipePackageID packageType,
	std::function<unsigned int(BYTE*, PipePackageID)> const& evaluateRedundancy)
{
	bool doInsert = true;
	for (size_t i = 0; i < list.size(); i++)
	{
		unsigned int redundancyType = evaluateRedundancy(list[i].first, list[i].second);
		if (redundancyType != 0)
		{
			delete[] list[i].first;
			list.erase(list.begin() + i);
			i--;
		}
		if (redundancyType == 2)
		{
			doInsert = false;
		}
	}
	if (doInsert)
		list.push_back({ package, packageType });
	return doInsert;
}
static bool InsertPackage(
	BYTE* package,
	PipePackageID packageType,
	std::function<unsigned int(BYTE*, PipePackageID)> const& evaluateRedundancy,
	bool insertToStartLog = false)
{
	if (insertToStartLog)
	{
		DWORD len = *(DWORD*)package;
		BYTE *packageCopy = new BYTE[len];
		memcpy(packageCopy, package, len);
		if (!InsertPackageTo(g_startLog, packageCopy, packageType, evaluateRedundancy))
			delete[] packageCopy;
	}
	return InsertPackageTo(g_packages, package, packageType, evaluateRedundancy);
}

//id : stream id; count : amount of images; sizeX/sizeY : size of each image;
void OnStartCameraStream(DWORD id, unsigned short sizeX, unsigned short sizeY)
{
	BYTE *openPackage = new BYTE[13];
	*(DWORD*)(&openPackage[0]) = 13;
	openPackage[4] = PipePackage_CameraStreamStart;
	*(DWORD*)(&openPackage[5]) = id;
	*(unsigned short*)(&openPackage[9]) = sizeX;
	*(unsigned short*)(&openPackage[11]) = sizeY;

	auto redundancyLambda = [id](BYTE *otherPackage, PipePackageID otherPackageType) -> int
	{
		if (otherPackageType == PipePackage_CameraStreamStart && *(DWORD*)(&otherPackage[5]) == id) return 1;
		if (otherPackageType == PipePackage_CameraStreamStop && *(DWORD*)(&otherPackage[5]) == id) return 2;
		return 0;
	};

	EnterCriticalSection(&g_packageQueueLock);
	if (InsertPackage(openPackage, PipePackage_CameraStreamStart, redundancyLambda, true))
		SetEvent(g_hNewPackageEvent);
	else
		delete[] openPackage;
	LeaveCriticalSection(&g_packageQueueLock);
}

//id : stream type id; buf : image buffer; sizeX/sizeY : size of the image;
void OnGetStreamImage(DWORD id, const BYTE *buf, unsigned short sizeX, unsigned short sizeY,
	unsigned short gain, unsigned short exposureUs, unsigned short linePeriod, unsigned short exposureLinePeriods,
	uint64_t timestamp)
{
	DWORD packageSize = 29 + (DWORD)sizeX * (DWORD)sizeY;
	BYTE *imagePackage = new BYTE[packageSize];
	*(DWORD*)(&imagePackage[0]) = packageSize;
	imagePackage[4] = PipePackage_CameraStreamImage;
	*(DWORD*)(&imagePackage[5]) = id;
	*(unsigned short*)(&imagePackage[9]) = sizeX;
	*(unsigned short*)(&imagePackage[11]) = sizeY;
	*(unsigned short*)(&imagePackage[13]) = gain;
	*(unsigned short*)(&imagePackage[15]) = exposureUs;
	*(unsigned short*)(&imagePackage[17]) = linePeriod;
	*(unsigned short*)(&imagePackage[19]) = exposureLinePeriods;
	*(uint64_t*)(&imagePackage[21]) = timestamp;
	memcpy(&imagePackage[29], buf, (DWORD)sizeX * (DWORD)sizeY);

	auto redundancyLambda = [id](BYTE *otherPackage, PipePackageID otherPackageType) -> int
	{
		if (otherPackageType == PipePackage_CameraStreamImage && *(DWORD*)(&otherPackage[5]) == id) return 1;
		return 0;
	};

	EnterCriticalSection(&g_packageQueueLock);
	if (InsertPackage(imagePackage, PipePackage_CameraStreamImage, redundancyLambda))
		SetEvent(g_hNewPackageEvent);
	else
		delete[] imagePackage;
	LeaveCriticalSection(&g_packageQueueLock);
}

void OnStopCameraStream(DWORD id)
{
	BYTE *stopPackage = new BYTE[9];
	*(DWORD*)(&stopPackage[0]) = 9;
	stopPackage[4] = PipePackage_CameraStreamStop;
	*(DWORD*)(&stopPackage[5]) = id;

	auto redundancyLambda = [id](BYTE *otherPackage, PipePackageID otherPackageType) -> int
	{
		if (otherPackageType == PipePackage_CameraStreamStop && *(DWORD*)(&otherPackage[5]) == id) return 1;
		if (otherPackageType == PipePackage_CameraStreamStart && *(DWORD*)(&otherPackage[5]) == id) return 2;
		return 0;
	};

	EnterCriticalSection(&g_packageQueueLock);
	if (InsertPackage(stopPackage, PipePackage_CameraStreamStop, redundancyLambda, true))
		SetEvent(g_hNewPackageEvent);
	else
		delete[] stopPackage;
	LeaveCriticalSection(&g_packageQueueLock);
}

void OnErrorLog(const char *error)
{
	size_t errorLen = strlen(error);
	DWORD pkLen = (5 + errorLen * sizeof(char)) & (DWORD)~0;

	BYTE *errPackage = new BYTE[pkLen]();
	*(DWORD*)(&errPackage[0]) = pkLen;
	errPackage[4] = PipePackage_Log;
	memcpy(&errPackage[5], error, errorLen * sizeof(char));

	if (!g_started) //allow initialization error logs
	{
		g_startLog.push_back({ errPackage, PipePackage_Log });
		return;
	}

	//create a copy for g_errorLog since delete[] will be called on errPackage sooner or later
	BYTE *errPackageCopy = new BYTE[pkLen];
	memcpy(errPackageCopy, errPackage, pkLen);

	EnterCriticalSection(&g_packageQueueLock);
	g_startLog.push_back({ errPackageCopy, PipePackage_Log });

	g_packages.push_back({ errPackage, PipePackage_Log });
	SetEvent(g_hNewPackageEvent);
	LeaveCriticalSection(&g_packageQueueLock);

	OutputDebugStringA(error);
}

void OnControllerTrackingStart(DWORD handle)
{
	BYTE *startPackage = new BYTE[9];
	*(DWORD*)(&startPackage[0]) = 9;
	startPackage[4] = PipePackage_ControllerTrackingStart;
	*(DWORD*)(&startPackage[5]) = handle;

	auto redundancyLambda = [handle](BYTE *otherPackage, PipePackageID otherPackageType) -> int
	{
		if (otherPackageType == PipePackage_ControllerTrackingStart && *(DWORD*)(&otherPackage[5]) == handle) return 1;
		if (otherPackageType == PipePackage_ControllerTrackingStop && *(DWORD*)(&otherPackage[5]) == handle) return 2;
		return 0;
	};

	EnterCriticalSection(&g_packageQueueLock);
	if (InsertPackage(startPackage, PipePackage_ControllerTrackingStart, redundancyLambda, true))
		SetEvent(g_hNewPackageEvent);
	else
		delete[] startPackage;
	LeaveCriticalSection(&g_packageQueueLock);
}
void OnControllerTrackingStop(DWORD handle)
{
	BYTE *stopPackage = new BYTE[9];
	*(DWORD*)(&stopPackage[0]) = 9;
	stopPackage[4] = PipePackage_ControllerTrackingStop;
	*(DWORD*)(&stopPackage[5]) = handle;

	auto redundancyLambda = [handle](BYTE *otherPackage, PipePackageID otherPackageType) -> int
	{
		if (otherPackageType == PipePackage_ControllerTrackingStop && *(DWORD*)(&otherPackage[5]) == handle) return 1;
		if (otherPackageType == PipePackage_ControllerTrackingStart && *(DWORD*)(&otherPackage[5]) == handle) return 2;
		return 0;
	};

	EnterCriticalSection(&g_packageQueueLock);
	if (InsertPackage(stopPackage, PipePackage_ControllerTrackingStop, redundancyLambda, true))
		SetEvent(g_hNewPackageEvent);
	else
		delete[] stopPackage;
	LeaveCriticalSection(&g_packageQueueLock);
}
void OnControllerTrackingStateUpdate(DWORD handle, BYTE leftOrRight, DWORD oldState, const char *oldStateName, DWORD newState, const char *newStateName)
{
	BYTE oldStateNameLen = (BYTE)min(254, strlen(oldStateName));
	BYTE newStateNameLen = (BYTE)min(254, strlen(newStateName));
	DWORD pkLen = 20 + (oldStateNameLen+1 + newStateNameLen+1) * sizeof(char);

	BYTE *updatePackage = new BYTE[pkLen]();
	*(DWORD*)(&updatePackage[0]) = pkLen;
	updatePackage[4] = PipePackage_ControllerTrackingState;
	*(DWORD*)(&updatePackage[5]) = handle;
	updatePackage[9] = leftOrRight;
	*(DWORD*)(&updatePackage[10]) = oldState;
	*(DWORD*)(&updatePackage[14]) = newState;

	updatePackage[18] = oldStateNameLen+1;
	updatePackage[19] = newStateNameLen+1;
	memcpy(&updatePackage[20], oldStateName, oldStateNameLen * sizeof(char));
	*(char*)(&updatePackage[20 + oldStateNameLen * sizeof(char)]) = 0;
	memcpy(&updatePackage[20 + (oldStateNameLen+1) * sizeof(char)], newStateName, newStateNameLen * sizeof(char));
	*(char*)(&updatePackage[20 + (oldStateNameLen+1 + newStateNameLen) * sizeof(char)]) = 0;

	size_t updateCount = 0;
	auto redundancyLambda = [handle,&updateCount](BYTE *otherPackage, PipePackageID otherPackageType) -> int
	{
		if (otherPackageType == PipePackage_ControllerTrackingState && *(DWORD*)(&otherPackage[5]) == handle &&
			++updateCount >= SERVER_MAXCONTROLLERSTATEUPDATES) return 1;
		return 0;
	};

	EnterCriticalSection(&g_packageQueueLock);
	if (InsertPackage(updatePackage, PipePackage_ControllerTrackingState, redundancyLambda))
		SetEvent(g_hNewPackageEvent);
	else
		delete[] updatePackage;
	LeaveCriticalSection(&g_packageQueueLock);
}

void OnControllerStreamStart(DWORD handle, BYTE leftOrRight)
{
	BYTE *startPackage = new BYTE[10];
	*(DWORD*)(&startPackage[0]) = 10;
	startPackage[4] = PipePackage_ControllerStreamStart;
	*(DWORD*)(&startPackage[5]) = handle;
	startPackage[9] = leftOrRight;

	auto redundancyLambda = [handle](BYTE *otherPackage, PipePackageID otherPackageType) -> int
	{
		if (otherPackageType == PipePackage_ControllerStreamStart && *(DWORD*)(&otherPackage[5]) == handle) return 1;
		if (otherPackageType == PipePackage_ControllerStreamStop && *(DWORD*)(&otherPackage[5]) == handle) return 2;
		return 0;
	};

	EnterCriticalSection(&g_packageQueueLock);
	if (InsertPackage(startPackage, PipePackage_ControllerStreamStart, redundancyLambda, true))
		SetEvent(g_hNewPackageEvent);
	else
		delete[] startPackage;
	LeaveCriticalSection(&g_packageQueueLock);
}
void OnControllerStreamStop(DWORD handle, BYTE leftOrRight)
{
	BYTE *stopPackage = new BYTE[10];
	*(DWORD*)(&stopPackage[0]) = 10;
	stopPackage[4] = PipePackage_ControllerStreamStop;
	*(DWORD*)(&stopPackage[5]) = handle;
	stopPackage[9] = leftOrRight;

	auto redundancyLambda = [handle](BYTE *otherPackage, PipePackageID otherPackageType) -> int
	{
		if (otherPackageType == PipePackage_ControllerStreamStop && *(DWORD*)(&otherPackage[5]) == handle) return 1;
		if (otherPackageType == PipePackage_ControllerStreamStart && *(DWORD*)(&otherPackage[5]) == handle) return 2;
		return 0;
	};

	EnterCriticalSection(&g_packageQueueLock);
	if (InsertPackage(stopPackage, PipePackage_ControllerStreamStop, redundancyLambda, true))
		SetEvent(g_hNewPackageEvent);
	else
		delete[] stopPackage;
	LeaveCriticalSection(&g_packageQueueLock);
}
void OnControllerStreamData(DWORD handle, BYTE leftOrRight, const ControllerStreamData &data)
{
	DWORD pkLen = 10 + sizeof(ControllerStreamData);
	BYTE *dataPackage = new BYTE[pkLen]();
	*(DWORD*)(&dataPackage[0]) = pkLen;
	dataPackage[4] = PipePackage_ControllerStreamData;
	*(DWORD*)(&dataPackage[5]) = handle;
	dataPackage[9] = leftOrRight;
	*(ControllerStreamData*)(&dataPackage[10]) = data;

	size_t updateCount = 0;
	auto redundancyLambda = [handle, &updateCount](BYTE *otherPackage, PipePackageID otherPackageType) -> int
	{
		if (otherPackageType == PipePackage_ControllerStreamData && *(DWORD*)(&otherPackage[5]) == handle &&
			++updateCount >= SERVER_MAXCONTROLLERSTREAMDATA) return 1;
		return 0;
	};

	EnterCriticalSection(&g_packageQueueLock);
	if (InsertPackage(dataPackage, PipePackage_ControllerStreamData, redundancyLambda))
		SetEvent(g_hNewPackageEvent);
	else
		delete[] dataPackage;
	LeaveCriticalSection(&g_packageQueueLock);
}

bool InitializePipeSecurityAttributes(PSID &pPipeAccessSID, PACL &pPipeAccessACL, PSECURITY_DESCRIPTOR &pPipeAccessSD, SECURITY_ATTRIBUTES &pipeAccessSA);

bool RunCamServer()
{
	InitializeCamServer();
	if (WaitForSingleObject(g_hServerClosedEvent, 0) == WAIT_TIMEOUT)
		return false;
	ResetEvent(g_hServerClosedEvent);
	ResetEvent(g_hClosePipeEvent);

	PSID pPipeAccessSID = NULL;
	PACL pPipeAccessACL = NULL;
	PSECURITY_DESCRIPTOR pPipeAccessSD = NULL;
	SECURITY_ATTRIBUTES pipeAccessSA;
	if (!InitializePipeSecurityAttributes(pPipeAccessSID, pPipeAccessACL, pPipeAccessSD, pipeAccessSA))
		return false;

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
				PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, PIPE_UNLIMITED_INSTANCES, 622592, 128, 0, &pipeAccessSA);
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
		//if (tData.connectedList.size() > 0)
		{
			EnterCriticalSection(&g_packageQueueLock);
			std::vector<std::pair<BYTE*, PipePackageID>> localPackages;
			g_packages.swap(localPackages);
			LeaveCriticalSection(&g_packageQueueLock);
			
			for (int i = 0; i < localPackages.size(); i++)
			{
				BYTE *buffer = localPackages[i].first;
				PipePackageID pkID = localPackages[i].second;
				DWORD bufferLen = *(DWORD*)&(buffer[0]);
				for (size_t k = 0; k < tData.connectedList.size(); k++)
				{
					HANDLE hPipe = tData.connectedList[k].first;
					if (!(tData.connectedList[k].second & PipeClientFlag_DisableCamera) || pkID != PipePackage_CameraStreamImage)
					{
						DWORD numBytesWritten = 0;
						if (!WriteFile(hPipe, buffer, bufferLen, &numBytesWritten, NULL) && GetLastError() == ERROR_NO_DATA)
						{
							DisconnectNamedPipe(hPipe);
							tData.connectedList.erase(tData.connectedList.begin() + k);
						}
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

		//terminate it in case it does not respond (slightly unsafe)
		if (WaitForSingleObject(hConnectThread, 1000) == WAIT_TIMEOUT)
			TerminateThread(hConnectThread, 1);

		CloseHandle(hConnectThread);
	}
	if (hDisconnectedPipe != INVALID_HANDLE_VALUE)
		CloseHandle(hDisconnectedPipe);

	for (size_t i = 0; i < tData.connectedList.size(); i++)
	{
		DisconnectNamedPipe(tData.connectedList[i].first);
		CloseHandle(tData.connectedList[i].first);
	}
	tData.connectedList.clear();

	SetEvent(g_hServerClosedEvent);
	
	DeleteCriticalSection(&syncClientList);
	DeleteCriticalSection(&syncPipeList);
	CloseHandle(hPipeConnectEvent);

	if (pPipeAccessSID)
		FreeSid(pPipeAccessSID);
	if (pPipeAccessACL)
		LocalFree(pPipeAccessACL);
	if (pPipeAccessSD)
		LocalFree(pPipeAccessSD);
	
	return true;
}

//Based on (include trailing --) https://docs.microsoft.com/en-us/windows/desktop/SecAuthZ/creating-a-security-descriptor-for-a-new-object-in-c--
bool InitializePipeSecurityAttributes(PSID &pPipeAccessSID, PACL &pPipeAccessACL, PSECURITY_DESCRIPTOR &pPipeAccessSD, SECURITY_ATTRIBUTES &pipeAccessSA)
{
	SID_IDENTIFIER_AUTHORITY authority = SECURITY_WORLD_SID_AUTHORITY;
	if (!AllocateAndInitializeSid(&authority, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &pPipeAccessSID))
	{
		OutputDebugString(TEXT("AllocateAndInitializeSid failed!\r\n"));
		return false;
	}
	EXPLICIT_ACCESS access = {};
	access.grfAccessPermissions = GENERIC_READ | GENERIC_WRITE;
	access.grfAccessMode = SET_ACCESS;
	access.grfInheritance = NO_INHERITANCE;
	access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
	access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
	access.Trustee.ptstrName = (LPTSTR)pPipeAccessSID;

	if (SetEntriesInAcl(1, &access, NULL, &pPipeAccessACL) != ERROR_SUCCESS)
	{
		OutputDebugString(TEXT("SetEntriesInAcl failed!\r\n"));
		FreeSid(pPipeAccessSID);
		pPipeAccessSID = NULL;
		return false;
	}

	pPipeAccessSD = (PSECURITY_DESCRIPTOR)LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
	if (!pPipeAccessSD)
	{
		OutputDebugString(TEXT("LocalAlloc failed!\r\n"));
		LocalFree(pPipeAccessACL);
		FreeSid(pPipeAccessSID);
		pPipeAccessSID = NULL;
		return false;
	}
	if (!InitializeSecurityDescriptor(pPipeAccessSD, SECURITY_DESCRIPTOR_REVISION) ||
		!SetSecurityDescriptorDacl(pPipeAccessSD, TRUE, pPipeAccessACL, FALSE))
	{
		OutputDebugString(TEXT("InitializeSecurityDescriptor failed!\r\n"));
		LocalFree(pPipeAccessSD);
		LocalFree(pPipeAccessACL);
		FreeSid(pPipeAccessSID);
		pPipeAccessSID = NULL;
		return false;
	}
	pipeAccessSA.nLength = sizeof(SECURITY_ATTRIBUTES);
	pipeAccessSA.lpSecurityDescriptor = pPipeAccessSD;
	pipeAccessSA.bInheritHandle = FALSE;
	return true;
}