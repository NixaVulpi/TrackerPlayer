#include "TrackerPlaybackInternal.h"
#include <process.h>
#include <strsafe.h>

PlaybackState _playbackState = { 0 };

static float _left[BUFFER_SIZE];
static float _right[BUFFER_SIZE];
static float* const _buffers[2] = { _left, _right };
static float _interleavedBuffer[BUFFER_SIZE * 2];

static void CleanupPlaybackResourcesLocked(void);
static void NotifyStatusCallback(TrackerPlaybackStatus oldStatus, TrackerPlaybackStatus newStatus);
static void ReportFatalErrorAndReset(const char* message);
static int SetStatusLocked(TrackerPlaybackStatus newStatus, TrackerPlaybackStatus* oldStatus);

void EnsureStateInitialized(void) {
    if (!_playbackState.lockInitialized) {
        InitializeCriticalSection(&_playbackState.lock);
        _playbackState.lockInitialized = 1;
        _playbackState.status = TRACKER_PLAYBACK_STATUS_STOPPED;
    }
}

static void EnterStateLock(void) {
    EnsureStateInitialized();
    EnterCriticalSection(&_playbackState.lock);
}

static void LeaveStateLock(void) {
    LeaveCriticalSection(&_playbackState.lock);
}

static void CleanupPlaybackResourcesLocked(void) {
    if (_playbackState.streamHandle) {
        Pa_StopStream(_playbackState.streamHandle);
        Pa_CloseStream(_playbackState.streamHandle);
        _playbackState.streamHandle = NULL;
    }

    if (_playbackState.paInitialized) {
        Pa_Terminate();
        _playbackState.paInitialized = 0;
    }

    if (_playbackState.moduleHandle) {
        openmpt_module_destroy(_playbackState.moduleHandle);
        _playbackState.moduleHandle = NULL;
    }

    if (_playbackState.moduleData) {
        HeapFree(GetProcessHeap(), 0, _playbackState.moduleData);
        _playbackState.moduleData = NULL;
        _playbackState.moduleDataSize = 0;
    }

    if (_playbackState.stopEvent) {
        CloseHandle(_playbackState.stopEvent);
        _playbackState.stopEvent = NULL;
    }

    if (_playbackState.pauseEvent) {
        CloseHandle(_playbackState.pauseEvent);
        _playbackState.pauseEvent = NULL;
    }

    if (_playbackState.threadHandle) {
        CloseHandle(_playbackState.threadHandle);
        _playbackState.threadHandle = NULL;
    }

    _playbackState.threadId = 0;
    _playbackState.isInterleaved = 0;
    _playbackState.loopForever = 0;
    _playbackState.status = TRACKER_PLAYBACK_STATUS_STOPPED;
}

static void NotifyStatusCallback(TrackerPlaybackStatus oldStatus, TrackerPlaybackStatus newStatus) {
    TrackerPlaybackStatusCallback callback;

    EnterStateLock();
    callback = _playbackState.statusCallback;
    LeaveStateLock();

    if (callback) {
        callback(oldStatus, newStatus);
    }
}

static int SetStatusLocked(TrackerPlaybackStatus newStatus, TrackerPlaybackStatus* oldStatus) {
    int changed;
    TrackerPlaybackStatus previousStatus;

    previousStatus = _playbackState.status;
    changed = (previousStatus != newStatus);
    _playbackState.status = newStatus;
    if (oldStatus) {
        *oldStatus = previousStatus;
    }
    return changed;
}

static void ReportFatalErrorAndReset(const char* message) {
    TrackerPlaybackErrorCallback callback = NULL;
    HANDLE threadHandleToClose = NULL;
    TrackerPlaybackStatus oldStatus = TRACKER_PLAYBACK_STATUS_STOPPED;
    int shouldNotifyStopped = 0;

    EnterStateLock();
    callback = _playbackState.errorCallback;
    oldStatus = _playbackState.status;
    shouldNotifyStopped = (oldStatus != TRACKER_PLAYBACK_STATUS_STOPPED);
    if (_playbackState.threadHandle && _playbackState.threadId == GetCurrentThreadId()) {
        threadHandleToClose = _playbackState.threadHandle;
        _playbackState.threadHandle = NULL;
    }
    CleanupPlaybackResourcesLocked();
    if (threadHandleToClose) {
        CloseHandle(threadHandleToClose);
    }
    LeaveStateLock();

    if (callback) {
        callback(message ? message : "Unknown playback error.");
    }
    if (shouldNotifyStopped) {
        NotifyStatusCallback(oldStatus, TRACKER_PLAYBACK_STATUS_STOPPED);
    }
}

