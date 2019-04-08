#include "stdafx.h"
#include "..\inc\WMRPipeClient\PipeClient.h"
#include <Windows.h>

#ifndef min
#define min(a,b) ((a < b) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) ((a > b) ? (a) : (b))
#endif

WMRInterceptPipeClient::WMRInterceptPipeClient(bool enableCamera, uint32_t updateTimeoutMs)
{
	this->enableCamera = enableCamera;
	this->updateTimeoutMs = updateTimeoutMs;
	if (updateTimeoutMs == (uint32_t)-1)
		this->updateTimeoutMs = INFINITE; //actually the same as (DWORD)-1
}

void WMRInterceptPipeClient::HandleHostMessage(unsigned char *data, size_t len)
{
	if (len >= 5)
	{
		switch (data[4])
		{
		case PipePackage_CameraStreamStart:
			if (len >= 13)
			{
				DWORD id = *(DWORD*)(&data[5]);
				unsigned int sizeX = *(unsigned short*)(&data[9]);
				unsigned int sizeY = *(unsigned short*)(&data[11]);
				for (size_t i = 0; i < cameraListeners.size(); i++)
					cameraListeners[i]->OnStartStream(id, sizeX, sizeY);
			}
			break;
		case PipePackage_CameraStreamStop:
			if (len >= 9)
			{
				DWORD id = *(DWORD*)(&data[5]);
				for (size_t i = 0; i < cameraListeners.size(); i++)
					cameraListeners[i]->OnStopStream(id);
			}
			break;
		case PipePackage_CameraStreamImage:
			if (len >= 29)
			{
				DWORD id = *(DWORD*)(&data[5]);
				unsigned int sizeX = *(unsigned short*)(&data[9]);
				unsigned int sizeY = *(unsigned short*)(&data[11]);
				unsigned short gain = *(unsigned short*)(&data[13]);
				unsigned short exposureUs = *(unsigned short*)(&data[15]); 
				unsigned short linePeriod = *(unsigned short*)(&data[17]); 
				unsigned short exposureLinePeriods = *(unsigned short*)(&data[19]);
				uint64_t timestamp = *(uint64_t*)(&data[21]);
				if (len >= (29 + (size_t)sizeX * (size_t)sizeY))
				{
					for (size_t i = 0; i < cameraListeners.size(); i++)
						cameraListeners[i]->OnImage(id, sizeX, sizeY, &data[29], gain, exposureUs, linePeriod, exposureLinePeriods, timestamp);
				}
			}
			break;
		case PipePackage_IMUStreamStart:
			for (size_t i = 0; i < imuListeners.size(); i++)
				imuListeners[i]->OnStreamStart();
			break;
		case PipePackage_IMUStreamStop:
			for (size_t i = 0; i < imuListeners.size(); i++)
				imuListeners[i]->OnStreamStop();
			break;
		case PipePackage_IMUStreamSample:
			if (len >= 5 + sizeof(IMUSample))
			{
				const IMUSample *pSample = (const IMUSample*)(&data[5]);
				for (size_t i = 0; i < imuListeners.size(); i++)
					imuListeners[i]->OnStreamData(*pSample);
			}
			break;
		case PipePackage_Log:
			if (len >= 6)
			{
				char *logTemp = new char[len - 5 + 1];
				memcpy(logTemp, &data[5], (len - 5) * sizeof(char));
				logTemp[len - 5] = 0;
				for (size_t i = 0; i < logListeners.size(); i++)
					logListeners[i]->OnHostLog(logTemp);
				delete[] logTemp;
			}
			break;
		case PipePackage_ControllerTrackingStart:
			if (len >= 9)
			{
				DWORD handle = *(DWORD*)(&data[5]);
				for (size_t i = 0; i < controllerListeners.size(); i++)
					controllerListeners[i]->OnTrackingStart(handle);
			}
			break;
		case PipePackage_ControllerTrackingStop:
			if (len >= 9)
			{
				DWORD handle = *(DWORD*)(&data[5]);
				for (size_t i = 0; i < controllerListeners.size(); i++)
					controllerListeners[i]->OnTrackingStop(handle);
			}
			break;
		case PipePackage_ControllerTrackingState:
			if (len >= 20)
			{
				DWORD handle = *(DWORD*)(&data[5]);
				BYTE leftOrRight = data[9];
				DWORD oldState = *(DWORD*)(&data[10]);
				DWORD newState = *(DWORD*)(&data[14]);
				BYTE oldStateNameLen = data[18];
				BYTE newStateNameLen = data[19];
				const char *oldStateName = (const char*)(&data[20]);
				const char *newStateName = (const char*)(&data[20 + oldStateNameLen]);
				if (len >= 20 + oldStateNameLen + newStateNameLen &&
					oldStateNameLen > 0 && oldStateName[oldStateNameLen - 1] == 0 &&
					newStateNameLen > 0 && newStateName[newStateNameLen - 1] == 0)
				{
					for (size_t i = 0; i < controllerListeners.size(); i++)
						controllerListeners[i]->OnTrackingStateChange(handle, leftOrRight, oldState, oldStateName, newState, newStateName);
				}
			}
			break;
		case PipePackage_ControllerStreamStart:
			if (len >= 10)
			{
				DWORD handle = *(DWORD*)(&data[5]);
				BYTE leftOrRight = data[9];
				for (size_t i = 0; i < controllerListeners.size(); i++)
					controllerListeners[i]->OnStreamStart(handle, leftOrRight);
			}
			break;
		case PipePackage_ControllerStreamStop:
			if (len >= 10)
			{
				DWORD handle = *(DWORD*)(&data[5]);
				BYTE leftOrRight = data[9];
				for (size_t i = 0; i < controllerListeners.size(); i++)
					controllerListeners[i]->OnStreamStop(handle, leftOrRight);
			}
			break;
		case PipePackage_ControllerStreamData:
			if (len >= 10 + sizeof(ControllerStreamData))
			{
				DWORD handle = *(DWORD*)(&data[5]);
				BYTE leftOrRight = data[9];
				const ControllerStreamData *pStreamData = (const ControllerStreamData*)(&data[10]);
				for (size_t i = 0; i < controllerListeners.size(); i++)
					controllerListeners[i]->OnStreamData(handle, leftOrRight, *pStreamData);
			}
			break;
		}
	}
}

