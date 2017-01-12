/////////////////////////////////////////////////////////////////////////////
// Name:        dMeshNGD.cpp
// Purpose:     
// Author:      Julio Jerez
// Modified by: 
// Created:     22/05/2010 07:45:05
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

#include "StdAfx.h"
#include "dTriangulateSelections.h"
#include "dUndoRedoSaveSelectedMesh.h"

dMeshTriangulateMesh::dMeshTriangulateMesh()
	:dPluginTool()
{
}

dMeshTriangulateMesh::~dMeshTriangulateMesh()
{
}


dMeshTriangulateMesh* dMeshTriangulateMesh::GetPlugin()
{
	static dMeshTriangulateMesh plugin;
	return &plugin;
}


bool dMeshTriangulateMesh::ReportProgress(dFloat progress, void* const handle)
{
	dPluginInterface* const interface = (dPluginInterface*) handle;
	return interface->UpdateProgress(progress);
}

bool dMeshTriangulateMesh::Execute (dPluginInterface* const interface)
{
	if (interface->HasMeshSelection (dMeshNodeInfo::GetRttiType())) {
		dScene* const scene = interface->GetScene();
		dAssert (scene);
		dScene::dTreeNode* const geometryCache = scene->FindGetGeometryCacheNode ();

		interface->Push (new dUndoRedoSaveSelectedMesh(interface));

		dSceneRender* const render = interface->GetRender();
		for (void* link = scene->GetFirstChildLink(geometryCache); link; link = scene->GetNextChildLink(geometryCache, link)) {
			dScene::dTreeNode* const node = scene->GetNodeFromLink(link);
			dNodeInfo* const info = scene->GetInfoFromNode(node);
			if (info->IsType(dMeshNodeInfo::GetRttiType())) {
				if (info->GetEditorFlags() & dNodeInfo::m_selected) {
					dMeshNodeInfo* const meshInfo = (dMeshNodeInfo*) info;
					render->InvalidateCachedDisplayList (meshInfo->GetMesh());

					//NewtonMeshApplyAngleBasedMapping (meshInfo->GetMesh(), 0, ReportProgress, interface);
					NewtonMeshTriangulate(meshInfo->GetMesh());
				}
			}
		}
	}
	return true;
}

