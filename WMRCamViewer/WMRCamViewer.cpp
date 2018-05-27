#include "stdafx.h"

#include "CamWindow.h"
#include "PipeClient.h"

int _tmain(int argc, _TCHAR* argv[])
{
	if (argc > 0 && !_tcscmp(argv[argc - 1], TEXT("shutdown")))
	{
		SendCloseHostCommand();
		return 0;
	}
	if (CamWindow_Init(GetModuleHandle(NULL)))
	{
		RunPipeClient();
		CamWindow_Close();
	}
	return 0;
}

