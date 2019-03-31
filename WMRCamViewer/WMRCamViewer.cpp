#include "stdafx.h"

#include "CamWindow.h"

#define PIPECLIENT_LIB
#include "..\inc\WMRPipeClient\PipeClient.h"

class CamViewer_PipeClientListener : public IWMRPipeClientListener
{
public:
	void OnClientConnected() { }
	void OnClientDisconnected() { }
	void Update() { }
};
class CamViewer_CameraListener : public IWMRCameraListener
{
public:
	void OnStartStream(unsigned int id, unsigned int count, unsigned int sizeX, unsigned int sizeY)
	{
		printf("Stream %u started : %u times %ux%u.\n", id, count, sizeX, sizeY);
	}
	void OnStopStream(unsigned int id)
	{
		CamWindow_OnCloseCam(id);
		printf("Stream %u stopped.\n", id);
	}
	void OnImages(unsigned int id, unsigned int count, unsigned int sizeX, unsigned int sizeY, const uint8_t *buffer)
	{
		if (count == 2 && sizeX == 640 && sizeY == 480)
		{
			CamWindow_OnData(id, count, sizeX, sizeY, buffer);
		}
		else
		{
			printf("Unsupported image in stream %u : %u times %ux%u.\n", id, count, sizeX, sizeY);
		}
	}
};
class CamViewer_LogListener : public IWMRLogListener
{
public:
	void OnHostLog(const char *log)
	{
		printf("Host log : %s", log);
	}
	void OnClientLog(const char *log)
	{
		printf("%s", log);
	}
};

int _tmain(int argc, _TCHAR* argv[])
{
	if (argc > 0 && !_tcscmp(argv[argc - 1], TEXT("shutdown")))
	{
		SendCloseHostCommand();
		return 0;
	}
	if (CamWindow_Init(GetModuleHandle(NULL)))
	{
		WMRInterceptPipeClient client;

		CamViewer_PipeClientListener clientListener;
		client.AddClientListener(clientListener);
		CamViewer_CameraListener cameraListener;
		client.AddCameraListener(cameraListener);
		CamViewer_LogListener logListener;
		client.AddLogListener(logListener);

		client.Run();
		CamWindow_Close();
	}
	return 0;
}

