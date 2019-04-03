#include "stdafx.h"
#include "WMRCamInterceptHost.h"
#include "WMRControllerInterceptHost.h"
#include "PipeServer.h"

DWORD WINAPI InterceptHostThread(PVOID arg)
{
	//Initialize pipe server locks and events
	InitializeCamServer();
	//Setup hooks
	WMRCamInterceptHost::Startup();
	WMRControllerInterceptHost::Startup();
	//Start and run the pipe server
	RunCamServer();
	//Remove hooks
	WMRControllerInterceptHost::Shutdown();
	WMRCamInterceptHost::Shutdown();
	Sleep(10); //wait a bit in case a hook callback is not done yet
	//Free remaining pipe server resources
	CloseCamServer();

	FreeLibraryAndExitThread((HMODULE)arg, 0);
	return 0;
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		CloseHandle(CreateThread(NULL, 0, InterceptHostThread, hModule, 0, NULL));
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

