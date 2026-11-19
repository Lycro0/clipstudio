#pragma once

#include <windows.h>
#include <QList>
#include <QPair>
#include <QSize>
#include <QString>

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

} // namespace WinUtil
