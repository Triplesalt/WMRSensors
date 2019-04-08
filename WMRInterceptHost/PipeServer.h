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
void OnHMDIMUStreamStart();
void OnHMDIMUStreamStop();
void OnHMDIMUSample(const IMUSample &sample);
void OnErrorLog(const char *error);
void OnControllerTrackingStart(DWORD handle);
void OnControllerTrackingStop(DWORD handle);
void OnControllerTrackingStateUpdate(DWORD handle, BYTE leftOrRight, DWORD oldState, const char *oldStateName, DWORD newState, const char *newStateName);
void OnControllerStreamStart(DWORD handle, BYTE leftOrRight);
void OnControllerStreamStop(DWORD handle, BYTE leftOrRight);
void OnControllerStreamData(DWORD handle, BYTE leftOrRight, const ControllerStreamData &data);
