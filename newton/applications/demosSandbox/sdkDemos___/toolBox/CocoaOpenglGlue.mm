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


#import <Foundation/Foundation.h>

void wglSwapIntervalEXT (void* const context)
{
    // julio added this to unlock opengl vsync.
    GLint swapInt = 0;
    [(NSOpenGLContext*) context setValues:&swapInt forParameter:NSOpenGLCPSwapInterval];
}
