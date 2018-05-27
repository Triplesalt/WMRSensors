#include "stdafx.h"
#include "WMRCamInterceptHost.h"
#include "PipeServer.h"

extern "C"
{
	DWORD HookGrabImage_RBXBackOffs;
	DWORD HookStartCameraStream_RSPSubOffs;
}
static void *g_pStartCameraStreamHook;
static BYTE g_StartCameraStreamHook_Backup[12];
static void *g_pGrabImageHook;
static BYTE g_GrabImageHook_Backup[12];
static void *g_pStopCameraStreamHook;
static BYTE g_StopCameraStreamHook_Backup[12];
static bool g_started = false;

extern "C" void _OnStartCameraStream(WORD camID)
{
	if (camID == 0)
		OnErrorLog("StartCameraStream ID 0!\r\n");
	if (camID & 1) //ignore left camera start
		return;
	OnStartCameraStream(camID - 2, 2, 640, 480);
}


extern "C" void _OnStopCameraStream(WORD camID)
{
	if (camID == 0)
		OnErrorLog("StopCameraStream ID 0!\r\n");
	if (camID & 1) //ignore left camera start
		return;
	OnStopCameraStream(camID - 2);
}

//static DWORD CameraInfo_timestampOffset = (DWORD)-1;
//static DWORD CameraInfo_IDOffset = (DWORD)-1;

static BYTE *pTempImageBuffer;

extern "C" void _OnGrabCameraImage(WORD camID, DWORD timestamp)
{
	/*WORD camID = 0;
	if (metadataContainer != nullptr && CameraInfo_IDOffset != (DWORD)-1)
		camID = *(WORD*)((UINT_PTR)metadataContainer + 6);*/
	OnGetStreamImageDefault(camID, pTempImageBuffer);
	
	//two 640*480*1bpp images combined : first line belongs to the left image, second line belongs to the right image
	/*EnterCriticalSection(&g_imageLock);
	for (int y = 0; y < 480; y++)
	{
		for (int img = 0; img < 2; img++)
		{
			memcpy(&g_images[img][640*y], &images[640*(2*y+img)], 640);
		}
	}
	LeaveCriticalSection(&g_imageLock);
	g_imagesUpdated = true;*/
}

//Hook.asm import
extern "C" void _Hook_StartCameraStream();
extern "C" void _Hook_GrabImage();
extern "C" void _Hook_StopCameraStream();

