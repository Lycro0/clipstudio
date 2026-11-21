#include "WinUtil.hpp"

#include <QFileInfo>
#include <QObject>
#include <QSet>
#include <QSize>

#include <unknwn.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <tlhelp32.h>
#include <functiondiscoverykeys_devpkey.h>
#include <algorithm>

#include <wrl/client.h>

namespace WinUtil {

NameValueList ListMonitors()
{
	NameValueList list;
	EnumDisplayMonitors(
		nullptr, nullptr,
		[](HMONITOR mon, HDC, LPRECT, LPARAM param) -> BOOL {
			auto *l = reinterpret_cast<NameValueList *>(param);
			MONITORINFOEXW mi = {};
			mi.cbSize = sizeof(mi);
			GetMonitorInfoW(mon, &mi);
			QSize s = MonitorSize(mon);
			QString name = QString("Display %1 (%2x%3)%4")
					       .arg(l->size() + 1)
					       .arg(s.width())
					       .arg(s.height())
					       .arg((mi.dwFlags & MONITORINFOF_PRIMARY) ? " - primary" : "");
			l->append({name, QString::fromWCharArray(mi.szDevice)});
			return TRUE;
		},
		reinterpret_cast<LPARAM>(&list));
	return list;
}

HMONITOR MonitorById(const QString &id)
{
	struct Ctx {
		std::wstring want;
		HMONITOR found = nullptr;
	} ctx{id.toStdWString()};
	if (!id.isEmpty()) {
		EnumDisplayMonitors(
			nullptr, nullptr,
			[](HMONITOR mon, HDC, LPRECT, LPARAM param) -> BOOL {
				auto *c = reinterpret_cast<Ctx *>(param);
				MONITORINFOEXW mi = {};
				mi.cbSize = sizeof(mi);
				GetMonitorInfoW(mon, &mi);
				if (c->want == mi.szDevice) {
					c->found = mon;
					return FALSE;
				}
				return TRUE;
			},
			reinterpret_cast<LPARAM>(&ctx));
	}
	if (ctx.found)
		return ctx.found;
	POINT origin = {0, 0};
	return MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
}

QSize MonitorSize(HMONITOR monitor)
{
	MONITORINFOEXW mi = {};
	mi.cbSize = sizeof(mi);
	GetMonitorInfoW(monitor, &mi);
	DEVMODEW dm = {};
	dm.dmSize = sizeof(dm);
	if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm))
		return QSize((int)dm.dmPelsWidth, (int)dm.dmPelsHeight);
	return QSize(mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top);
}

NameValueList ListAudioDevices(bool input)
{
	NameValueList list;
	list.append({QObject::tr("Default"), "default"});
	Microsoft::WRL::ComPtr<IMMDeviceEnumerator> en;
	if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en))))
		return list;
	Microsoft::WRL::ComPtr<IMMDeviceCollection> col;
	if (FAILED(en->EnumAudioEndpoints(input ? eCapture : eRender, DEVICE_STATE_ACTIVE, &col)))
		return list;
	UINT n = 0;
	col->GetCount(&n);
	for (UINT i = 0; i < n; i++) {
		Microsoft::WRL::ComPtr<IMMDevice> dev;
		if (FAILED(col->Item(i, &dev)))
			continue;
		LPWSTR id = nullptr;
		dev->GetId(&id);
		Microsoft::WRL::ComPtr<IPropertyStore> props;
		QString name;
		if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
			PROPVARIANT pv;
			PropVariantInit(&pv);
			if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR)
				name = QString::fromWCharArray(pv.pwszVal);
			PropVariantClear(&pv);
		}
		if (id) {
			list.append({name.isEmpty() ? QString::fromWCharArray(id) : name, QString::fromWCharArray(id)});
			CoTaskMemFree(id);
		}
	}
	return list;
}

QString ExeForWindow(HWND hwnd)
{
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	return ExeForProcess(pid);
}

QString ExeForProcess(DWORD pid)
{
	HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!h)
		return QString();
	wchar_t path[MAX_PATH * 2];
	DWORD len = MAX_PATH * 2;
	QString exe;
	if (QueryFullProcessImageNameW(h, 0, path, &len))
		exe = QFileInfo(QString::fromWCharArray(path, (int)len)).fileName();
	CloseHandle(h);
	return exe;
}

static bool IsCandidateWindow(HWND hwnd)
{
	if (!IsWindowVisible(hwnd) || IsIconic(hwnd))
		return false;
	if (GetWindow(hwnd, GW_OWNER))
		return false;
	LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
	if (ex & WS_EX_TOOLWINDOW)
		return false;
	if (GetWindowTextLengthW(hwnd) == 0)
		return false;
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	return pid != GetCurrentProcessId();
}

