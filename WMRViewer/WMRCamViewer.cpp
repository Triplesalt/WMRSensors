#include "stdafx.h"
#include <array>
#include <vector>
#include <stdint.h>

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
	uint64_t lastBufferTimestamps[4];
	std::array<std::vector<uint8_t>, 2> buffers;
public:
	CamViewer_CameraListener()
	{
		for (size_t i = 0; i < 4; i++)
			lastBufferTimestamps[i] = 0;
		for (size_t i = 0; i < 2; i++)
			buffers[i].resize(2 * 640 * 480);
	}
	void OnStartStream(unsigned int id, unsigned int sizeX, unsigned int sizeY)
	{
		printf("Stream %u started : %ux%u.\n", id, sizeX, sizeY);
	}
	void OnStopStream(unsigned int id)
	{
		if (id&1)
			CamWindow_OnCloseCam(id >> 1);
		printf("Stream %u stopped.\n", id);
	}
	void OnImage(unsigned int id, unsigned int sizeX, unsigned int sizeY, const uint8_t *buffer,
		unsigned short gain, unsigned short exposureUs, unsigned short linePeriod, unsigned short exposureLinePeriods,
		uint64_t timestamp)
	{
		if (id < 4 && sizeX == 640 && sizeY == 480)
		{
			lastBufferTimestamps[id] = timestamp;
			memcpy(&(buffers[id >> 1].data())[(id & 1) * 640 * 480], buffer, 640 * 480);
			if (timestamp == lastBufferTimestamps[id ^ 1])
				CamWindow_OnData(id >> 1, 2, sizeX, sizeY, buffers[id >> 1].data());
		}
		else
		{
			printf("Unsupported image in stream %u : %ux%u.\n", id, sizeX, sizeY);
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
class CamViewer_ControllerListener : public IWMRControllerListener
{
	uint64_t lastIMUPrint = 0;
public:
	void OnTrackingStart(uint32_t handle)
	{
		printf("Controller tracking start : %u\n", handle);
	}
	void OnTrackingStop(uint32_t handle)
	{
		printf("Controller tracking stop : %u\n", handle);
	}
	void OnTrackingStateChange(uint32_t handle, uint32_t leftOrRight, uint32_t oldState, const char *oldStateName, uint32_t newState, const char *newStateName)
	{
		printf("Controller tracking state : %u (%s), %s -> %s\n", handle, GetDeviceTypeString(leftOrRight), oldStateName, newStateName);
	}

	void OnStreamStart(uint32_t handle, uint32_t leftOrRight)
	{
		printf("Controller IMU stream start : %u (%s)\n", handle, GetDeviceTypeString(leftOrRight));
	}
	void OnStreamStop(uint32_t handle, uint32_t leftOrRight)
	{
		printf("Controller IMU stream stop : %u (%s)\n", handle, GetDeviceTypeString(leftOrRight));
	}
	void OnStreamData(uint32_t handle, uint32_t leftOrRight, const ControllerStreamData &data)
	{
		uint64_t curTime = GetTickCount64();
		if (curTime - lastIMUPrint >= 100)
		{
			lastIMUPrint = curTime;
			printf("Controller IMU stream data : %u (%s) (gyro (%7.3f|%7.3f|%7.3f), accel (%7.3f|%7.3f|%7.3f))\n",
				handle, GetDeviceTypeString(leftOrRight),
				data.gyroscope[0], data.gyroscope[1], data.gyroscope[2],
				data.accelerometer[0], data.accelerometer[1], data.accelerometer[2]);
		}
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
		CamViewer_ControllerListener controllerListener;
		client.AddControllerListener(controllerListener);

		client.Run();
		CamWindow_Close();
	}
	return 0;
}

