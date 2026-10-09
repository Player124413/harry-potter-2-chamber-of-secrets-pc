/*=============================================================================
	XOpenGlDrv.h: Unreal OpenGL support header.
	Copyright 2014-2016 Oldunreal

	Revision history:
		* Created by Smirftsch

=============================================================================*/

/*-----------------------------------------------------------------------------
	Includes.
-----------------------------------------------------------------------------*/

#ifndef _INCL_XOPENGLDRV_H_
#define _INCL_XOPENGLDRV_H_

#ifdef WIN32
#include <windows.h>
#endif
#include <cmath>
#ifndef WIN32
using std::isnan;
using std::isfinite;
#endif
#include "Engine.h"
#include "UnRender.h"

#ifndef XOPENGL_WGL
#if defined(_WIN32) && !XOPENGL_USE_SDL
#define XOPENGL_WGL 1
#else
#define XOPENGL_WGL 0
#endif
#endif

#if !XOPENGL_WGL && !defined(SDL2BUILD)
# define SDL2BUILD 1
#endif

//#define AUTO_INITIALIZE_REGISTRANTS_OPENGLDRV UXOpenGLRenderDevice::StaticClass();
extern "C" { void autoInitializeRegistrantsXOpenGLDrv(void); }
#define AUTO_INITIALIZE_REGISTRANTS_XOPENGLDRV autoInitializeRegistrantsXOpenGLDrv();

#endif
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