void WMRInterceptPipeClient::Run()
{
	LPCTSTR namedPipeName = TEXT("\\\\.\\pipe\\wmrcam");
	ULONGLONG lastUpdateCall = 0;
	ULONGLONG tmpTickCount;
	BroadcastClientLog("Connecting to pipe server... ");
	while (true)
	{
		if (WaitNamedPipe(namedPipeName, NMPWAIT_WAIT_FOREVER))
		{
			BOOL result;
			HANDLE hPipe = CreateFile(namedPipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
			if (hPipe == INVALID_HANDLE_VALUE)
			{
				DWORD lastError = GetLastError();
				if (lastError != ERROR_FILE_NOT_FOUND)
				{
					char logBuffer[128];
					sprintf_s(logBuffer, "Failed (Open error %u)\n", lastError);
					BroadcastClientLog(logBuffer);
				}
				Sleep(500);
				continue;
			}
			OVERLAPPED rwOverlapped = {};
			rwOverlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

			BYTE connectFlags = enableCamera ? 0 : PipeClientFlag_DisableCamera;
			DWORD tmpWritten = 0;
			result = WriteFile(hPipe, &connectFlags, 1, &tmpWritten, &rwOverlapped);
			if (!result && GetLastError() == ERROR_IO_PENDING)
			{
				while (WaitForSingleObject(rwOverlapped.hEvent, INFINITE) != WAIT_OBJECT_0) {}
				ResetEvent(rwOverlapped.hEvent);
			}

			BroadcastClientLog("Connected\n");
			for (size_t i = 0; i < clientListeners.size(); i++)
				clientListeners[i]->OnClientConnected();

			std::vector<uint8_t> buffer;
			buffer.resize(1024);
			size_t bufferedDataLen = 0;
			DWORD numRead = 0;

			//Read from the host.
			while (true)
			{
				//Use asynchronous I/O to facilitate timer callbacks.
				BOOL result = ReadFile(hPipe, &buffer.data()[bufferedDataLen], (DWORD)min(1024 * 1024, buffer.size() - bufferedDataLen), NULL, &rwOverlapped);
				if (!result)
				{
					DWORD lastError = GetLastError();
					if (lastError != ERROR_IO_PENDING)
						break;
					DWORD waitState;
					do
					{
						if (this->updateTimeoutMs != INFINITE)
						{
							//Wait for the read operation to finish but use a timeout to call the Update callbacks.
							ULONGLONG updateDelta = GetTickCount64() - lastUpdateCall;
							DWORD curTimeout = (updateDelta >= this->updateTimeoutMs) ? 0 : (DWORD)(this->updateTimeoutMs - updateDelta);
							waitState = WaitForSingleObject(rwOverlapped.hEvent, curTimeout);
							if (waitState == WAIT_TIMEOUT)
							{
								lastUpdateCall = GetTickCount64();
								for (size_t i = 0; i < clientListeners.size(); i++)
									clientListeners[i]->Update();
							}
						}
						else
							waitState = WaitForSingleObject(rwOverlapped.hEvent, INFINITE);
					} while (waitState != WAIT_OBJECT_0);
					if (!GetOverlappedResult(hPipe, &rwOverlapped, &numRead, FALSE))
						break;
					ResetEvent(rwOverlapped.hEvent);
				}
				else if (!GetOverlappedResult(hPipe, &rwOverlapped, &numRead, FALSE))
					break;
				if (this->updateTimeoutMs != INFINITE && (tmpTickCount = GetTickCount64()) - lastUpdateCall >= this->updateTimeoutMs)
				{
					lastUpdateCall = tmpTickCount;
					for (size_t i = 0; i < clientListeners.size(); i++)
						clientListeners[i]->Update();
				}

				uint8_t *data = buffer.data();
				if (numRead > 0)
				{
					bufferedDataLen += numRead;

					uint8_t *curData = data;
					size_t remainingDataLen = bufferedDataLen;
					//Handle all messages in the data buffer.
					while (remainingDataLen >= 4 && remainingDataLen >= *(DWORD*)(&curData[0]))
					{
						DWORD curDataLen = *(DWORD*)(&curData[0]);
						HandleHostMessage(curData, curDataLen);

						curData += curDataLen;
						remainingDataLen -= curDataLen;
					}
					//Remove processed messages from the data buffer.
					if (curData != data)
					{
						memmove(data, curData, remainingDataLen);
						memset(&data[remainingDataLen], 0, buffer.size() - remainingDataLen);
						bufferedDataLen = remainingDataLen;
					}
					//Enlarge the data buffer if it is nearly full.
					if ((buffer.size() - bufferedDataLen) < 128)
						buffer.resize(buffer.size() + max(bufferedDataLen, 1024));
				}
			}
			CloseHandle(rwOverlapped.hEvent);
			CloseHandle(hPipe);
			for (size_t i = 0; i < clientListeners.size(); i++)
				clientListeners[i]->OnClientDisconnected();
			BroadcastClientLog("Connecting to pipe server... ");
		}
		else
		{
			DWORD lastError = GetLastError();
			if (lastError != ERROR_FILE_NOT_FOUND)
			{
				char logBuffer[128];
				sprintf_s(logBuffer, "Failed (Wait error %u)\n", lastError);
				BroadcastClientLog(logBuffer);
			}
			Sleep(500);
		}
	}
}

void WMRInterceptPipeClient::Stop()
{
	BroadcastClientLog("ERROR: WMRInterceptPipeClient::Stop() has not been implemented yet!\n");
}

void SendCloseHostCommand(uint32_t timeout)
{
	HANDLE hCloseHostPipe = CreateNamedPipe(TEXT("\\\\.\\pipe\\wmrcam_doclose"),
		PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, PIPE_UNLIMITED_INSTANCES, 1024, 128, 0, NULL);
	if (hCloseHostPipe != INVALID_HANDLE_VALUE)
	{
		OVERLAPPED connectOverlapped = {};
		connectOverlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		bool connected = false;
		if (!ConnectNamedPipe(hCloseHostPipe, &connectOverlapped))
		{
			switch (GetLastError())
			{
			case ERROR_PIPE_CONNECTED:
				connected = true;
				break;
			case ERROR_IO_PENDING:
				if (WaitForSingleObject(connectOverlapped.hEvent, timeout) == WAIT_OBJECT_0)
				{
					DWORD dwTransferred;
					connected = GetOverlappedResult(hCloseHostPipe, &connectOverlapped, &dwTransferred, FALSE) != 0;
				}
				else
					CancelIo(hCloseHostPipe);
				break;
			}
		}
		else
			connected = true;
		CloseHandle(connectOverlapped.hEvent);
		if (connected)
		{
			DisconnectNamedPipe(hCloseHostPipe);
			printf("The camera host should shut down now.\n");
		}
		else
			printf("WARNING: Couldn't reach the camera host!\n");
		CloseHandle(hCloseHostPipe);
	}
	else
		printf("ERROR: Can't create the named pipe!\n");
}