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

#include <toolbox_stdafx.h>
#include "DemoEntityManager.h"

// memory allocation for Newton
static void* PhysicsAlloc (int sizeInBytes)
{
	//	m_totalMemoryUsed += sizeInBytes;
	return new char[sizeInBytes];
}

// memory free use by the engine
static void PhysicsFree (void* ptr, int sizeInBytes)
{
	//	m_totalMemoryUsed -= sizeInBytes;
	delete[] (char*)ptr;
}



int main(int, char**)
{
	// Set the memory allocation function before creation the newton world
	// this is the only function that can be called before the creation of the newton world.
	// it should be called once, and the the call is optional 
	NewtonSetMemorySystem (PhysicsAlloc, PhysicsFree);

	DemoEntityManager demos;

	demos.Run();

    return 0;
}