static int CopyModuleBufferLocked(const void* xmData, size_t xmDataSize) {
    void* copiedBuffer = NULL;

    if (!xmData || xmDataSize == 0) {
        return 0;
    }

    copiedBuffer = HeapAlloc(GetProcessHeap(), 0, xmDataSize);
    if (!copiedBuffer) {
        return 0;
    }

    CopyMemory(copiedBuffer, xmData, xmDataSize);
    _playbackState.moduleData = copiedBuffer;
    _playbackState.moduleDataSize = xmDataSize;
    return 1;
}

static unsigned __stdcall PlaybackThreadProc(void* context) {
    int modErr = OPENMPT_ERROR_OK;
    const char* modErrStr = NULL;
    PaError paError = paNoError;
    DWORD waitResult;
    HANDLE waitHandles[2];
    size_t sampleCount;
    int shouldLoop;
    TrackerPlaybackStatus oldStatus = TRACKER_PLAYBACK_STATUS_STOPPED;
    int shouldNotifyStopped = 0;

    (void) context;

    EnterStateLock();
    shouldLoop = _playbackState.loopForever;
    _playbackState.moduleHandle = openmpt_module_create_from_memory2(_playbackState.moduleData, _playbackState.moduleDataSize, NULL, NULL, NULL, NULL, &modErr, &modErrStr, NULL);
    LeaveStateLock();

    if (!_playbackState.moduleHandle) {
        CHAR message[1024] = { 0 };
        StringCchPrintfA(message, sizeof(message), "openmpt_module_create_from_memory2() failed. %s", modErrStr ? modErrStr : "Unknown error.");
        if (modErrStr) {
            openmpt_free_string(modErrStr);
        }
        ReportFatalErrorAndReset(message);
        return 0;
    }

    if (modErrStr) {
        openmpt_free_string(modErrStr);
        modErrStr = NULL;
    }

    if (!openmpt_module_set_repeat_count(_playbackState.moduleHandle, shouldLoop ? -1 : 0)) {
        ReportFatalErrorAndReset("Failed to configure repeat count.");
        return 0;
    }

    paError = Pa_Initialize();
    if (paError != paNoError) {
        CHAR message[512] = { 0 };
        StringCchPrintfA(message, sizeof(message), "Pa_Initialize() failed. %s", Pa_GetErrorText(paError));
        ReportFatalErrorAndReset(message);
        return 0;
    }

    EnterStateLock();
    _playbackState.paInitialized = 1;
    LeaveStateLock();

    paError = Pa_OpenDefaultStream(&_playbackState.streamHandle, 0, 2, paFloat32 | paNonInterleaved, SAMPLE_RATE, paFramesPerBufferUnspecified, NULL, NULL);
    if (paError == paSampleFormatNotSupported) {
        EnterStateLock();
        _playbackState.isInterleaved = 1;
        LeaveStateLock();
        paError = Pa_OpenDefaultStream(&_playbackState.streamHandle, 0, 2, paFloat32, SAMPLE_RATE, paFramesPerBufferUnspecified, NULL, NULL);
    }
    if (paError != paNoError || !_playbackState.streamHandle) {
        CHAR message[512] = { 0 };
        StringCchPrintfA(message, sizeof(message), "Pa_OpenDefaultStream() failed. %s", Pa_GetErrorText(paError));
        ReportFatalErrorAndReset(message);
        return 0;
    }

    paError = Pa_StartStream(_playbackState.streamHandle);
    if (paError != paNoError) {
        CHAR message[512] = { 0 };
        StringCchPrintfA(message, sizeof(message), "Pa_StartStream() failed. %s", Pa_GetErrorText(paError));
        ReportFatalErrorAndReset(message);
        return 0;
    }

    for (;;) {
        EnterStateLock();
        if (WaitForSingleObject(_playbackState.stopEvent, 0) == WAIT_OBJECT_0) {
            LeaveStateLock();
            break;
        }
        if (_playbackState.status == TRACKER_PLAYBACK_STATUS_PAUSED) {
            waitHandles[0] = _playbackState.stopEvent;
            waitHandles[1] = _playbackState.pauseEvent;
            LeaveStateLock();
            waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
            if (waitResult == WAIT_OBJECT_0) {
                break;
            }
            continue;
        }
        LeaveStateLock();

        openmpt_module_error_clear(_playbackState.moduleHandle);
        sampleCount = _playbackState.isInterleaved
            ? openmpt_module_read_interleaved_float_stereo(_playbackState.moduleHandle, SAMPLE_RATE, BUFFER_SIZE, _interleavedBuffer)
            : openmpt_module_read_float_stereo(_playbackState.moduleHandle, SAMPLE_RATE, BUFFER_SIZE, _left, _right);

        modErr = openmpt_module_error_get_last(_playbackState.moduleHandle);
        if (modErr != OPENMPT_ERROR_OK) {
            CHAR message[1024] = { 0 };
            modErrStr = openmpt_module_error_get_last_message(_playbackState.moduleHandle);
            StringCchPrintfA(message, sizeof(message), "openmpt_module_read_float_stereo() failed. %s", modErrStr ? modErrStr : "Unknown error.");
            if (modErrStr) {
                openmpt_free_string(modErrStr);
            }
            ReportFatalErrorAndReset(message);
            return 0;
        }

        if (sampleCount == 0) {
            if (_playbackState.loopForever) {
                if (openmpt_module_set_position_seconds(_playbackState.moduleHandle, 0.0) < 0.0) {
                    ReportFatalErrorAndReset("Failed to reset playback position.");
                    return 0;
                }
                continue;
            }
            break;
        }

        paError = _playbackState.isInterleaved
            ? Pa_WriteStream(_playbackState.streamHandle, _interleavedBuffer, (unsigned long) sampleCount)
            : Pa_WriteStream(_playbackState.streamHandle, _buffers, (unsigned long) sampleCount);

        if (paError == paOutputUnderflowed) {
            paError = paNoError;
        }
        if (paError != paNoError) {
            CHAR message[512] = { 0 };
            StringCchPrintfA(message, sizeof(message), "Pa_WriteStream() failed. %s", Pa_GetErrorText(paError));
            ReportFatalErrorAndReset(message);
            return 0;
        }
    }

    EnterStateLock();
    oldStatus = _playbackState.status;
    shouldNotifyStopped = (oldStatus != TRACKER_PLAYBACK_STATUS_STOPPED);
    if (_playbackState.threadId == GetCurrentThreadId()) {
        HANDLE threadHandleToClose = _playbackState.threadHandle;
        _playbackState.threadHandle = NULL;
        CleanupPlaybackResourcesLocked();
        LeaveStateLock();
        if (threadHandleToClose) {
            CloseHandle(threadHandleToClose);
        }
    } else {
        CleanupPlaybackResourcesLocked();
        LeaveStateLock();
    }

    if (shouldNotifyStopped) {
        NotifyStatusCallback(oldStatus, TRACKER_PLAYBACK_STATUS_STOPPED);
    }

    return 0;
}

