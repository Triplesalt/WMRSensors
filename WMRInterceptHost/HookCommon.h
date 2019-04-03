#pragma once

void *FindPattern(void *regionStart, size_t regionSize, const BYTE *pattern, const BYTE *mask, size_t patternSize);
void GetImageSection(HMODULE hModule, void *&pSection, size_t &sectionLen, const char *sectionName);