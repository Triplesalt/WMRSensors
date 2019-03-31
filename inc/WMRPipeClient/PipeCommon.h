#pragma once

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

};