TRACKER_PLAYBACK_API void TRACKER_PLAYBACK_CALL TrackerPlayback_SetErrorCallback(TrackerPlaybackErrorCallback callback) {
    EnterStateLock();
    _playbackState.errorCallback = callback;
    LeaveStateLock();
}

TRACKER_PLAYBACK_API void TRACKER_PLAYBACK_CALL TrackerPlayback_SetStatusCallback(TrackerPlaybackStatusCallback callback) {
    EnterStateLock();
    _playbackState.statusCallback = callback;
    LeaveStateLock();
}

TRACKER_PLAYBACK_API int TRACKER_PLAYBACK_CALL TrackerPlayback_Play(const void* xmData, size_t xmDataSize, int loopForever) {
    uintptr_t threadHandleValue;
    int shouldNotifyPlaying = 0;
    TrackerPlaybackStatus oldStatus = TRACKER_PLAYBACK_STATUS_STOPPED;

    EnterStateLock();
    if (_playbackState.status != TRACKER_PLAYBACK_STATUS_STOPPED) {
        LeaveStateLock();
        return 0;
    }

    if (!CopyModuleBufferLocked(xmData, xmDataSize)) {
        LeaveStateLock();
        return 0;
    }

    _playbackState.stopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    _playbackState.pauseEvent = CreateEventW(NULL, TRUE, TRUE, NULL);
    if (!_playbackState.stopEvent || !_playbackState.pauseEvent) {
        CleanupPlaybackResourcesLocked();
        LeaveStateLock();
        return 0;
    }

    _playbackState.loopForever = loopForever ? 1 : 0;
    shouldNotifyPlaying = SetStatusLocked(TRACKER_PLAYBACK_STATUS_PLAYING, &oldStatus);

    threadHandleValue = _beginthreadex(NULL, 0, PlaybackThreadProc, NULL, 0, (unsigned*) &_playbackState.threadId);
    if (threadHandleValue == 0) {
        CleanupPlaybackResourcesLocked();
        LeaveStateLock();
        return 0;
    }

    _playbackState.threadHandle = (HANDLE) threadHandleValue;
    LeaveStateLock();
    if (shouldNotifyPlaying) {
        NotifyStatusCallback(oldStatus, TRACKER_PLAYBACK_STATUS_PLAYING);
    }
    return 1;
}

