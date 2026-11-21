#pragma once

#include <windows.h>
#include <QList>
#include <QPair>
#include <QSize>
#include <QString>
#include <QStringList>

using NameValueList = QList<QPair<QString, QString>>; // (display name, value)

namespace WinUtil {

NameValueList ListMonitors();                  // value = device name, e.g. \\.\DISPLAY1
NameValueList ListAudioDevices(bool input);    // value = endpoint id
NameValueList ListApps();                      // value = exe name, e.g. Discord.exe

HMONITOR MonitorById(const QString &id);       // empty / unknown = primary
QSize MonitorSize(HMONITOR monitor);           // physical pixels

/* Game capture targets. */
HWND FindWindowForExe(const QString &exe);     // largest visible window of that exe
HWND FindFullscreenWindow();                   // foreground window covering its whole monitor

QString ExeForWindow(HWND hwnd);
QString ExeForProcess(DWORD pid);

/* Executables that currently have an active (playing) audio session on any output device. */
QStringList AppsPlayingAudio();
bool IsProcessRunning(const QString &exe);
QStringList RunningExes(); // every running executable name, once each

} // namespace WinUtil
