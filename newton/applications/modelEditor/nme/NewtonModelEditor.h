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

#ifndef __NEWTON_MODEL_EDITOR_H_
#define __NEWTON_MODEL_EDITOR_H_


class EditorMainMenu;
class EditorExplorer;
class EditorCommandPanel;
class EditorUVMappingTool;
class EditorRenderViewport;

#define D_MAX_PLUGINS_COUNT 128

class NewtonModelEditor: public wxFrame, public dPluginInterface
{
	public:
	enum NavigationMode
	{
		m_selectNode,
		m_translateNode,
		m_rotateNode,
		m_scaleNode,
		m_panViewport,
		m_moveViewport,
		m_zoomViewport,
		m_rotateViewport,
	};

	enum EditorIds
	{
		ID_CANVAS = wxID_HIGHEST,

		// object selection modes
		ID_CURSOR_COMMAND_MODE,
		ID_SELECT_COMMAND_MODE,
		ID_TRANSLATE_COMMAND_MODE,
		ID_ROTATE_COMMAND_MODE,
		ID_SCALE_COMMAND_MODE,

		// editor navigation modes
		//ID_VIEWPORT_MAXIMIZE,
		ID_VIEWPORT_PANNING,
		ID_VIEWPORT_MOVE,
		ID_VIEWPORT_ROTATE,
		ID_VIEWPORT_ZOOM,


		// editor menu options
		ID_EDIT_NODE_NAME,
		
		ID_CLEAR_UNDO_HISTORY,

		// view menu options
		ID_HIDE_CONTROL_PANE,
		ID_HIDE_CONTROL_PANE_COUNT = ID_HIDE_CONTROL_PANE + 16,

		//view modes
		ID_VIEW_MODES,
		ID_VIEW_MODES_LAST = ID_VIEW_MODES + 16,

		ID_SHADE_MODES,
		ID_SHADE_MODES_LAST = ID_SHADE_MODES + 16,

		ID_TOOL_PLUGINS,
		ID_MAX_TOOL_PLUGINS = ID_TOOL_PLUGINS + D_MAX_PLUGINS_COUNT,

		ID_MESH_PLUGINS,
		ID_MAX_MESH_PLUGINS = ID_MESH_PLUGINS + D_MAX_PLUGINS_COUNT,

		ID_IMPORT_PLUGINS,
		ID_MAX_IMPORT_PLUGINS = ID_IMPORT_PLUGINS + D_MAX_PLUGINS_COUNT,

		ID_EXPORT_PLUGINS,
		ID_MAX_EXPORT_PLUGINS = ID_EXPORT_PLUGINS + D_MAX_PLUGINS_COUNT,


		// options menu
//		ID_HIDE_FILE_TOOLBAR,
//		ID_HIDE_NAVIGATION_TOOLBAR,
//		ID_HIDE_COMMAND_PANEL,
//		ID_KEYBOARD_SHORCUTS,
//		ID_MODEL_PLUGINS,
//		ID_MAX_MODELS_PLUGINS = ID_MODEL_PLUGINS + D_MAX_PLUGINS_COUNT,
	};

	NewtonModelEditor(const wxString& title, const wxPoint& pos, const wxSize& size);
	~NewtonModelEditor();

