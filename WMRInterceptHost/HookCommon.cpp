#include "stdafx.h"
#include "HookCommon.h"

void *FindPattern(void *regionStart, size_t regionSize, const BYTE *pattern, const BYTE *mask, size_t patternSize)
{
	void *regionEnd = (void*)((UINT_PTR)regionStart + regionSize);
	for (BYTE *curLoc = ((BYTE*)regionStart); (UINT_PTR)(curLoc + patternSize) <= (UINT_PTR)regionEnd; curLoc++)
	{
		bool found = true;
		for (size_t i = 0; i < patternSize; i++)
		{
			if ((curLoc[i] & mask[i]) != (pattern[i] & mask[i]))
			{
				found = false;
				break;
			}
		}
		if (found)
			return curLoc;
	}
	return nullptr;
}

void GetImageSection(HMODULE hModule, void *&pSection, size_t &sectionLen, const char *sectionName)
{
	pSection = nullptr;
	sectionLen = 0;

	PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hModule;
	PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((UINT_PTR)dosHeader + dosHeader->e_lfanew);
	IMAGE_SECTION_HEADER *sectionHeaders = (IMAGE_SECTION_HEADER*)((UINT_PTR)ntHeaders + sizeof(IMAGE_NT_HEADERS));
	for (unsigned int i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++)
	{
		if (!strncmp((char*)sectionHeaders[i].Name, sectionName, 8))
		{
			pSection = (void*)((UINT_PTR)hModule + sectionHeaders[i].VirtualAddress);
			sectionLen = sectionHeaders[i].Misc.VirtualSize;
			break;
		}
	}
}