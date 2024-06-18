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
// Local Variables
/////////////////////////////////////////////////

static BOOL sRunning = TRUE;

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

INT64 VaChooseCommand(LPCSTR* Commands, UINT64 CommandsLength);

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

INT64 VaChooseCommand(LPCSTR* Commands, UINT64 CommandsLength)
{
    CHAR line[0x100] = { 0 };

    printf("> ");

    if (fgets(line, 0x100, stdin) != NULL)
    {
        UINT64 lineLength = strlen(line);

        if ((lineLength > 0) && line[lineLength - 1] == '\n')
        {
            line[lineLength - 1] = '\0';
        }

        for (UINT64 i = 0; i < CommandsLength; i++)
        {
            if (strcmp(line, Commands[i]) == 0)
            {
                return i;
            }
        }

        printf("%s\n", line);
    }

    return -1;
}

/////////////////////////////////////////////////
// Entry Point
/////////////////////////////////////////////////

INT main(VOID)
{
    LPCSTR commands[] =
    {
        "exit",
        "injectHotLoader",
        "injectSusano",
        "ejectHotLoader",
        "ejectSusano",
    };

    while (sRunning)
    {
        UINT64 command = VaChooseCommand(commands, ARRAY_LENGTH(commands));

        switch (command)
        {
            case 0:
            {
                sRunning = FALSE;

                break;
            }
            case 1:
            {
                UINT32 processId = VaFindProcessId("okami.exe");

                HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);

                VaInjectModuleIntoProcess(process, "C:\\Data\\VisualStudio\\Susano\\x64\\Debug\\HotLoader.dll");

                CloseHandle(process);

                break;
            }
            case 2:
            {
                UINT32 processId = VaFindProcessId("okami.exe");

                HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);

                VaInjectModuleIntoProcess(process, "C:\\Data\\VisualStudio\\Susano\\x64\\Debug\\Susano.dll");

                CloseHandle(process);

                break;
            }
            case 3:
            {
                UINT32 processId = VaFindProcessId("okami.exe");

                HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
                HMODULE module = VaFindModuleHandle(processId, "HotLoader.dll");

                VaEjectModuleFromProcess(process, module);

                CloseHandle(process);

                break;
            }
            case 4:
            {
                UINT32 processId = VaFindProcessId("okami.exe");

                HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
                HMODULE module = VaFindModuleHandle(processId, "Susano.dll");

                VaEjectModuleFromProcess(process, module);

                CloseHandle(process);

                break;
            }
        }
    }

    return 0;
}