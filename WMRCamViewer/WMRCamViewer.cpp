#include "stdafx.h"

#include "CamWindow.h"
#include "PipeClient.h"

int _tmain(int argc, _TCHAR* argv[])
{
	if (CamWindow_Init(GetModuleHandle(NULL)))
	{
		RunPipeClient();
		CamWindow_Close();
	}
	return 0;
}

