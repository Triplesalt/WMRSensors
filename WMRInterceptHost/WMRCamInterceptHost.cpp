#include "stdafx.h"
#include "WMRCamInterceptHost.h"
#include "PipeServer.h"
#include "HookCommon.h"

extern "C"
{
	UINT_PTR HookCloseCameraStream_RBPOffset;
	DWORD HookCloseCameraStream_StackSize;
	DWORD HookStartCameraStream_RSPSubOffs;
}
static void *g_pOpenCameraStreamHook;
static BYTE g_OpenCameraStreamHook_Backup[13];
static void *g_pOpenMirroredCameraStreamHook;
static BYTE g_OpenMirroredCameraStreamHook_Backup[13];
static void *g_pCloseCameraStreamHook;
static BYTE g_CloseCameraStreamHook_Backup[18];
static void *g_pCloseMirroredCameraStreamHook;
static BYTE g_CloseMirroredCameraStreamHook_Backup[18];
static void *g_pStartCameraStreamHook;
static BYTE g_StartCameraStreamHook_Backup[12];
static void *g_pStopCameraStreamHook;
static BYTE g_StopCameraStreamHook_Backup[12];
static HMODULE g_hMRUSBHost;
static bool g_started = false;

struct CameraFrameInfo
{
	uint32_t unknown1; //always 0
	uint16_t stride; //always 640
	uint16_t unknown2; //0 for streams 1/2, 2 for streams 3/4
	uint64_t timestamp; //Probably 100ns steps
};
typedef void(*CameraFrameCallback)(struct StreamClientEntry *handle, UINT_PTR lockFrameHandle, CameraFrameInfo *frameInfo, UINT_PTR userData);

typedef HRESULT(*tdOasis_LockFrame)(
	struct StreamClientEntry *handle, UINT_PTR lockFrameHandle,
	void **pFrameDataOut, size_t *pFrameSizeOut, 
	WORD *pGainOut, WORD *pExposureUsOut,
	WORD *pLinePeriodOut, WORD *pExposureLinePeriodsOut);
typedef HRESULT(*tdOasis_UnlockFrame)(struct StreamClientEntry *handle, UINT_PTR lockFrameHandle);
typedef HRESULT(*tdOasis_OpenCameraStream)(DWORD streamType, CameraFrameCallback userCallback, UINT_PTR userData, struct StreamClientEntry **pHandleOut);
typedef HRESULT(*tdOasis_CloseCameraStream)(struct StreamClientEntry *handle);
typedef HRESULT(*tdOasis_StartCameraStream)(struct StreamClientEntry *handle);
typedef HRESULT(*tdOasis_StopCameraStream)(struct StreamClientEntry *handle);

struct StreamClientHead
{
	//Either a self pointer or a StreamClientEntry.
	void *pLeftLink;
	//Either a self pointer or a StreamClientEntry.
	void *pRightLink;
	//Seems to be 0 all the time (not initialized?)
	SRWLOCK lock;
	//For exclusive access : lock owner Thread ID; For shared access : lock count.
	DWORD lockMarker;
	//Possibly padding
	DWORD unknown1;
	//Amount of clients that are active (for cameras the amount of started streams for the current type).
	DWORD activeClientCount;
	//Possibly padding
	DWORD unknown2;
};
struct StreamClientEntry
{
	//Either another StreamClientEntry or the StreamClientHead (located in the .data section of MRUSBHost.dll).
	void *pLeftLink;
	//Either another StreamClientEntry or the StreamClientHead (located in the .data section of MRUSBHost.dll).
	void *pRightLink;
	//Matches the camera type (1-4) passed to OpenCameraStream; Possible non-camera stream types : 
	//0 (Oasis_OpenIMUStream), 5 (Oasis_RegisterDeviceRemoveCallback), 6 (Oasis_CrystalKeySubscribeInputReport), 7 (Oasis_CrystalKeySubscribeEvents),
	//8 (Oasis_SubscribeBtPairingCommandEvents), 9 (Oasis_SubscribeBtDeviceStatusEvents)
	DWORD streamType;
	//Always set to 1, except during destruction.
	DWORD isValidEntry;
	//1 for OpenCameraStream, 0 for OpenMirroredCameraStream.
	DWORD subType;
	//For cameras, it is 0 by default and set to 1/0 by StartCameraStream and StopCameraStream, respectively.
	DWORD isActive;
	//Parameters vary depending on streamType. For cameras : OnFrameReceived(StreamClientEntry *handle, UINT_PTR lockFrameHandle, void *unknown, UINT_PTR userData).
	void *userCallback;
	//User parameter, passed to the callback.
	UINT_PTR userData;
};