TRACKER_PLAYBACK_API int TRACKER_PLAYBACK_CALL TrackerPlayback_Stop(void) {
    HANDLE threadHandle = NULL;
    DWORD threadId = 0;
    TrackerPlaybackStatus oldStatus = TRACKER_PLAYBACK_STATUS_STOPPED;
    int shouldNotifyStopped = 0;

    EnterStateLock();
    if (_playbackState.status == TRACKER_PLAYBACK_STATUS_STOPPED) {
        LeaveStateLock();
        return 0;
    }

    if (_playbackState.stopEvent) {
        SetEvent(_playbackState.stopEvent);
    }
    if (_playbackState.pauseEvent) {
        SetEvent(_playbackState.pauseEvent);
    }
    oldStatus = _playbackState.status;
    threadHandle = _playbackState.threadHandle;
    threadId = _playbackState.threadId;
    LeaveStateLock();

    if (threadHandle && threadId != GetCurrentThreadId()) {
        WaitForSingleObject(threadHandle, INFINITE);
    }

    EnterStateLock();
    shouldNotifyStopped = (_playbackState.status != TRACKER_PLAYBACK_STATUS_STOPPED && oldStatus != TRACKER_PLAYBACK_STATUS_STOPPED);
    CleanupPlaybackResourcesLocked();
    LeaveStateLock();
    if (shouldNotifyStopped) {
        NotifyStatusCallback(oldStatus, TRACKER_PLAYBACK_STATUS_STOPPED);
    }
    return 1;
}

TRACKER_PLAYBACK_API int TRACKER_PLAYBACK_CALL TrackerPlayback_Pause(void) {
    int shouldNotifyPaused;
    TrackerPlaybackStatus oldStatus = TRACKER_PLAYBACK_STATUS_STOPPED;

    EnterStateLock();
    if (_playbackState.status != TRACKER_PLAYBACK_STATUS_PLAYING || !_playbackState.pauseEvent) {
        LeaveStateLock();
        return 0;
    }

    ResetEvent(_playbackState.pauseEvent);
    if (_playbackState.streamHandle) {
        Pa_StopStream(_playbackState.streamHandle);
    }
    shouldNotifyPaused = SetStatusLocked(TRACKER_PLAYBACK_STATUS_PAUSED, &oldStatus);
    LeaveStateLock();
    if (shouldNotifyPaused) {
        NotifyStatusCallback(oldStatus, TRACKER_PLAYBACK_STATUS_PAUSED);
    }
    return 1;
}

TRACKER_PLAYBACK_API int TRACKER_PLAYBACK_CALL TrackerPlayback_Resume(void) {
    PaError paError;
    int shouldNotifyPlaying;
    TrackerPlaybackStatus oldStatus = TRACKER_PLAYBACK_STATUS_STOPPED;

    EnterStateLock();
    if (_playbackState.status != TRACKER_PLAYBACK_STATUS_PAUSED || !_playbackState.pauseEvent) {
        LeaveStateLock();
        return 0;
    }

    if (_playbackState.streamHandle) {
        paError = Pa_StartStream(_playbackState.streamHandle);
        if (paError != paNoError) {
            LeaveStateLock();
            ReportFatalErrorAndReset("Failed to resume the audio stream.");
            return 0;
        }
    }

    SetEvent(_playbackState.pauseEvent);
    shouldNotifyPlaying = SetStatusLocked(TRACKER_PLAYBACK_STATUS_PLAYING, &oldStatus);
    LeaveStateLock();
    if (shouldNotifyPlaying) {
        NotifyStatusCallback(oldStatus, TRACKER_PLAYBACK_STATUS_PLAYING);
    }
    return 1;
}

TRACKER_PLAYBACK_API TrackerPlaybackStatus TRACKER_PLAYBACK_CALL TrackerPlayback_GetStatus(void) {
    TrackerPlaybackStatus status;

    EnterStateLock();
    status = _playbackState.status;
    LeaveStateLock();

    return status;
}
