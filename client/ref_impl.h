// Autogenerated file: do not modify

static refImport_t ri = {
	sizeof(refImport_t),
	API_VERSION,
	Cmd_AddCommand,
	Cmd_RemoveCommand,
	Cmd_Argc,
	Cmd_Argv,
	Cmd_Args,
	Cvar_Get,
	Cvar_GetVars,
	Cvar_Set,
	Cvar_SetValue,
	Cvar_SetInteger,
	Cvar_VariableString,
	Cvar_VariableValue,
	Cvar_VariableInt,
	Cvar_Clamp,
	Cvar_ClampName,
	Z_Malloc,
	Z_Free,
	CreateMemoryChain,
	AllocChainBlock,
	FreeMemoryChain,
	Hunk_Begin,
	Hunk_Alloc,
	Hunk_End,
	Hunk_Free,
	Sys_Milliseconds,
	Com_Printf,
	Com_DPrintf,
	Com_WPrintf,
	Com_FatalError,
	Com_DropError
#ifdef _WIN32
,
	win32ExceptFilter2
#endif
,
	appUnwindPrefix,
	appUnwindThrow,
	FS_FileExists,
	FS_ListFiles,
	FS_LoadFile,
	FS_FreeFile,
	FS_Gamedir,
	FS_CreatePath,
	FS_CopyFile,
	FS_CopyFiles,
	FS_RemoveFiles,
	MatchWildcard,
	MatchWildcard2,
	Vid_GetModeInfo,
	Vid_MenuInit,
	ImageExists,
	LoadPCX,
	LoadTGA,
	LoadJPG,
	WriteTGA,
	WriteJPG,
	LoadBspFile,
	CM_BoxTrace,
	CM_TransformedBoxTrace,
	CM_TransformedBoxTrace2,
	CM_BrushTrace,
	CM_RefineBrushTrace
#ifdef _WIN32
,
	Vid_CreateWindow,
	Vid_DestroyWindow
#else
,
	Vid_NewWindow
#endif

};

