#include "TrackerPlayback.h"
#include <Windows.h>
    #include <shellapi.h>
        #include <strsafe.h>

            #define IpcWindowClass L"TrackerPlayer_IpcWindow"
            #define IpcWindowTitle L"TrackerPlayer_IpcWindow_Title"
            #define IpcMutexName L"Local\\TrackerPlayer_SingleInstance"
            #define TrackerPlayerStopMessage (WM_APP + 1)

            static volatile LONG _stopRequested = 0;
            static HWND _messageWindow = NULL;

            static void ShowMessageBox(const char* title, const char* message) {
            MessageBoxA(NULL, message ? message : "Unknown error.", title ? title : "Tracker Player", MB_OK | MB_ICONERROR);
            }

            static void ShowLastErrorMessage(const char* context) {
            DWORD errorCode = GetLastError();
            CHAR systemMessage[1024] = { 0 };
            CHAR fullMessage[1536] = { 0 };
            DWORD formatFlags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
            DWORD messageLength = FormatMessageA(formatFlags, NULL, errorCode, 0, systemMessage, (DWORD) (sizeof(systemMessage) / sizeof(systemMessage[0])), NULL);

            if (messageLength == 0) {
            StringCchPrintfA(fullMessage, sizeof(fullMessage), "%s\nError code: %lu", context ? context : "Operation failed.", (unsigned long) errorCode);
            } else {
            StringCchPrintfA(fullMessage, sizeof(fullMessage), "%s\n%s\nError code: %lu", context ? context : "Operation failed.", systemMessage, (unsigned long) errorCode);
            }

            ShowMessageBox(NULL, fullMessage);
            }

            static void TRACKER_PLAYBACK_CALL OnPlaybackError(const char* message) {
            ShowMessageBox("Tracker Player", message ? message : "Unknown playback error.");
            InterlockedExchange(&_stopRequested, 1);
            }

            static LRESULT CALLBACK PlayerWindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam) {
            (void) wParam;
            (void) lParam;

            if (message == TrackerPlayerStopMessage || message == WM_CLOSE) {
            InterlockedExchange(&_stopRequested, 1);
            return 0;
            }

            return DefWindowProcW(windowHandle, message, wParam, lParam);
            }

            static int CreateMessageWindow(HINSTANCE instanceHandle) {
            WNDCLASSW windowClass;

            ZeroMemory(&windowClass, sizeof(windowClass));
            windowClass.lpfnWndProc = PlayerWindowProc;
            windowClass.hInstance = instanceHandle;
            windowClass.lpszClassName = IpcWindowClass;

            if (!RegisterClassW(&windowClass)) {
            if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            ShowLastErrorMessage("Failed to register the IPC window class.");
            return 0;
            }
            }

            _messageWindow = CreateWindowExW(
            0,
            IpcWindowClass,
            IpcWindowTitle,
            WS_OVERLAPPED,
            0,
            0,
            0,
            0,
            NULL,
            NULL,
            instanceHandle,
            NULL
            );

            if (!_messageWindow) {
            ShowLastErrorMessage("Failed to create the IPC window.");
            return 0;
            }

            return 1;
            }

            static void PumpMessages(void) {
            MSG message;

            while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            }
            }

            static int SignalRunningInstanceToStop(void) {
            HWND windowHandle = FindWindowW(IpcWindowClass, IpcWindowTitle);
            DWORD_PTR resultValue = 0;

            if (!windowHandle) {
            return 0;
            }

            if (!SendMessageTimeoutW(windowHandle, TrackerPlayerStopMessage, 0, 0, SMTO_ABORTIFHUNG, 3000, &resultValue)) {
            return 0;
            }

            return 1;
            }

            static int LoadModuleFile(const wchar_t* filePath, void** fileData, size_t* fileSize) {
            HANDLE fileHandle = INVALID_HANDLE_VALUE;
            LARGE_INTEGER sizeValue;
            DWORD bytesRead = 0;
            void* buffer = NULL;

            if (!filePath || !fileData || !fileSize) {
            ShowMessageBox(NULL, "Invalid arguments while loading the module file.");
            return 0;
            }

            fileHandle = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (fileHandle == INVALID_HANDLE_VALUE) {
            ShowLastErrorMessage("Failed to open the module file.");
            return 0;
            }

            if (!GetFileSizeEx(fileHandle, &sizeValue) || sizeValue.QuadPart <= 0 || sizeValue.QuadPart > MAXDWORD) {
                ShowMessageBox(NULL, "Failed to determine the module file size.");
                CloseHandle(fileHandle);
                return 0;
                }

                buffer = HeapAlloc(GetProcessHeap(), 0, (SIZE_T) sizeValue.QuadPart);
                if (!buffer) {
                ShowMessageBox(NULL, "Out of memory while loading the module file.");
                CloseHandle(fileHandle);
                return 0;
                }

                if (!ReadFile(fileHandle, buffer, (DWORD) sizeValue.QuadPart, &bytesRead, NULL) || bytesRead != (DWORD) sizeValue.QuadPart) {
                ShowMessageBox(NULL, "Failed to read the module file.");
                HeapFree(GetProcessHeap(), 0, buffer);
                CloseHandle(fileHandle);
                return 0;
                }

                CloseHandle(fileHandle);
                *fileData = buffer;
                *fileSize = (size_t) sizeValue.QuadPart;
                return 1;
                }

                int WINAPI wWinMain(_In_ HINSTANCE instanceHandle, _In_opt_ HINSTANCE previousInstance, _In_ PWSTR commandLine, _In_ int showCommand) {
                int argCount = 0;
                LPWSTR* arguments = NULL;
                int result = 1;
                HANDLE mutexHandle = NULL;
                int hasModuleArgument = 0;
                WCHAR modulePath[MAX_PATH] = { 0 };
                void* moduleData = NULL;
                size_t moduleSize = 0;

                (void) previousInstance;
                (void) commandLine;
                (void) showCommand;

                arguments = CommandLineToArgvW(GetCommandLineW(), &argCount);
                if (!arguments) {
                ShowLastErrorMessage("Failed to parse the command line.");
                goto Cleanup;
                }

hasModuleArgument = (argCount >= 2 && arguments[1] && arguments[1][0] != L'\0');

                    mutexHandle = CreateMutexW(NULL, FALSE, IpcMutexName);
                    if (!mutexHandle) {
                    ShowLastErrorMessage("Failed to create the single-instance mutex.");
                    goto Cleanup;
                    }

                    if (GetLastError() == ERROR_ALREADY_EXISTS) {
                    if (!hasModuleArgument) {
                    SignalRunningInstanceToStop();
                    result = 0;
                    goto Cleanup;
                    }

                    ShowMessageBox(NULL, "TrackerPlayer is already running. Starting another playback instance is not allowed.");
                    goto Cleanup;
                    }

                    if (!hasModuleArgument) {
                    ShowMessageBox(NULL, "Please pass the module file path as a command-line argument.\n\nExample: TrackerPlayer.exe Music.xm");
                    goto Cleanup;
                    }

                    if (GetFullPathNameW(arguments[1], (DWORD) (sizeof(modulePath) / sizeof(modulePath[0])), modulePath, NULL) == 0) {
                    ShowLastErrorMessage("Failed to resolve the module file path.");
                    goto Cleanup;
                    }

                    if (!CreateMessageWindow(instanceHandle)) {
                    goto Cleanup;
                    }

                    if (!LoadModuleFile(modulePath, &moduleData, &moduleSize)) {
                    goto Cleanup;
                    }

                    TrackerPlayback_SetErrorCallback(OnPlaybackError);

                    if (!TrackerPlayback_Play(moduleData, moduleSize, 1)) {
                    ShowMessageBox(NULL, "Failed to start playback.");
                    goto Cleanup;
                    }

                    while (!InterlockedCompareExchange(&_stopRequested, 0, 0)) {
                    PumpMessages();
                    if (TrackerPlayback_GetStatus() == TRACKER_PLAYBACK_STATUS_STOPPED) {
                    break;
                    }

                    Sleep(10);
                    }

                    result = 0;

                    Cleanup:
                    if (TrackerPlayback_GetStatus() != TRACKER_PLAYBACK_STATUS_STOPPED) {
                    TrackerPlayback_Stop();
                    }
                    if (moduleData) {
                    HeapFree(GetProcessHeap(), 0, moduleData);
                    moduleData = NULL;
                    }
                    if (_messageWindow) {
                    DestroyWindow(_messageWindow);
                    _messageWindow = NULL;
                    }
                    if (mutexHandle) {
                    CloseHandle(mutexHandle);
                    mutexHandle = NULL;
                    }
                    if (arguments) {
                    LocalFree(arguments);
                    arguments = NULL;
                    }

                    return result;
                    }
