#pragma once

/*
Windows Mixed Reality Intercept Pipe Client. Connects to the host dll loaded into the WMR driver.
*/

#if defined(PIPECLIENT_DLL)
#define PIPECLIENT_API __declspec(dllexport) 
#elif defined(PIPECLIENT_LIB)
#define PIPECLIENT_API
#else
#define PIPECLIENT_API __declspec(dllimport) 
#endif

#include "..\inc\WMRPipeClient\PipeCommon.h"
#include <vector>
#include <algorithm>
#include <stdint.h>

class IWMRPipeClientListener
{
public:
	/* Called whenever the pipe client has successfully connected to the host. */
	virtual void OnClientConnected() = 0;
	/* Called whenever the pipe client connection has been lost. */
	virtual void OnClientDisconnected() = 0;

	/* Called regularly once the client is connected, not tied to any event. Call delay depends on the updateTimeoutMs value of the pipe client. */
	virtual void Update() = 0;
};

class IWMRCameraListener
{
public:
	/* Called whenever a stream starts. May not be called if this listener is added to the pipe client after the client has started.
	id : ID of the stream (0 : Primary stream left, 1 : Primary stream right, 2 : Secondary stream left, 3 : Secondary stream right).
	     The primary stream is used for head tracking, the secondary stream is used for controller tracking (same camera but different gain properties).
	count : Amount of independent images (2 for stereo vision).
	sizeX : Expected x-size of each image (pixels per line).
	sizeY : Expected y-size of each image (amount of lines).
	*/
	virtual void OnStartStream(unsigned int id, unsigned int sizeX, unsigned int sizeY) = 0;
	/* Called whenever a stream stops. May not be called if this listener is added to the pipe client after the client has started.
	id : ID of the stream.
	*/
	virtual void OnStopStream(unsigned int id) = 0;

	/* Called whenever the pipe client has received an image.
	id : ID of the stream.
	sizeX : Expected x-size of each image (pixels per line).
	sizeY : Expected y-size of each image (amount of lines).
	buffer : Buffer containing the grayscale image (8bpp). Must not be used after returning from the callback.
	gain, exposureUs, linePeriod, exposureLinePeriods : Additional image metadata.
	*/
	virtual void OnImage(unsigned int id, unsigned int sizeX, unsigned int sizeY, const uint8_t *buffer,
		unsigned short gain, unsigned short exposureUs, unsigned short linePeriod, unsigned short exposureLinePeriods,
		uint64_t timestamp) = 0;
};

class IWMRLogListener
{
public:
	/* Called whenever the client receives a log line from the host. 
	line : The log line as a C string. Must not be used after returning from the callback.
	*/
	virtual void OnHostLog(const char *line) = 0;

	/* Called whenever the client wants to print to the debug log.
	line : The log line as a C string. Must not be used after returning from the callback.
	*/
	virtual void OnClientLog(const char *line) = 0;
};

//Not implemented yet
class IWMRControllerListener
{
public:
	/* Called whenever the controller tracking starts. Usually called after the controller has been connected.
	leftOrRight : 0 for left, 1 for right controller.
	*/
	virtual void OnTrackingStart(int leftOrRight) = 0;

	/* Called whenever the controller tracking stops. Usually called after the controller has been disconnected.
	leftOrRight : 0 for left, 1 for right controller.
	*/
	virtual void OnTrackingStop(int leftOrRight) = 0;

	/* Called whenever the controller tracking state changes. 
	leftOrRight : 0 for left, 1 for right controller.
	oldState : Old state value. Heavily dependant of driver internals, only use it for debugging purposes.
	oldStateName : Old state name.
	newState : New state value. Heavily dependant of driver internals, only use it for debugging purposes.
	newStateName : New state name.
	*/
	virtual void OnTrackingStateChange(int leftOrRight, uint32_t oldState, const char *oldStateName, uint32_t newState, const char *newStateName) = 0;

	/* Called whenever the controller tracking data stream starts. Usually called after the controller has been connected or moved.
	leftOrRight : 0 for left, 1 for right controller.
	*/
	virtual void OnStreamStart(int leftOrRight) = 0;

	/* Called whenever the controller tracking data stream stops. Usually called after the controller has been disconnected or not moved for a while.
	leftOrRight : 0 for left, 1 for right controller.
	*/
	virtual void OnStreamStop(int leftOrRight) = 0;

	/* Called whenever the driver has received controller tracking data.
	leftOrRight : 0 for left, 1 for right controller.
	data : Controller tracking data, see PipeCommon.h.
	*/
	virtual void OnStreamData(int leftOrRight, const ControllerStreamData &data) = 0;
};

/* Pipe Client. Not thread-safe. */
class WMRInterceptPipeClient
{
	std::vector<IWMRPipeClientListener*> clientListeners;
	std::vector<IWMRCameraListener*> cameraListeners;
	std::vector<IWMRLogListener*> logListeners;
	std::vector<IWMRControllerListener*> controllerListeners;
	bool enableCamera;
	uint32_t updateTimeoutMs;

public:
	/* enableCamera : Set to false if camera data is not required. Default : true
	   updateTimeoutMs : Timeout for the Update callback of client listeners. 
	                     Can be set to (uint32_t)-1 if the callback is not required. Default : 100 */
	PIPECLIENT_API WMRInterceptPipeClient(bool enableCamera = true, uint32_t updateTimeoutMs = 100);

	/* Starts the pipe client. Will not return to the caller, unless Stop was called by a listener. */
	PIPECLIENT_API void Run();

	/* Stops the pipe client, if it is running. Must be called from within the same thread the pipe client runs in, i.e. only through listeners. */
	PIPECLIENT_API void Stop();
	
	inline void AddClientListener(IWMRPipeClientListener &listener)
	{
		clientListeners.push_back(&listener);
	}
	inline void RemoveClientListener(IWMRPipeClientListener &listener)
	{
		clientListeners.erase(std::remove(clientListeners.begin(), clientListeners.end(), &listener), clientListeners.end());
	}

	inline void AddCameraListener(IWMRCameraListener &listener)
	{
		cameraListeners.push_back(&listener);
	}
	inline void RemoveCameraListener(IWMRCameraListener &listener)
	{
		cameraListeners.erase(std::remove(cameraListeners.begin(), cameraListeners.end(), &listener), cameraListeners.end());
	}

	inline void AddLogListener(IWMRLogListener &listener)
	{
		logListeners.push_back(&listener);
	}
	inline void RemoveLogListener(IWMRLogListener &listener)
	{
		logListeners.erase(std::remove(logListeners.begin(), logListeners.end(), &listener), logListeners.end());
	}

	inline void AddControllerListener(IWMRControllerListener &listener)
	{
		controllerListeners.push_back(&listener);
	}
	inline void RemoveControllerListener(IWMRControllerListener &listener)
	{
		controllerListeners.erase(std::remove(controllerListeners.begin(), controllerListeners.end(), &listener), controllerListeners.end());
	}

private:
	inline void BroadcastClientLog(const char *line)
	{
		for (size_t i = 0; i < logListeners.size(); i++)
			logListeners[i]->OnClientLog(line);
	}

	PIPECLIENT_API void HandleHostMessage(unsigned char *data, size_t len);
};

PIPECLIENT_API void SendCloseHostCommand(uint32_t timeout = 2000);
