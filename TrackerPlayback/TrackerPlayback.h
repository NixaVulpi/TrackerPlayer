#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(TRACKERPLAYBACK_EXPORTS)
#define TRACKER_PLAYBACK_CALL __cdecl
#define TRACKER_PLAYBACK_API __declspec(dllexport)
#else
#define TRACKER_PLAYBACK_CALL
#define TRACKER_PLAYBACK_API
#endif

typedef enum TrackerPlaybackStatus {
    TRACKER_PLAYBACK_STATUS_STOPPED = 0,
    TRACKER_PLAYBACK_STATUS_PLAYING = 1,
    TRACKER_PLAYBACK_STATUS_PAUSED = 2
} TrackerPlaybackStatus;

typedef void (TRACKER_PLAYBACK_CALL *TrackerPlaybackErrorCallback)(const char* message);
typedef void (TRACKER_PLAYBACK_CALL *TrackerPlaybackStatusCallback)(TrackerPlaybackStatus oldStatus, TrackerPlaybackStatus newStatus);

TRACKER_PLAYBACK_API void TRACKER_PLAYBACK_CALL TrackerPlayback_SetErrorCallback(TrackerPlaybackErrorCallback callback);
TRACKER_PLAYBACK_API void TRACKER_PLAYBACK_CALL TrackerPlayback_SetStatusCallback(TrackerPlaybackStatusCallback callback);
TRACKER_PLAYBACK_API TrackerPlaybackStatus TRACKER_PLAYBACK_CALL TrackerPlayback_GetStatus(void);
TRACKER_PLAYBACK_API int TRACKER_PLAYBACK_CALL TrackerPlayback_Play(const void* xmData, size_t xmDataSize, int loopForever);
TRACKER_PLAYBACK_API int TRACKER_PLAYBACK_CALL TrackerPlayback_Stop(void);
TRACKER_PLAYBACK_API int TRACKER_PLAYBACK_CALL TrackerPlayback_Pause(void);
TRACKER_PLAYBACK_API int TRACKER_PLAYBACK_CALL TrackerPlayback_Resume(void);

#ifdef __cplusplus
}
#endif