	EditorExplorer* GetExplorer() const;

/*
	// GUI interface functions
	void create();

	bool IsAltDown() const;
	bool IsShiftDown() const;
	bool IsControlDown() const;
	
	long onKeyboardHandle(FXObject* sender, FXSelector id, void* eventPtr);
	
	long onEditorMode(FXObject* sender, FXSelector id, void* eventPtr); 

	long onPaint(FXObject* sender, FXSelector id, void* eventPtr);
//	long onClearViewports(FXObject* sender, FXSelector id, void* eventPtr);

	long onHideFileToolbar(FXObject* sender, FXSelector id, void* eventPtr);
	long onHideNavigationToolbar(FXObject* sender, FXSelector id, void* eventPtr);
	long onHideExplorerPanel(FXObject* sender, FXSelector id, void* eventPtr);
	long onHideCommandPanel(FXObject* sender, FXSelector id, void* eventPtr);

	long onNew(FXObject* sender, FXSelector id, void* eventPtr); 
	
	long onLoadRecentScene(FXObject* sender, FXSelector id, void* eventPtr); 

	long onLoadAsset(FXObject* sender, FXSelector id, void* eventPtr); 


	long onSelectionCommandMode(FXObject* sender, FXSelector id, void* eventPtr); 

	long onImport(FXObject* sender, FXSelector id, void* eventPtr); 
	long onExport(FXObject* sender, FXSelector id, void* eventPtr); 

	
	long onModel (FXObject* sender, FXSelector id, void* eventPtr); 


	long onAssetSelected (FXObject* sender, FXSelector id, void* eventPtr); 
//	long onAssetDeslectd (FXObject* sender, FXSelector id, void* eventPtr); 


	void LoadIcon (const char* const iconName);
	
	static void* PhysicsAlloc (int sizeInBytes);
	static void PhysicsFree (void *ptr, int sizeInBytes);

	private:
	void SaveConfig();
	void LoadConfig();
	void LoadPlugins(const char* const path);
	void PopNavigationMode();
	void PushNavigationMode(NavigationMode mode);
	void ShowNavigationMode(NavigationMode mode) const;
	EditorCanvas* GetCanvas(FXObject* const sender) const;

	FXToolBarShell* m_fileToolbarShell;
	FXToolBar* m_fileToolbar;

	FXToolBarShell* m_navigationToolbarShell;
	FXToolBar* m_navigationToolbar;


	FXToolBarShell* m_commandPanelToolbarShell;
	EditorCommandPanel* m_commandPanel;

	GLVisual* m_sharedVisual;
	FXDockSite* m_docks[4]; 
	EditMode m_editMode;
	bool m_altKey;
	bool m_shiftKey;
	bool m_controlKey;
	bool m_navigationKey;

	FXTextField* m_showNavigationMode;

	friend class EditorCanvas;
	friend class EditorMainMenu;
	friend class EditorFileToolBar;
	friend class EditorAssetExplorer;
	friend class EditorRenderViewport;
	friend class EditorNavigationToolBar;
*/
	
	int GetViewMode() const;
	int GetShadeMode() const;
	int GetNavigationMode() const;
	wxBitmap* FindIcon (const char* const iconName) const;

	bool UpdateProgress(dFloat progress) const; 

	protected:	
	DECLARE_EVENT_TABLE()

	void OnExit(wxCommandEvent& event);
	void OnNew(wxCommandEvent& event);
	void OnAbout(wxCommandEvent& event);
	void OnChangeViewMode(wxCommandEvent& event);
	void OnChangeShadeMode(wxCommandEvent& event);
	void OnChangeNavigationMode(wxCommandEvent& event);
	void OnOpenScene(wxCommandEvent& event);
	void OnSaveScene(wxCommandEvent& event);
	void OnSaveSceneAs(wxCommandEvent& event);
	void OnUndo(wxCommandEvent& event); 
	void OnRedo(wxCommandEvent& event); 
	void OnClearUndoHistory(wxCommandEvent& event); 
	void OnMesh (wxCommandEvent& event); 
	void OnTool (wxCommandEvent& event); 
	void OnImport (wxCommandEvent& event); 
	void OnExport (wxCommandEvent& event); 

	void OnHideExplorerPane (wxCommandEvent& event); 
	void OnPaneClose (wxAuiManagerEvent& event); 
	void LoadResources ();
	void DeleteResources ();

	void CreateExplorer();
	void CreateFileToolBar();
	void CreateCommandPanel();
	void CreateRenderViewPort();
	void CreateNavigationToolBar();
	void CreateObjectSelectionToolBar();
	void CreateUVMappingDialog();
	
	void CreateScene();
	void DestroyScene();
	void RefrehViewports();

	void LoadPlugins(const char* const path);
	void LoadIcon (const char* const iconName);
	virtual void RefreshExplorerEvent(bool clear) const;
	
	wxAuiManager m_mgr;
	EditorMainMenu* m_mainMenu;
	wxStatusBar* m_statusBar;
	wxAuiToolBar* m_fileToolbar;
	wxAuiToolBar* m_navigationToolbar;
	wxAuiToolBar* m_objectSelectionToolbar;
	wxChoice* m_viewMode;
	wxChoice* m_shadeMode;

	
	EditorExplorer* m_explorer;
	EditorCommandPanel* m_commandPanel;
	EditorUVMappingTool* m_uvMappingTool;
	EditorRenderViewport* m_renderViewport;
	
	
	int m_viewModeMap[ID_VIEW_MODES_LAST - ID_VIEW_MODES];
	int m_shapeModeMap[ID_SHADE_MODES_LAST - ID_SHADE_MODES];

	
	NavigationMode m_navigationMode[16];
	int m_navigationStack;

	wxString m_lastFilePath;
	wxString m_currentFileName;
	dTree<wxBitmap*, dCRCTYPE> m_icons;

	wxProgressDialog* m_currentProgress; 
};



#endif