/*============================================================================
	UnWindowsHP2.h: Windows system-specific declarations.
============================================================================*/
#ifndef INC_UNWINDOWSHP2_H
#define INC_UNWINDOWSHP2_H
#include <string>
#define PATH(s) appWindowsPath(s)
char* appWindowsPath(const char* Path);
DWORD appGetLocalIP(void);
int stricmp(const char* s, const char* t);
int strnicmp(const char* s, const char* t, int n);
char* strupr(char* s);
void appGetGUID(void* GUID);
void HandleSignal(int Signal);
void HandleInterrupt(int Signal);
#endif
