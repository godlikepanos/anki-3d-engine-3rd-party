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

#ifndef __DEMO_VISUAL_DEBUGER_LISTENER_H__
#define __DEMO_VISUAL_DEBUGER_LISTENER_H__

#include "toolbox_stdafx.h"
#include "DemoListenerBase.h"

#if 0
class DemoVisualDebugerListener: public DemoListenerBase
{
	public:
	DemoVisualDebugerListener(DemoEntityManager* const scene);
	~DemoVisualDebugerListener();

	virtual void PreUpdate (const NewtonWorld* const world, dFloat timestep);
	virtual void PostUpdate (const NewtonWorld* const world, dFloat timestep);

	void* m_visualDebugger;
};
#endif

#endif