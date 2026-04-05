using System;
using System.Runtime.InteropServices;

namespace TrackerPlaybackTest
{
    public enum TrackerPlaybackStatus
    {
        Stopped = 0,
        Playing = 1,
        Paused = 2,
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public delegate void TrackerPlaybackErrorCallback(string message);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void TrackerPlaybackStatusCallback(TrackerPlaybackStatus oldStatus, TrackerPlaybackStatus newStatus);

    public static class TrackerPlayback
    {
        private const string DllName = "TrackerPlayback.dll";

        [DllImport(DllName, EntryPoint = "TrackerPlayback_SetErrorCallback", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern void SetErrorCallback(TrackerPlaybackErrorCallback? callback);

        [DllImport(DllName, EntryPoint = "TrackerPlayback_SetStatusCallback", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
        public static extern void SetStatusCallback(TrackerPlaybackStatusCallback? callback);

        [DllImport(DllName, EntryPoint = "TrackerPlayback_GetStatus", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
        public static extern TrackerPlaybackStatus GetStatus();

        [DllImport(DllName, EntryPoint = "TrackerPlayback_Play", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool Play(byte[] xmData, UIntPtr xmDataSize, [MarshalAs(UnmanagedType.Bool)] bool loopForever);

        [DllImport(DllName, EntryPoint = "TrackerPlayback_Stop", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool Stop();

        [DllImport(DllName, EntryPoint = "TrackerPlayback_Pause", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool Pause();

        [DllImport(DllName, EntryPoint = "TrackerPlayback_Resume", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool Resume();

        public static bool Play(byte[] xmData, bool loopForever)
        {
            if (xmData is null)
            {
                throw new ArgumentNullException(nameof(xmData));
            }

            return Play(xmData, (UIntPtr)xmData.Length, loopForever);
        }
    }
}