tdOasis_OpenCameraStream Oasis_OpenCameraStream;
tdOasis_OpenCameraStream Oasis_OpenMirroredCameraStream;
tdOasis_CloseCameraStream Oasis_CloseCameraStream;
tdOasis_CloseCameraStream Oasis_CloseMirroredCameraStream;
tdOasis_StartCameraStream Oasis_StartCameraStream;
tdOasis_StopCameraStream Oasis_StopCameraStream;
tdOasis_LockFrame Oasis_LockFrame;
tdOasis_UnlockFrame Oasis_UnlockFrame;

static StreamClientEntry *g_OwnCameraStreams[4] = {};
static unsigned int g_ActiveCameraStreamCounts[4] = {};

void OnCameraFrame(struct StreamClientEntry *handle, UINT_PTR lockFrameHandle, CameraFrameInfo *frameInfo, UINT_PTR userData)
{
	DWORD streamType = handle->streamType;
	if (handle->streamType < 1 || handle->streamType > 4)
		OnErrorLog("OnCameraFrame type out of known range!\r\n");
	else
	{
		void *frameData = nullptr; size_t frameSize = 0;
		WORD gain = 0, exposureUs = 0, linePeriod = 0, exposureLinePeriods = 0;
		if (FAILED(Oasis_LockFrame(handle, lockFrameHandle, &frameData, &frameSize, &gain, &exposureUs, &linePeriod, &exposureLinePeriods)))
			OnErrorLog("Oasis_LockFrame failed!\r\n");
		else
		{
			if (frameSize != 640*480)
				OnErrorLog("Oasis_LockFrame returned an unexpected frame size!\r\n");
			else
				OnGetStreamImage(handle->streamType - 1, (BYTE*)frameData, 640, 480, gain, exposureUs, linePeriod, exposureLinePeriods, frameInfo->timestamp);
			Oasis_UnlockFrame(handle, lockFrameHandle);
		}
	}
}

//Hook called after Oasis_OpenCameraStream.
extern "C" void _OnOpenCameraStream(StreamClientEntry *handle)
{
	if (handle->streamType < 1 || handle->streamType > 4)
		OnErrorLog("OpenCameraStream : type out of known range!\r\n");
}

//Hook called before Oasis_CloseCameraStream.
extern "C" void _OnCloseCameraStream(StreamClientEntry *handle)
{
	if (handle->streamType < 1 || handle->streamType > 4)
		OnErrorLog("CloseCameraStream type out of known range!\r\n");
	else if (handle->isActive)
		OnErrorLog("CloseCameraStream called on an active handle!\r\n");
}

extern "C" void _OnStartCameraStream(StreamClientEntry *handle)
{
	if (handle->streamType < 1 || handle->streamType > 4)
		OnErrorLog("StartCameraStream type out of known range!\r\n");
	else if (handle != g_OwnCameraStreams[handle->streamType - 1])
	{
		if (InterlockedIncrementAcquire(&g_ActiveCameraStreamCounts[handle->streamType - 1]) == 1)
		{
			Oasis_StartCameraStream(g_OwnCameraStreams[handle->streamType - 1]);
			OnStartCameraStream(handle->streamType - 1, 640, 480);
		}
	}
}

extern "C" void _OnStopCameraStream(StreamClientEntry *handle)
{
	if (handle->streamType < 1 || handle->streamType > 4)
		OnErrorLog("StopCameraStream type out of known range!\r\n");
	else if (handle != g_OwnCameraStreams[handle->streamType - 1])
	{
		if (InterlockedDecrementAcquire(&g_ActiveCameraStreamCounts[handle->streamType - 1]) == 0)
		{
			Oasis_StopCameraStream(g_OwnCameraStreams[handle->streamType - 1]);
			OnStopCameraStream(handle->streamType - 1);
		}
	}
}

