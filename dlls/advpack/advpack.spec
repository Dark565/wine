@ stdcall AddDelBackupEntry(str str str long)
@ stdcall AdvInstallFile(long str str str str long long)
@ stub CloseINFEngine
@ stdcall DelNode(str long)
@ stdcall DelNodeRunDLL32(ptr ptr str long)
@ stdcall -private DllMain(long long ptr)
@ stdcall DoInfInstall(ptr)
@ stdcall ExecuteCab(ptr ptr ptr)
@ stdcall ExtractFiles(str str long ptr ptr long)
@ stdcall FileSaveMarkNotExist(str str str)
@ stdcall FileSaveRestore(ptr str str str long)
@ stdcall FileSaveRestoreOnINF(ptr str str str str str long)
@ stdcall GetVersionFromFile(str ptr ptr long)
@ stdcall GetVersionFromFileEx(str ptr ptr long)
@ stdcall IsNTAdmin(long ptr)
@ stdcall LaunchINFSection(ptr ptr str long)
@ stdcall LaunchINFSectionEx(ptr ptr str long)
@ stdcall NeedReboot(long)
@ stdcall NeedRebootInit()
@ stub OpenINFEngine
@ stub RebootCheckOnInstall
@ stdcall RegInstall(ptr str ptr)
@ stdcall RegRestoreAll(ptr str long)
@ stdcall RegSaveRestore(ptr str long str str str long)
@ stdcall RegSaveRestoreOnINF(ptr str str str long long long)
@ stdcall RegisterOCX(ptr ptr str long)
@ stdcall RunSetupCommand(long str str str str ptr long ptr)
@ stub SetPerUserSecValues
@ stdcall TranslateInfString(str str str str ptr long ptr ptr)
@ stub TranslateInfStringEx
@ stub UserInstStubWrapper
@ stub UserUnInstStubWrapper
