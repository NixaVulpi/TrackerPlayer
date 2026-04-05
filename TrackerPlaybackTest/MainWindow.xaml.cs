using Microsoft.Win32;

using System;
using System.IO;
using System.Windows;

namespace TrackerPlaybackTest
{
    public partial class MainWindow: Window
    {
        private byte[]? _xmData;
        private readonly TrackerPlaybackErrorCallback _errorCallback;
        private readonly TrackerPlaybackStatusCallback _statusCallback;

        public MainWindow()
        {
            InitializeComponent();

            _errorCallback = OnPlaybackError;
            _statusCallback = OnPlaybackStatusChanged;

            TrackerPlayback.SetErrorCallback(_errorCallback);
            TrackerPlayback.SetStatusCallback(_statusCallback);

            RefreshStatus(TrackerPlayback.GetStatus());
            AppendLog("程序已启动。");
        }

        private void BrowseButton_Click(object sender, RoutedEventArgs e)
        {
            OpenFileDialog openFileDialog = new OpenFileDialog
            {
                Filter = "Tracker Modules|*.xm;*.it;*.mod;*.s3m|All Files|*.*",
                Title = "选择模块文件"
            };

            if (openFileDialog.ShowDialog(this) != true)
            {
                return;
            }

            try
            {
                _xmData = File.ReadAllBytes(openFileDialog.FileName);
                FilePathTextBox.Text = openFileDialog.FileName;
                AppendLog($"已加载文件: {openFileDialog.FileName}");
            }
            catch (Exception ex)
            {
                AppendLog($"加载文件失败: {ex.Message}");
                MessageBox.Show(this, ex.Message, "读取文件失败", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private void PlayButton_Click(object sender, RoutedEventArgs e)
        {
            if (_xmData is null || _xmData.Length == 0)
            {
                MessageBox.Show(this, "请先选择一个模块文件。", "提示", MessageBoxButton.OK, MessageBoxImage.Information);
                return;
            }

            bool success = TrackerPlayback.Play(_xmData, LoopCheckBox.IsChecked == true);
            AppendLog(success ? "调用播放成功。" : "调用播放失败。可能已经在播放中。");
            RefreshStatus(TrackerPlayback.GetStatus());
        }

        private void PauseButton_Click(object sender, RoutedEventArgs e)
        {
            bool success = TrackerPlayback.Pause();
            AppendLog(success ? "暂停成功。" : "暂停失败。当前可能不在播放中。");
            RefreshStatus(TrackerPlayback.GetStatus());
        }

        private void ResumeButton_Click(object sender, RoutedEventArgs e)
        {
            bool success = TrackerPlayback.Resume();
            AppendLog(success ? "继续播放成功。" : "继续播放失败。当前可能不在暂停状态。");
            RefreshStatus(TrackerPlayback.GetStatus());
        }

        private void StopButton_Click(object sender, RoutedEventArgs e)
        {
            bool success = TrackerPlayback.Stop();
            AppendLog(success ? "停止成功。" : "停止失败。当前可能已经停止。");
            RefreshStatus(TrackerPlayback.GetStatus());
        }

        private void OnPlaybackError(string message)
        {
            Dispatcher.BeginInvoke(new Action(() =>
            {
                AppendLog($"错误: {message}");
                RefreshStatus(TrackerPlayback.GetStatus());
                MessageBox.Show(this, message, "播放错误", MessageBoxButton.OK, MessageBoxImage.Error);
            }));
        }

        private void OnPlaybackStatusChanged(TrackerPlaybackStatus oldStatus, TrackerPlaybackStatus newStatus)
        {
            Dispatcher.BeginInvoke(new Action(() =>
            {
                AppendLog($"状态变化: {oldStatus} -> {newStatus}");
                RefreshStatus(newStatus);
            }));
        }

        private void RefreshStatus(TrackerPlaybackStatus status)
        {
            StatusTextBlock.Text = status.ToString();
            switch (status)
            {
                case TrackerPlaybackStatus.Stopped:
                    BrowseButton.IsEnabled = true;
                    PlayButton.IsEnabled = true;
                    PauseButton.IsEnabled = false;
                    ResumeButton.IsEnabled = false;
                    StopButton.IsEnabled = false;
                    LoopCheckBox.IsEnabled = true;
                    break;
                case TrackerPlaybackStatus.Paused:
                    BrowseButton.IsEnabled = false;
                    LoopCheckBox.IsEnabled = false;
                    PlayButton.IsEnabled = false;
                    PauseButton.IsEnabled = false;
                    ResumeButton.IsEnabled = true;
                    LoopCheckBox.IsEnabled = false;
                    break;
                case TrackerPlaybackStatus.Playing:
                    BrowseButton.IsEnabled = false;
                    PlayButton.IsEnabled = false;
                    PauseButton.IsEnabled = true;
                    ResumeButton.IsEnabled = false;
                    StopButton.IsEnabled = true;
                    LoopCheckBox.IsEnabled = false;
                    break;
            }
        }

        private void AppendLog(string message)
        {
            string line = $"[{DateTime.Now:HH:mm:ss}] {message}{Environment.NewLine}";
            LogTextBox.AppendText(line);
            LogTextBox.ScrollToEnd();
        }

        protected override void OnClosed(EventArgs e)
        {
            TrackerPlayback.SetErrorCallback(null);
            TrackerPlayback.SetStatusCallback(null);
            TrackerPlayback.Stop();
            base.OnClosed(e);
        }
    }
}
