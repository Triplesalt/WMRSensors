#pragma once
#include "..\inc\WMRPipeClient\PipeCommon.h"

void InitializeCamServer();
bool RunCamServer();
void CloseCamServer();

void OnStartCameraStream(DWORD id, unsigned short sizeX, unsigned short sizeY);
void OnGetStreamImage(DWORD id, const BYTE *buf, unsigned short sizeX, unsigned short sizeY,
	unsigned short gain, unsigned short exposureUs, unsigned short linePeriod, unsigned short exposureLinePeriods,
	uint64_t timestamp);
void OnStopCameraStream(DWORD id);
void OnErrorLog(const char *error);
void OnControllerTrackingStart(BYTE leftOrRight);
void OnControllerTrackingStop(BYTE leftOrRight);
void OnControllerTrackingStateUpdate(BYTE leftOrRight, DWORD oldState, const char *oldStateName, DWORD newState, const char *newStateName);
void OnControllerStreamStart(BYTE leftOrRight);
void OnControllerStreamStop(BYTE leftOrRight);
void OnControllerStreamData(BYTE leftOrRight, const ControllerStreamData &data);
