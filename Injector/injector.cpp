#include <stdio.h>
#include <string.h>

#include <windows.h>
#include <tlhelp32.h>

/////////////////////////////////////////////////
// Macros
/////////////////////////////////////////////////

#define PRINT_LAST_ERROR() printf("0x%08X\n", GetLastError())

#define ARRAY_LENGTH(ARRAY) (sizeof(ARRAY) / sizeof((ARRAY)[0]))

#define PAGE_SIZE (0x1000)

#define ALIGN_PAGE_DOWN(VALUE) (((UINT64)VALUE) & ~((PAGE_SIZE) - 1))
#define ALIGN_PAGE_UP(VALUE) ((((UINT64)VALUE) + ((PAGE_SIZE) - 1)) & ~((PAGE_SIZE) - 1))

/////////////////////////////////////////////////
// Function Definition
/////////////////////////////////////////////////

VOID VaStartProcessSuspended(LPCSTR ExecutablePath, PPROCESS_INFORMATION ProcessInfo);

UINT32 VaFindProcessId(LPCSTR ProcessName);
UINT64 VaFindModuleBase(UINT32 ProcessId, LPCSTR ModuleName);

HMODULE VaFindModuleHandle(UINT32 ProcessId, LPCSTR ModuleName);

VOID VaReadFromMemory(HANDLE Process, UINT64 Base, UINT64 Size, PVOID Buffer);
VOID VaWriteIntoMemory(HANDLE Process, UINT64 Base, UINT64 Size, PVOID Buffer);

VOID VaInjectModuleIntoProcess(HANDLE Process, LPCSTR FilePath);
VOID VaEjectModuleFromProcess(HANDLE Process, HMODULE Module);

/////////////////////////////////////////////////
// Function Implementation
/////////////////////////////////////////////////

VOID VaStartProcessSuspended(LPCSTR ExecutablePath, PPROCESS_INFORMATION ProcessInfo)
{
	STARTUPINFO startupInfo = { 0 };

	startupInfo.cb = sizeof(startupInfo);

	CreateProcessA(NULL, (LPSTR)ExecutablePath, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &startupInfo, ProcessInfo);
}

UINT32 VaFindProcessId(LPCSTR ProcessName)
{
	UINT32 processId = 0;

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

	PROCESSENTRY32 pe32 = { 0 };
	pe32.dwSize = sizeof(PROCESSENTRY32);

	if (Process32First(snapshot, &pe32))
	{
		do
		{
			if (strcmp(ProcessName, pe32.szExeFile) == 0)
			{
				processId = pe32.th32ProcessID;
				break;
			}
		} while (Process32Next(snapshot, &pe32));
	}

	CloseHandle(snapshot);

	return processId;
}
UINT64 VaFindModuleBase(UINT32 ProcessId, LPCSTR ModuleName)
{
	UINT64 base = 0;

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, ProcessId);

	MODULEENTRY32 me32 = { 0 };
	me32.dwSize = sizeof(MODULEENTRY32);

	if (Module32First(snapshot, &me32))
	{
		do
		{
			if (strcmp(ModuleName, me32.szModule) == 0)
			{
				base = (UINT64)me32.modBaseAddr;
				break;
			}
		} while (Module32Next(snapshot, &me32));
	}

	CloseHandle(snapshot);

	return base;
}

HMODULE VaFindModuleHandle(UINT32 ProcessId, LPCSTR ModuleName)
{
	HMODULE module = 0;

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, ProcessId);

	MODULEENTRY32 me32 = { 0 };
	me32.dwSize = sizeof(MODULEENTRY32);

	if (Module32First(snapshot, &me32))
	{
		do
		{
			if (strcmp(ModuleName, me32.szModule) == 0)
			{
				module = me32.hModule;
				break;
			}
		} while (Module32Next(snapshot, &me32));
	}

	CloseHandle(snapshot);

	return module;
}

VOID VaReadFromMemory(HANDLE Process, UINT64 Base, UINT64 Size, PVOID Buffer)
{
	UINT64 pageBase = ALIGN_PAGE_DOWN(Base);
	UINT64 pageSize = ALIGN_PAGE_UP(Size);

	DWORD oldProtect = 0;

	if (VirtualProtectEx(Process, (PVOID)pageBase, pageSize, PAGE_EXECUTE_READWRITE, &oldProtect))
	{
		ReadProcessMemory(Process, (PVOID)Base, Buffer, Size, NULL);
		VirtualProtectEx(Process, (PVOID)pageBase, pageSize, oldProtect, &oldProtect);
	}
}
VOID VaWriteIntoMemory(HANDLE Process, UINT64 Base, UINT64 Size, PVOID Buffer)
{
	UINT64 pageBase = ALIGN_PAGE_DOWN(Base);
	UINT64 pageSize = ALIGN_PAGE_UP(Size);

	DWORD oldProtect = 0;

	if (VirtualProtectEx(Process, (PVOID)pageBase, pageSize, PAGE_EXECUTE_READWRITE, &oldProtect))
	{
		WriteProcessMemory(Process, (PVOID)Base, Buffer, Size, NULL);
		VirtualProtectEx(Process, (PVOID)pageBase, pageSize, oldProtect, &oldProtect);
	}
}

VOID VaInjectModuleIntoProcess(HANDLE Process, LPCSTR FilePath)
{
	PVOID modulePathBase = VirtualAllocEx(Process, NULL, MAX_PATH, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

	WriteProcessMemory(Process, modulePathBase, FilePath, strlen(FilePath), NULL);

	HMODULE kernel32 = GetModuleHandleA("Kernel32");
	PVOID loadLibraryProc = GetProcAddress(kernel32, "LoadLibraryA");

	HANDLE thread = CreateRemoteThread(Process, NULL, 0, (LPTHREAD_START_ROUTINE)loadLibraryProc, modulePathBase, 0, NULL);

	WaitForSingleObject(thread, INFINITE);

	CloseHandle(thread);

	VirtualFreeEx(Process, modulePathBase, 0, MEM_RELEASE);
}
VOID VaEjectModuleFromProcess(HANDLE Process, HMODULE Module)
{
	HMODULE kernel32 = GetModuleHandleA("Kernel32");
	PVOID freeLibraryProc = GetProcAddress(kernel32, "FreeLibrary");

	HANDLE thread = CreateRemoteThread(Process, NULL, 0, (LPTHREAD_START_ROUTINE)freeLibraryProc, Module, 0, NULL);

	WaitForSingleObject(thread, INFINITE);

	CloseHandle(thread);
}

/////////////////////////////////////////////////
// Entry Point
/////////////////////////////////////////////////

INT main(INT Argc, CHAR** Argv, CHAR** ENVP)
{
	if (strcmp("Inject", Argv[1]) == 0)
	{
		UINT32 processId = VaFindProcessId(Argv[2]);

		HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);

		VaInjectModuleIntoProcess(process, Argv[3]);

		CloseHandle(process);
	}
	else if (strcmp("Eject", Argv[1]) == 0)
	{
		UINT32 processId = VaFindProcessId(Argv[2]);

		HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
		HMODULE module = VaFindModuleHandle(processId, Argv[3]);

		VaEjectModuleFromProcess(process, module);

		CloseHandle(process);
	}

	return 0;
}