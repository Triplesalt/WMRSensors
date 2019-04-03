#pragma once
#include <stdint.h>

#define PipeClientFlag_DisableCamera (1<<0)
enum PipePackageID
{
	//Camera
	PipePackage_CameraFIRST=0,
	PipePackage_CameraStreamStart=0,
	PipePackage_CameraStreamStop,
	PipePackage_CameraStreamImage,
	PipePackage_CameraEND,
	//Log
	PipePackage_LogFIRST=16,
	PipePackage_Log=16,
	PipePackage_LogEND,
	//Controller
	PipePackage_ControllerFIRST=32,
	PipePackage_ControllerTrackingStart=32,
	PipePackage_ControllerTrackingStop,
	PipePackage_ControllerTrackingState,
	PipePackage_ControllerStreamStart,
	PipePackage_ControllerStreamStop,
	PipePackage_ControllerStreamData,
	PipePackage_ControllerEND,
};

struct ControllerStreamData
{
	float gyroscope[3];
	float accelerometer[3];
	float magnetometer[3]; //Does not appear to be used by WMR tracking.
	float unknown1; //Known values in range 33-41, not sure what it is.
	uint64_t timestamp1; //100ns ticks
	uint64_t timestamp2; //100ns ticks
	uint32_t unknown2; //Known values : 0,1,2,3.
};