//MRUSBHost.dll
//48 8D 05 ?? ?? ?? ?? 0F 42 F1 49 03 D6 48 69 CF 00 B0 04 00 44 8B C6 48 03 CB 48 03 C8 E8
static const BYTE GrabImage_Copy_Pattern[] = {
	0x48, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x42, 0xF1, 0x49, 0x03, 0xD6, 0x48, 0x69, 0xCF, 0x00, 0xB0, 0x04, 0x00, 0x44, 0x8B, 0xC6, 0x48, 0x03, 0xCB, 0x48, 0x03, 0xC8, 0xE8
};
static const BYTE GrabImage_Copy_Mask[] = {
	0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
//42 0F B7 84 31 ?? ?? ?? ?? 66 41 89 47 06 41 83 7E ?? 00 74 04 49 03 56 ?? 48 8B 5C 24 ?? 33 C0 49 89 57 08 48 83 C4 ??
static const BYTE GrabImage_Hook_Pattern[] = {
	0x42, 0x0F, 0xB7, 0x84, 0x31, 0x00, 0x00, 0x00, 0x00, 0x66, 0x41, 0x89, 0x47, 0x06, 0x41, 0x83, 0x7E, 0x00, 0x00, 0x74, 0x04, 0x49, 0x03, 0x56, 0x00, 0x48, 0x8B, 0x5C, 0x24, 0x00, 0x33, 0xC0, 0x49, 0x89, 0x57, 0x08, 0x48, 0x83, 0xC4, 0x00
};
static const BYTE GrabImage_Hook_Mask[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
};
//48 89 5C 24 08 57 48 83 EC ?? 33 DB 48 8B F9 8B 49 ??
static const BYTE StartStream_Pattern[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x00, 0x33, 0xDB, 0x48, 0x8B, 0xF9, 0x8B, 0x49, 0x00
};
static const BYTE StartStream_Mask[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
};
//48 89 5C 24 10 48 89 74 24 18 55 57
static const BYTE StopStream_Pattern[] = {
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x55, 0x57
};
static const BYTE StopStream_Mask[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

//4C 8B 35 ?? ?? ?? ?? B8 80 02 00 00 4C 8B 7C 24 ?? 41 83 27 00 66 41 89 47 04
//static const BYTE GrabImage_CameraInfo_Pattern[] = {
//	0x4C, 0x8B, 0x35, 0x00, 0x00, 0x00, 0x00, 0xB8, 0x80, 0x02, 0x00, 0x00, 0x4C, 0x8B, 0x7C, 0x24, 0x00, 0x41, 0x83, 0x27, 0x00, 0x66, 0x41, 0x89, 0x47, 0x04
//};
//static const BYTE GrabImage_CameraInfo_Mask[] = {
//	0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
//};

void *FindPattern(void *regionStart, size_t regionSize, const BYTE *pattern, const BYTE *mask, size_t patternSize)
{
	void *regionEnd = (void*)((UINT_PTR)regionStart + regionSize);
	for (BYTE *curLoc = ((BYTE*)regionStart); (UINT_PTR)(curLoc + patternSize) <= (UINT_PTR)regionEnd; curLoc = &curLoc[1])
	{
		bool found = true;
		for (size_t i = 0; i < patternSize; i++)
		{
			if ((curLoc[i] & mask[i]) != (pattern[i] & mask[i]))
			{
				found = false;
				break;
			}
		}
		if (found)
			return curLoc;
	}
	return nullptr;
}

void GetTextSection(HMODULE hModule, void *&pTextSection, size_t &textSectionLen)
{
	pTextSection = nullptr;
	textSectionLen = 0;

	PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hModule;
	PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((UINT_PTR)dosHeader + dosHeader->e_lfanew);
	IMAGE_SECTION_HEADER *sectionHeaders = (IMAGE_SECTION_HEADER*)((UINT_PTR)ntHeaders + sizeof(IMAGE_NT_HEADERS));
	for (unsigned int i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++)
	{
		if (!memcmp(sectionHeaders[i].Name, ".text\0", 6))
		{
			pTextSection = (void*)((UINT_PTR)hModule + sectionHeaders[i].VirtualAddress);
			textSectionLen = sectionHeaders[i].Misc.VirtualSize;
			break;
		}
	}
}

void Startup()
{
	if (g_started) return;
	//InitializeCriticalSection(&g_imageLock);
	HMODULE hMRUSBHost = nullptr;
	if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, TEXT("MRUSBHost.dll"), &hMRUSBHost) && hMRUSBHost)
	{
		//TODO : Sanity checks (i.e. compare NT Headers signature with "PE\0\0", etc.)
		void *pTextSection = nullptr;
		size_t textSectionLen = 0;
		GetTextSection(hMRUSBHost, pTextSection, textSectionLen);

		if (pTextSection && textSectionLen)
		{
			//Hook grab image
			BYTE *pGrabImageCopy = (BYTE*)FindPattern(pTextSection, textSectionLen, GrabImage_Copy_Pattern, GrabImage_Copy_Mask, sizeof(GrabImage_Copy_Pattern));
			if (pGrabImageCopy)
			{
				//BYTE *pCameraInfoCode = (BYTE*)FindPattern(pGrabImageCopy, ((BYTE*)pTextSection + textSectionLen) - pGrabImageCopy,
				//	GrabImage_CameraInfo_Pattern, GrabImage_CameraInfo_Mask, sizeof(GrabImage_CameraInfo_Pattern));
				BYTE *pGrabImageHook = (BYTE*)FindPattern(pGrabImageCopy, ((BYTE*)pTextSection + textSectionLen) - pGrabImageCopy,
					GrabImage_Hook_Pattern, GrabImage_Hook_Mask, sizeof(GrabImage_Hook_Pattern));

				if (pGrabImageHook) //&& pCameraInfoCode
				{
					pGrabImageHook += 25;
					DWORD oldProt = 0;
					VirtualProtect(pTextSection, textSectionLen, PAGE_EXECUTE_READWRITE, &oldProt);

					pTempImageBuffer = (BYTE*)(*(DWORD*)(&pGrabImageCopy[3]) + (UINT_PTR)&pGrabImageCopy[7]);

					memcpy(g_GrabImageHook_Backup, pGrabImageHook, 12);
					g_pGrabImageHook = pGrabImageHook;

					HookGrabImage_RBXBackOffs = pGrabImageHook[4];

					pGrabImageHook[0] = 0x48;
					pGrabImageHook[1] = 0xB8;
					*(unsigned long long int*)(&pGrabImageHook[2]) = (unsigned long long int)&_Hook_GrabImage;
					pGrabImageHook[10] = 0xFF;
					pGrabImageHook[11] = 0xD0;


					//CameraInfo_timestampOffset = pCameraInfoCode[11];
					//CameraInfo_IDOffset = pCameraInfoCode[19];

					BYTE *pStartCameraStream = (BYTE*)GetProcAddress(hMRUSBHost, "Oasis_StartCameraStream");
					BYTE *pStopCameraStream = (BYTE*)GetProcAddress(hMRUSBHost, "Oasis_StopCameraStream");
					if (pStartCameraStream && pStopCameraStream)
					{
						//verify
						if (FindPattern(pStartCameraStream, sizeof(StartStream_Pattern), StartStream_Pattern, StartStream_Mask, sizeof(StartStream_Pattern)))
						{
							//verify
							if (FindPattern(pStopCameraStream, sizeof(StopStream_Pattern), StopStream_Pattern, StopStream_Mask, sizeof(StopStream_Pattern)))
							{
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
							}
							else
								OnErrorLog("Oasis_StopCameraStream has an unknown function body!\r\n");
						}
						else
							OnErrorLog("Oasis_StartCameraStream has an unknown function body!\r\n");
					}
					else
						OnErrorLog("Can't find Oasis_StartCameraStream and/or Oasis_StopCameraStream!\r\n");

					VirtualProtect(pTextSection, textSectionLen, oldProt, &oldProt);
					g_started = true;
				}
				else
					OnErrorLog("Can't find GrabImage_Hook!\r\n");
			}
			else
				OnErrorLog("Can't find GrabImage_Copy!\r\n");
		}
		else
			OnErrorLog("Can't find .text section of MRUSBHost.dll!\r\n");
	}
	else
		OnErrorLog("Can't find MRUSBHost.dll!\r\n");
}

void Shutdown()
{
	if (!g_started)
		return;
	HMODULE hMRUSBHost = nullptr;
	if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, TEXT("MRUSBHost.dll"), &hMRUSBHost) && hMRUSBHost)
	{
		void *pTextSection = nullptr;
		size_t textSectionLen = 0;
		GetTextSection(hMRUSBHost, pTextSection, textSectionLen);
		if (pTextSection && textSectionLen)
		{
			DWORD oldProt = 0;
			VirtualProtect(pTextSection, textSectionLen, PAGE_EXECUTE_READWRITE, &oldProt);

			if (g_pGrabImageHook)
			{
				memcpy(g_pGrabImageHook, g_GrabImageHook_Backup, 12);
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
	}
	g_started = false;
}