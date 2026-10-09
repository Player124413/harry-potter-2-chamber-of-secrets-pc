/*=============================================================================
	FFileManagerPlatformHP2.h: Native file manager selected for HP2 targets.
=============================================================================*/
#ifndef INC_FFILEMANAGERPLATFORMHP2_H
#define INC_FFILEMANAGERPLATFORMHP2_H

#if HP2_WINDOWS
#include "FFileManagerWindowsHP2.h"
using FFileManagerPlatformHP2 = FFileManagerWindowsHP2;
#else
#include "FFileManagerUnix.h"
using FFileManagerPlatformHP2 = FFileManagerUnix;
#endif

#endif
