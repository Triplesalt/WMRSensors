#pragma once
#include "..\inc\WMRPipeClient\PipeCommon.h"

bool RunCamServer();
void CloseCamServer();

void OnStartCameraStream(WORD id, unsigned char count, unsigned short sizeX, unsigned short sizeY);
void OnGetStreamImage(WORD id, const BYTE *buf, unsigned char count, unsigned short sizeX, unsigned short sizeY);
inline void OnGetStreamImageDefault(WORD id, const BYTE *imageBuf)
{
	OnGetStreamImage(id, imageBuf, 2, 640, 480);
}
void OnStopCameraStream(WORD id);
void OnErrorLog(const char *error);
void OnControllerTrackingStart(BYTE leftOrRight);
void OnControllerTrackingStop(BYTE leftOrRight);
void OnControllerTrackingStateUpdate(BYTE leftOrRight, DWORD oldState, const char *oldStateName, DWORD newState, const char *newStateName);
void OnControllerStreamStart(BYTE leftOrRight);
void OnControllerStreamStop(BYTE leftOrRight);
void OnControllerStreamData(BYTE leftOrRight, const ControllerStreamData &data);
