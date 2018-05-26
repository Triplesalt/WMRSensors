#pragma once

bool CamWindow_Init(HINSTANCE hInstance);
void CamWindow_OnData(unsigned short camID, unsigned char count, unsigned int sizeX, unsigned int sizeY, const BYTE *buffer);
void CamWindow_OnCloseCam(unsigned short camID);
void CamWindow_Close();