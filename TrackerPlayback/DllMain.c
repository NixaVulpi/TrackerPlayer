#include "TrackerPlaybackInternal.h"

BOOL WINAPI DllMain(HINSTANCE instanceHandle, DWORD reason, LPVOID reserved) {
    (void) instanceHandle;
    (void) reserved;

    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(instanceHandle);
            EnsureStateInitialized();
            break;

        case DLL_PROCESS_DETACH:
            if (_playbackState.lockInitialized) {
                DeleteCriticalSection(&_playbackState.lock);
                _playbackState.lockInitialized = 0;
            }
            break;
    }

    return TRUE;
}