//Hook.asm import
extern "C" void _Hook_OpenCameraStream();
extern "C" void _Hook_CloseCameraStream();
extern "C" void _Hook_StartCameraStream();
extern "C" void _Hook_StopCameraStream();

//MRUSBHost.dll
//40 55 53 56 57 41 54 41 55 41 56 41 57
static const BYTE OpenCameraStream_Pattern[] = {
	0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57
};
static const BYTE OpenCameraStream_Mask[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
//40 55 53 56 57 41 54 41 55 41 56 41 57
static const BYTE OpenMirroredCameraStream_Pattern[] = {
	0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57
};
static const BYTE OpenMirroredCameraStream_Mask[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
//48 89 5C 24 10 55 48 8D 6C 24 ?? 48 81 EC ?? ?? ?? ??
static const BYTE CloseCameraStream_Pattern[] = {
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x48, 0x8D, 0x6C, 0x24, 0x00, 0x48, 0x81, 0xEC, 0x00, 0x00, 0x00, 0x00
};
static const BYTE CloseCameraStream_Mask[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
//48 89 5C 24 10 55 48 8D 6C 24 ?? 48 81 EC ?? ?? ?? ??
static const BYTE CloseMirroredCameraStream_Pattern[] = {
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x48, 0x8D, 0x6C, 0x24, 0x00, 0x48, 0x81, 0xEC, 0x00, 0x00, 0x00, 0x00
};
static const BYTE CloseMirroredCameraStream_Mask[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
//48 89 5C 24 08 57 48 83 EC ?? 33 DB 48 8B F9 8B 49 ??
static const BYTE StartCameraStream_Pattern[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x00, 0x33, 0xDB, 0x48, 0x8B, 0xF9, 0x8B, 0x49, 0x00
};
static const BYTE StartCameraStream_Mask[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
};
//48 89 5C 24 10 48 89 74 24 18 55 57
static const BYTE StopCameraStream_Pattern[] = {
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x55, 0x57
};
static const BYTE StopCameraStream_Mask[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};



void WMRCamInterceptHost::Startup()
{
	if (g_started) return;

	for (size_t i = 0; i < 4; i++)
	{
		g_OwnCameraStreams[i] = nullptr;
		g_ActiveCameraStreamCounts[i] = 0;
	}

	//InitializeCriticalSection(&g_imageLock);
	g_hMRUSBHost = GetModuleHandle(TEXT("MRUSBHost.dll"));
	if (!g_hMRUSBHost)
	{
		OnErrorLog("ERROR: Can't find MRUSBHost.dll!\r\n");
		OnErrorLog("Camera hooks are disabled.\r\n");
		return;
	}
	void *pTextSection = nullptr;
	size_t textSectionLen = 0;
	GetImageSection(g_hMRUSBHost, pTextSection, textSectionLen, ".text");
	if (!pTextSection || textSectionLen == 0)
	{
		FreeLibrary(g_hMRUSBHost);
		OnErrorLog("ERROR: Can't find the .text section of MRUSBHost.dll!\r\n");
		OnErrorLog("Camera hooks are disabled.\r\n");
		return;
	}

	void *pDataSection = nullptr;
	size_t dataSectionLen = 0;
	GetImageSection(g_hMRUSBHost, pDataSection, dataSectionLen, ".data");
	if (!pDataSection || dataSectionLen == 0)
	{
		FreeLibrary(g_hMRUSBHost);
		OnErrorLog("ERROR: Can't find the .data section of MRUSBHost.dll!\r\n");
		OnErrorLog("Camera hooks are disabled.\r\n");
		return;
	}

	DWORD oldProt = 0;
	VirtualProtect(pTextSection, textSectionLen, PAGE_EXECUTE_READWRITE, &oldProt);

	Oasis_OpenCameraStream = (tdOasis_OpenCameraStream)GetProcAddress(g_hMRUSBHost, "Oasis_OpenCameraStream");
	Oasis_OpenMirroredCameraStream = (tdOasis_OpenCameraStream)GetProcAddress(g_hMRUSBHost, "Oasis_OpenMirroredCameraStream");
	Oasis_CloseCameraStream = (tdOasis_CloseCameraStream)GetProcAddress(g_hMRUSBHost, "Oasis_CloseCameraStream");
	Oasis_CloseMirroredCameraStream = (tdOasis_CloseCameraStream)GetProcAddress(g_hMRUSBHost, "Oasis_CloseMirroredCameraStream");
	Oasis_StartCameraStream = (tdOasis_StartCameraStream)GetProcAddress(g_hMRUSBHost, "Oasis_StartCameraStream");
	Oasis_StopCameraStream = (tdOasis_StopCameraStream)GetProcAddress(g_hMRUSBHost, "Oasis_StopCameraStream");
	Oasis_LockFrame = (tdOasis_LockFrame)GetProcAddress(g_hMRUSBHost, "Oasis_LockFrame");
	Oasis_UnlockFrame = (tdOasis_UnlockFrame)GetProcAddress(g_hMRUSBHost, "Oasis_UnlockFrame");

	if (!Oasis_OpenCameraStream ||
		!FindPattern(Oasis_OpenCameraStream, sizeof(OpenCameraStream_Pattern), OpenCameraStream_Pattern, OpenCameraStream_Mask, sizeof(OpenCameraStream_Pattern)))
	{
		OnErrorLog("ERROR: Oasis_OpenCameraStream not found or has an unknown function body!\r\n");
		goto fail;
	}
	if (!Oasis_OpenMirroredCameraStream ||
		!FindPattern(Oasis_OpenMirroredCameraStream, sizeof(OpenMirroredCameraStream_Pattern), OpenMirroredCameraStream_Pattern, OpenMirroredCameraStream_Mask, sizeof(OpenMirroredCameraStream_Pattern)))
	{
		OnErrorLog("ERROR: Oasis_OpenMirroredCameraStream not found or has an unknown function body!\r\n");
		goto fail;
	}
	if (!Oasis_CloseCameraStream ||
		!FindPattern(Oasis_CloseCameraStream, sizeof(CloseCameraStream_Pattern), CloseCameraStream_Pattern, CloseCameraStream_Mask, sizeof(CloseCameraStream_Pattern)))
	{
		OnErrorLog("ERROR: Oasis_CloseCameraStream not found or has an unknown function body!\r\n");
		goto fail;
	}
	if (!Oasis_CloseMirroredCameraStream ||
		!FindPattern(Oasis_CloseMirroredCameraStream, sizeof(CloseMirroredCameraStream_Pattern), CloseMirroredCameraStream_Pattern, CloseMirroredCameraStream_Mask, sizeof(CloseMirroredCameraStream_Pattern)))
	{
		OnErrorLog("ERROR: Oasis_CloseMirroredCameraStream not found or has an unknown function body!\r\n");
		goto fail;
	}
	if (memcmp(Oasis_CloseCameraStream, Oasis_CloseMirroredCameraStream, sizeof(CloseCameraStream_Pattern)))
	{
		OnErrorLog("ERROR: Oasis_CloseCameraStream and Oasis_CloseMirroredCameraStream entry points are different!\r\n");
		goto fail;
	}
	if (!Oasis_StartCameraStream ||
		!FindPattern(Oasis_StartCameraStream, sizeof(StartCameraStream_Pattern), StartCameraStream_Pattern, StartCameraStream_Mask, sizeof(StartCameraStream_Pattern)))
	{
		OnErrorLog("ERROR: Oasis_StartCameraStream not found or has an unknown function body!\r\n");
		goto fail;
	}
	if (!Oasis_StopCameraStream ||
		!FindPattern(Oasis_StopCameraStream, sizeof(StopCameraStream_Pattern), StopCameraStream_Pattern, StopCameraStream_Mask, sizeof(StopCameraStream_Pattern)))
	{
		OnErrorLog("ERROR: Oasis_StopCameraStream not found or has an unknown function body!\r\n");
		goto fail;
	}
	if (!Oasis_LockFrame)
	{
		OnErrorLog("ERROR: Oasis_LockFrame not found!\r\n");
		goto fail;
	}
	if (!Oasis_UnlockFrame)
	{
		OnErrorLog("ERROR: Oasis_LockFrame not found!\r\n");
		goto fail;
	}

	//Hoping that no other thread calls these functions or modifies the stream client list during this operation.
	for (size_t i = 0; i < 4; i++)
	{
		if (FAILED(Oasis_OpenCameraStream((DWORD)(i + 1), OnCameraFrame, (UINT_PTR)(i + 1), &g_OwnCameraStreams[i])))
		{
			OnErrorLog("ERROR: Unable to open a camera stream!\r\n");
			goto fail;
		}
		StreamClientEntry *pCurEntry = g_OwnCameraStreams[i];
		while (pCurEntry->pLeftLink != g_OwnCameraStreams[i])
		{
			pCurEntry = (StreamClientEntry*)pCurEntry->pLeftLink;
			if (((UINT_PTR)pCurEntry >= (UINT_PTR)pDataSection) && ((UINT_PTR)pCurEntry < ((UINT_PTR)pDataSection + dataSectionLen)))
				continue; //pCurEntry is the StreamClientHead.
			if (pCurEntry->isActive)
				g_ActiveCameraStreamCounts[i]++;
		}
		if (g_ActiveCameraStreamCounts[i] > 0)
		{
			Oasis_StartCameraStream(g_OwnCameraStreams[i]);
			OnStartCameraStream((DWORD)i, 640, 480);
		}
		else
		{
			Oasis_StartCameraStream(g_OwnCameraStreams[i]);
			Oasis_StopCameraStream(g_OwnCameraStreams[i]);
		}
	}

	BYTE *pOpenCameraStream = (BYTE*)Oasis_OpenCameraStream;
	BYTE *pOpenMirroredCameraStream = (BYTE*)Oasis_OpenMirroredCameraStream;
	BYTE *pCloseCameraStream = (BYTE*)Oasis_CloseCameraStream;
	BYTE *pCloseMirroredCameraStream = (BYTE*)Oasis_CloseMirroredCameraStream;
	BYTE *pStartCameraStream = (BYTE*)Oasis_StartCameraStream;
	BYTE *pStopCameraStream = (BYTE*)Oasis_StopCameraStream;

	memcpy(g_OpenCameraStreamHook_Backup, pOpenCameraStream, 13);
	g_pOpenCameraStreamHook = pOpenCameraStream;

	pOpenCameraStream[0] = 0x48;
	pOpenCameraStream[1] = 0xB8;
	*(unsigned long long int*)(&pOpenCameraStream[2]) = (unsigned long long int)&_Hook_OpenCameraStream;
	pOpenCameraStream[10] = 0xFF;
	pOpenCameraStream[11] = 0xD0;
	pOpenCameraStream[12] = 0x90;

	memcpy(g_OpenMirroredCameraStreamHook_Backup, pOpenMirroredCameraStream, 13);
	g_pOpenMirroredCameraStreamHook = pOpenMirroredCameraStream;

	pOpenMirroredCameraStream[0] = 0x48;
	pOpenMirroredCameraStream[1] = 0xB8;
	*(unsigned long long int*)(&pOpenMirroredCameraStream[2]) = (unsigned long long int)&_Hook_OpenCameraStream;
	pOpenMirroredCameraStream[10] = 0xFF;
	pOpenMirroredCameraStream[11] = 0xD0;
	pOpenMirroredCameraStream[12] = 0x90;

	HookCloseCameraStream_RBPOffset = (UINT_PTR)((INT_PTR)(char)pCloseCameraStream[10]);
	HookCloseCameraStream_StackSize = *(unsigned int*)(&pCloseCameraStream[14]);

	memcpy(g_CloseCameraStreamHook_Backup, pCloseCameraStream, 18);
	g_pCloseCameraStreamHook = pCloseCameraStream;

	pCloseCameraStream[0] = 0x48;
	pCloseCameraStream[1] = 0xB8;
	*(unsigned long long int*)(&pCloseCameraStream[2]) = (unsigned long long int)&_Hook_CloseCameraStream;
	pCloseCameraStream[10] = 0xFF;
	pCloseCameraStream[11] = 0xD0;
	memset(&pCloseCameraStream[12], 0x90, 6);

	memcpy(g_CloseMirroredCameraStreamHook_Backup, pCloseMirroredCameraStream, 18);
	g_pCloseMirroredCameraStreamHook = pCloseMirroredCameraStream;

	pCloseMirroredCameraStream[0] = 0x48;
	pCloseMirroredCameraStream[1] = 0xB8;
	*(unsigned long long int*)(&pCloseMirroredCameraStream[2]) = (unsigned long long int)&_Hook_CloseCameraStream;
	pCloseMirroredCameraStream[10] = 0xFF;
	pCloseMirroredCameraStream[11] = 0xD0;
	memset(&pCloseMirroredCameraStream[12], 0x90, 6);

	memcpy(g_StartCameraStreamHook_Backup, pStartCameraStream, 12);
	g_pStartCameraStreamHook = pStartCameraStream;

	HookStartCameraStream_RSPSubOffs = ((BYTE*)pStartCameraStream)[9];
	pStartCameraStream[0] = 0x48;
	pStartCameraStream[1] = 0xB8;
	*(unsigned long long int*)(&pStartCameraStream[2]) = (unsigned long long int)&_Hook_StartCameraStream;
	pStartCameraStream[10] = 0xFF;
	pStartCameraStream[11] = 0xD0;

	memcpy(g_StopCameraStreamHook_Backup, pStopCameraStream, 12);
	g_pStopCameraStreamHook = pStopCameraStream;

	pStopCameraStream[0] = 0x48;
	pStopCameraStream[1] = 0xB8;
	*(unsigned long long int*)(&pStopCameraStream[2]) = (unsigned long long int)&_Hook_StopCameraStream;
	pStopCameraStream[10] = 0xFF;
	pStopCameraStream[11] = 0xD0;

	g_started = true;
	fail:
	VirtualProtect(pTextSection, textSectionLen, oldProt, &oldProt);
	if (!g_started)
	{
		if (Oasis_CloseCameraStream)
		{
			for (size_t i = 0; i < 4; i++)
			{
				if (g_OwnCameraStreams[i])
				{
					if (g_ActiveCameraStreamCounts[i] > 0)
					{
						Oasis_StopCameraStream(g_OwnCameraStreams[i]);
						OnStopCameraStream((DWORD)i);
					}
					Oasis_CloseCameraStream(g_OwnCameraStreams[i]);
					g_OwnCameraStreams[i] = nullptr;
				}
			}
		}
		FreeLibrary(g_hMRUSBHost);
		OnErrorLog("Camera hooks are disabled.\r\n");
	}
}

void WMRCamInterceptHost::Shutdown()
{
	if (!g_started)
		return;
	if (g_hMRUSBHost)
	{
		void *pTextSection = nullptr;
		size_t textSectionLen = 0;
		GetImageSection(g_hMRUSBHost, pTextSection, textSectionLen, ".text");
		if (pTextSection && textSectionLen)
		{
			DWORD oldProt = 0;
			VirtualProtect(pTextSection, textSectionLen, PAGE_EXECUTE_READWRITE, &oldProt);

			if (g_pOpenCameraStreamHook)
			{
				memcpy(g_pOpenCameraStreamHook, g_OpenCameraStreamHook_Backup, 13);
			}
			if (g_pOpenMirroredCameraStreamHook)
			{
				memcpy(g_pOpenMirroredCameraStreamHook, g_OpenMirroredCameraStreamHook_Backup, 13);
			}
			if (g_pCloseCameraStreamHook)
			{
				memcpy(g_pCloseCameraStreamHook, g_CloseCameraStreamHook_Backup, 18);
			}
			if (g_pCloseMirroredCameraStreamHook)
			{
				memcpy(g_pCloseMirroredCameraStreamHook, g_CloseMirroredCameraStreamHook_Backup, 18);
			}
			if (g_pStartCameraStreamHook)
			{
				memcpy(g_pStartCameraStreamHook, g_StartCameraStreamHook_Backup, 12);
			}
			if (g_pStopCameraStreamHook)
			{
				memcpy(g_pStopCameraStreamHook, g_StopCameraStreamHook_Backup, 12);
			}

			VirtualProtect(pTextSection, textSectionLen, oldProt, &oldProt);
		}

		if (Oasis_CloseCameraStream)
		{
			for (size_t i = 0; i < 4; i++)
			{
				if (g_OwnCameraStreams[i])
				{
					if (InterlockedExchangeAcquire(&g_ActiveCameraStreamCounts[i], 0) > 0)
						Oasis_StopCameraStream(g_OwnCameraStreams[i]);
					Oasis_CloseCameraStream(g_OwnCameraStreams[i]);
					g_OwnCameraStreams[i] = nullptr;
				}
			}
		}
		FreeLibrary(g_hMRUSBHost);
		g_hMRUSBHost = NULL;
	}
	g_started = false;
}