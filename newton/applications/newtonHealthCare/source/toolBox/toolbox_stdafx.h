/* Copyright (c) <2009> <Newton Game Dynamics>
* 
* This software is provided 'as-is', without any express or implied
* warranty. In no event will the authors be held liable for any damages
* arising from the use of this software.
* 
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely
*/

// stdafx.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//      are changed infrequently


#if !defined(AFX_STDAFX_H__AE78B9E2_A5B8_11D4_A1FB_00500C0076C8_H)
#define AFX_STDAFX_H__AE78B9E2_A5B8_11D4_A1FB_00500C0076C8_H

// Insert your headers here
#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers


typedef char dInt8;
typedef unsigned char dUnsigned8;

typedef short dInt16;
typedef unsigned short dUnsigned16;

typedef int dInt32;
typedef unsigned dUnsigned32;
typedef unsigned int dUnsigned32;


typedef long long unsigned64;


#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
#include <ctype.h>

//#define USE_TEST_SERIALIZATION
#define USE_STATIC_MESHES_DEBUG_COLLISION
//#define USE_TEST_ALL_FACE_USER_RAYCAST_CALLBACK


#ifdef _MSC_VER
	#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
	#include <windows.h>
	#include <Commctrl.h>
	#include <stdlib.h>
	#include <malloc.h>
	#include <memory.h>
	#include <time.h>
	#include <tchar.h>
	#include <crtdbg.h>
	#include <GL/glew.h>
	#include <GL/wglew.h>
	#include <gl/gl.h>
	#include <gl/glu.h>
#endif
	
#ifdef _POSIX_VER
	#include <unistd.h>
	#include <time.h>
	#include <GL/glew.h>
	#include <GL/glxew.h>
#endif

#ifdef _MACOSX_VER
	#include <CoreFoundation/CoreFoundation.h> 
	#include <unistd.h>
	#include <OpenGL/gl.h>
	#include <OpenGL/glu.h>
//	#include <GLEW/glew.h>
#endif


#ifdef _MSC_VER
	#pragma warning (disable: 4100) //unreferenced formal parameter
	#pragma warning (disable: 4505) //unreferenced local function has been removed
	#pragma warning (disable: 4201) //nonstandard extension used : nameless struct/union
	#pragma warning (disable: 4127) //conditional expression is constant

	#if (_MSC_VER >= 1400)
		#pragma warning (disable: 4996) // for 2005 users declared deprecated
	#endif
#endif


// gui library includes
#include <fx.h>
#include <fx3d.h>
//#include <FXApp.h>

// sound library
#include <fmod.h>
#include <fmod_errors.h>

// SDK includes
#include <Newton.h>
#include <dVector.h>
#include <dMatrix.h>
#include <dQuaternion.h>
#include <dMathDefines.h>
#include <NewtonDebuggerServer.h>

// AI wrapper 
#include <dAI.h>
#include <dAIAgent.h>
#include <dAIAgentState.h>
#include <dAIAgentVehicleController.h>
#include <dAIAgentCharacterController.h>


//#include <CustomJointLibraryStdAfx.h>

#include <dCRC.h>
#include <dHeap.h>
#include <dList.h>
#include <dTree.h>
#include <dRtti.h>
#include <dClassInfo.h>
#include <dRefCounter.h>
#include <dBaseHierarchy.h>

#include "dPluginStdafx.h"
#include "dScene.h"
#include "dCameraNodeInfo.h"
#include "dRootNodeInfo.h"
#include "dBoneNodeInfo.h"
#include "dSceneNodeInfo.h"
#include "dMeshNodeInfo.h"
#include "dTextureNodeInfo.h"
#include "dMaterialNodeInfo.h"
#include "dRigidbodyNodeInfo.h"
#include "dCollisionNodeInfo.h"
#include "dCollisionBoxNodeInfo.h"
#include "dCollisionSphereNodeInfo.h"
#include "dCollisionConvexHullNodeInfo.h"
#include "dGeometryNodeSkinModifierInfo.h"





#ifdef __USE_DOUBLE_PRECISION__
	#define glMultMatrix(x) glMultMatrixd(x)
	#define glLoadMatrix(x) glMultMatrixd(x)
//	#define glGetFloat(x,y) glGetDoublev(x,(GLdouble *)y) 
#else
	#define glMultMatrix(x) glMultMatrixf(x)
	#define glLoadMatrix(x) glMultMatrixf(x)
	#define glGetFloat(x,y) glGetFloatv(x,(dFloat  *)y) 
#endif



#ifndef _MSC_VER
	#ifndef stricmp
		#define stricmp strcasecmp
	#endif

	#ifndef strlwr
		inline char *_strlwr_ (char *a) 
		{ 
			char *ret = a; 
			while (*a != '\0') 
			{ 
				//if (isupper (*a)) 
				*a = char (tolower (*a)); 
				++a; 
			} 
			return ret; 
		}

		#define strlwr(a) _strlwr_ (a) 
	#endif
#endif


#ifndef min
#define min(a,b) ((a < b) ? a : b)
#endif

#ifndef max
#define max(a,b) ((a > b) ? a : b)
#endif


#define dRAND_MAX		0x0fffffff

unsigned dRand ();


// for some reason specifying a relative does not seem to work in Linus
// and i have to specify a absolute path
// #define ASSETS_PATH "."
void GetWorkingFileName (const char* const name, char* const outPathName);


// little Indian/big Indian conversion
unsigned SWAP_INT32(unsigned x);
unsigned short SWAP_INT16(unsigned short x);
void SWAP_FLOAT32_ARRAY (void* const array, dInt32 count);



#endif 

