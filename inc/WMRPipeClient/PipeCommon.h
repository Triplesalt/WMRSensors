#pragma once
#include <stdint.h>

#define PipeClientFlag_DisableCamera (1<<0)
enum PipePackageID
{
	//Camera/HMD
	PipePackage_HMDFIRST=0,
	PipePackage_CameraStreamStart=0,
	PipePackage_CameraStreamStop,
	PipePackage_CameraStreamImage,
	PipePackage_IMUStreamStart,
	PipePackage_IMUStreamStop,
	PipePackage_IMUStreamSample,
	PipePackage_HMDEND,
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

struct IMUSample
{
	uint16_t accelerometerHistoryCount; //<= 4
	uint16_t gyroscopeHistoryCount; //<= 32
	uint16_t magnetometerHistoryCount; //<= 4, usually 0 or 1
	uint16_t padding;
	//0x008
	uint64_t gyroscopeHistoryTimestamps[32];
	//0x108
	float gyroscopeTemperatureHistory[32]; //degrees Fahrenheit ?
	//0x188
	float gyroscopeXHistory[32]; //gyroscope x-axis ??
	//0x208
	float gyroscopeYHistory[32]; //gyroscope y-axis ??
	//0x288
	float gyroscopeZHistory[32]; //gyroscope z-axis ??
	//0x308
	uint64_t accelerometerHistoryTimestamps[4];
	//0x328
	float accelerometerTemperatureHistory[4]; //degrees Fahrenheit ?
	//0x338
	float accelerometerXHistory[4]; //accelerometer x-axis??
	//0x348
	float accelerometerYHistory[4]; //accelerometer y-axis??
	//0x358
	float accelerometerZHistory[4]; //accelerometer z-axis??
	//0x368
	uint64_t magnetometerHistoryTimestamps[4];
	//0x388
	float magnetometerXHistory[4]; //magnetometer x-axis??
	//0x398
	float magnetometerYHistory[4]; //magnetometer y-axis??
	//0x3A8
	float magnetometerZHistory[4]; //magnetometer z-axis??
	//0x3B8
};