NameValueList ListApps()
{
	struct Ctx {
		NameValueList list;
		QSet<QString> seen;
	} ctx;
	EnumWindows(
		[](HWND hwnd, LPARAM param) -> BOOL {
			auto *c = reinterpret_cast<Ctx *>(param);
			if (!IsCandidateWindow(hwnd))
				return TRUE;
			QString exe = ExeForWindow(hwnd);
			if (exe.isEmpty() || exe.compare("explorer.exe", Qt::CaseInsensitive) == 0 ||
			    c->seen.contains(exe.toLower()))
				return TRUE;
			c->seen.insert(exe.toLower());
			wchar_t title[256];
			GetWindowTextW(hwnd, title, 256);
			c->list.append({QString("%1  (%2)").arg(QString::fromWCharArray(title).left(60), exe), exe});
			return TRUE;
		},
		reinterpret_cast<LPARAM>(&ctx));
	std::sort(ctx.list.begin(), ctx.list.end(),
		  [](const auto &a, const auto &b) { return a.second.compare(b.second, Qt::CaseInsensitive) < 0; });
	return ctx.list;
}

HWND FindWindowForExe(const QString &exe)
{
	struct Ctx {
		QString exe;
		HWND best = nullptr;
		long long bestArea = 0;
	} ctx{exe};
	EnumWindows(
		[](HWND hwnd, LPARAM param) -> BOOL {
			auto *c = reinterpret_cast<Ctx *>(param);
			if (!IsCandidateWindow(hwnd))
				return TRUE;
			if (ExeForWindow(hwnd).compare(c->exe, Qt::CaseInsensitive) != 0)
				return TRUE;
			RECT r;
			GetClientRect(hwnd, &r);
			long long area = (long long)(r.right - r.left) * (r.bottom - r.top);
			if (area > c->bestArea) {
				c->bestArea = area;
				c->best = hwnd;
			}
			return TRUE;
		},
		reinterpret_cast<LPARAM>(&ctx));
	return ctx.best;
}

HWND FindFullscreenWindow()
{
	HWND fg = GetForegroundWindow();
	if (!fg || fg == GetShellWindow() || fg == GetDesktopWindow() || !IsCandidateWindow(fg))
		return nullptr;
	wchar_t cls[64];
	GetClassNameW(fg, cls, 64);
	if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0)
		return nullptr;

	RECT wr;
	if (!GetWindowRect(fg, &wr))
		return nullptr;
	MONITORINFO mi = {sizeof(mi)};
	GetMonitorInfoW(MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST), &mi);
	const RECT &m = mi.rcMonitor;
	if (wr.left <= m.left && wr.top <= m.top && wr.right >= m.right && wr.bottom >= m.bottom)
		return fg;
	return nullptr;
}

QStringList AppsPlayingAudio()
{
	QStringList apps;
	Microsoft::WRL::ComPtr<IMMDeviceEnumerator> en;
	if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en))))
		return apps;
	Microsoft::WRL::ComPtr<IMMDeviceCollection> col;
	if (FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)))
		return apps;
	UINT n = 0;
	col->GetCount(&n);
	for (UINT i = 0; i < n; i++) {
		Microsoft::WRL::ComPtr<IMMDevice> dev;
		Microsoft::WRL::ComPtr<IAudioSessionManager2> mgr;
		Microsoft::WRL::ComPtr<IAudioSessionEnumerator> sessions;
		if (FAILED(col->Item(i, &dev)) ||
		    FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
					 (void **)mgr.GetAddressOf())) ||
		    FAILED(mgr->GetSessionEnumerator(&sessions)))
			continue;
		int count = 0;
		sessions->GetCount(&count);
		for (int k = 0; k < count; k++) {
			Microsoft::WRL::ComPtr<IAudioSessionControl> ctl;
			Microsoft::WRL::ComPtr<IAudioSessionControl2> ctl2;
			if (FAILED(sessions->GetSession(k, &ctl)) || FAILED(ctl.As(&ctl2)))
				continue;
			AudioSessionState state;
			if (FAILED(ctl2->GetState(&state)) || state != AudioSessionStateActive)
				continue;
			if (ctl2->IsSystemSoundsSession() == S_OK)
				continue;
			DWORD pid = 0;
			if (FAILED(ctl2->GetProcessId(&pid)) || pid == 0 || pid == GetCurrentProcessId())
				continue;
			QString exe = ExeForProcess(pid);
			if (!exe.isEmpty() && !apps.contains(exe, Qt::CaseInsensitive))
				apps << exe;
		}
	}
	return apps;
}

bool IsProcessRunning(const QString &exe)
{
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return false;
	std::wstring want = exe.toStdWString();
	bool found = false;
	PROCESSENTRY32W pe = {sizeof(pe)};
	for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe))
		found = _wcsicmp(pe.szExeFile, want.c_str()) == 0;
	CloseHandle(snap);
	return found;
}

QStringList RunningExes()
{
	QStringList list;
	QSet<QString> seen;
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return list;
	PROCESSENTRY32W pe = {sizeof(pe)};
	for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
		QString exe = QString::fromWCharArray(pe.szExeFile);
		if (!seen.contains(exe.toLower())) {
			seen.insert(exe.toLower());
			list << exe;
		}
	}
	CloseHandle(snap);
	return list;
}

} // namespace WinUtil
