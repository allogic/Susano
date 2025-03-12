#include <stdio.h>
#include <string.h>

#include <windows.h>
#include <tlhelp32.h>

/////////////////////////////////////////////////
// Macros
/////////////////////////////////////////////////

#define PRINT_LAST_ERROR() printf("0x%08X\n", GetLastError())

/////////////////////////////////////////////////
// Local Variables
/////////////////////////////////////////////////

static HANDLE sMainThread = NULL;
static HANDLE sMainThreadExitEvent = NULL;

static BOOL sRunning = TRUE;

static LPCWSTR sTargetFileName = L"Susano.dll";

static LPCWSTR sObservedPath = L"C:\\Users\\mialb\\Downloads\\Susano\\x64\\Debug";
static LPCWSTR sStreamedPath = L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Okami";

static LPCWSTR sObservedFile = L"C:\\Users\\mialb\\Downloads\\Susano\\x64\\Debug\\Susano.dll";
static LPCWSTR sStreamedFile = L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Okami\\Susano.dll";

static HMODULE sSusanoModule = NULL;

/////////////////////////////////////////////////
// Function Definition
/////////////////////////////////////////////////

INT32 WINAPI VaMainThread(PVOID UserParam);

/////////////////////////////////////////////////
// Function Implementation
/////////////////////////////////////////////////

INT32 WINAPI VaMainThread(PVOID UserParam)
{
	HANDLE directoryChangeNotify = FindFirstChangeNotificationW(sObservedPath, FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);

	while (sRunning)
	{
		UINT32 waitStatus = WaitForSingleObject(directoryChangeNotify, 1000);

		switch(waitStatus)
		{
			case WAIT_OBJECT_0:
			{
				HANDLE observedDirectory = CreateFileW(sObservedPath, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

				BYTE buffer[1024] = { 0 };
				DWORD bytesReturned = 0;
				PFILE_NOTIFY_INFORMATION notifyInfo = NULL;

				BOOL changedDetected = ReadDirectoryChangesW(observedDirectory, &buffer, sizeof(buffer), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE, &bytesReturned, NULL, NULL);

				if (changedDetected)
				{
					notifyInfo = (PFILE_NOTIFY_INFORMATION)buffer;

					do
					{
						WCHAR fileName[MAX_PATH] = { 0 };

						memcpy(fileName, notifyInfo->FileName, notifyInfo->FileNameLength);

						if (wcscmp(fileName, sTargetFileName) == 0)
						{
							switch (notifyInfo->Action)
							{
								case FILE_ACTION_ADDED:
								{
									Sleep(2000);
									CopyFileW(sObservedFile, sStreamedFile, FALSE);
									Sleep(2000);

									sSusanoModule = LoadLibraryW(sStreamedFile);

									break;
								}
								case FILE_ACTION_MODIFIED:
								{
									if (sSusanoModule)
									{
										FreeLibrary(sSusanoModule);
									
										sSusanoModule = NULL;
									}
									
									Sleep(2000);
									CopyFileW(sObservedFile, sStreamedFile, FALSE);
									Sleep(2000);
									
									sSusanoModule = LoadLibraryW(sStreamedFile);

									break;
								}
								case FILE_ACTION_REMOVED:
								{
									FreeLibrary(sSusanoModule);

									Sleep(10000);
									DeleteFileW(sStreamedFile);
									Sleep(2000);

									break;
								}
							}
						}

						notifyInfo = (PFILE_NOTIFY_INFORMATION)(((PBYTE)notifyInfo) + notifyInfo->NextEntryOffset);
					} while (notifyInfo->NextEntryOffset != 0);
				}

				CloseHandle(observedDirectory);

				FindNextChangeNotification(directoryChangeNotify);

				break;
			}
			case WAIT_ABANDONED:
			case WAIT_TIMEOUT:
			case WAIT_FAILED:
			{
				break;
			}
		}
	}

	FindCloseChangeNotification(directoryChangeNotify);

	SetEvent(sMainThreadExitEvent);

	return 0;
}

/////////////////////////////////////////////////
// Entry Point
/////////////////////////////////////////////////

BOOL APIENTRY DllMain(HMODULE Module, UINT32 CallReason, PVOID Reserved)
{
	switch (CallReason)
	{
		case DLL_PROCESS_ATTACH:
		{
			sMainThreadExitEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
			sMainThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)VaMainThread, Module, 0, NULL);

			break;
		}
		case DLL_PROCESS_DETACH:
		{
			sRunning = FALSE;

			WaitForSingleObject(sMainThreadExitEvent, INFINITE);

			CloseHandle(sMainThread);
			CloseHandle(sMainThreadExitEvent);
		}
	}

	return TRUE;
}