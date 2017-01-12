/////////////////////////////////////////////////////////////////////////////
// Name:        dPluginScene.h
// Purpose:     
// Author:      Julio Jerez
// Modified by: 
// Created:     22/05/2010 08:02:08
// RCS-ID:      
// Copyright:   Copyright (c) <2010> <Newton Game Dynamics>
// License:     
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
// 
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely
/////////////////////////////////////////////////////////////////////////////

#ifndef _D_PLUGIN_SCENE_H_
#define _D_PLUGIN_SCENE_H_

#include "dPluginUtils.h"


#define D_EXPLORER_INFO	"editorExplorerInfo"
class dPluginInterface;

class dPluginSceneRegisterClass
{
	public:
	dPluginSceneRegisterClass(const char* const className, const dNodeInfo* const singletonClass)
	{
		dNodeInfo::ReplaceSingletonClass (className, singletonClass);
	}
};

#define D_EDITOR_SCENE_REGISTER_CLASS(className) \
	D_IMPLEMENT_CLASS_NODE(className) \
	static dPluginSceneRegisterClass __##className(className::BaseClassName(), &className::GetSingleton())


class dPluginScene: public dScene
{
	public:
	DPLUGIN_API dPluginScene(NewtonWorld* const newton);
	DPLUGIN_API dPluginScene(const dPluginScene& scene);
	DPLUGIN_API virtual ~dPluginScene(void);

	DPLUGIN_API virtual void RenderWireframe (dSceneRender* const render);
	DPLUGIN_API virtual void RenderFlatShaded (dSceneRender* const render);
	DPLUGIN_API virtual void RenderSolidWireframe (dSceneRender* const render);
	DPLUGIN_API virtual void RenderWireframeSelection (dSceneRender* const render);


	DPLUGIN_API virtual void UpdateAllOOBB ();


	DPLUGIN_API virtual bool Deserialize (const char* const fileName);
	DPLUGIN_API virtual void Serialize (const char* const fileName);
	private:
	virtual void RenderWireframeSceneNode (dSceneRender* const render, dScene::dTreeNode* const sceneNode);
	virtual void RenderFlatShadedSceneNode (dSceneRender* const render, dScene::dTreeNode* const sceneNode);
	virtual void RenderSelectedSceneNodes (dSceneRender* const render, dScene::dTreeNode* const sceneNode);
	

	int IncLRU();
	int m_lru;
};

#endif