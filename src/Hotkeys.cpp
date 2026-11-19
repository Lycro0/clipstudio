#include "Hotkeys.hpp"

#include <QCoreApplication>
#include <QKeySequence>

#include <windows.h>

static UINT QtKeyToVk(int key)
{
	if (key >= Qt::Key_A && key <= Qt::Key_Z)
		return 'A' + (key - Qt::Key_A);
	if (key >= Qt::Key_0 && key <= Qt::Key_9)
		return '0' + (key - Qt::Key_0);
	if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
		return VK_F1 + (key - Qt::Key_F1);
	switch (key) {
	case Qt::Key_Space: return VK_SPACE;
	case Qt::Key_Insert: return VK_INSERT;
	case Qt::Key_Delete: return VK_DELETE;
	case Qt::Key_Home: return VK_HOME;
	case Qt::Key_End: return VK_END;
	case Qt::Key_PageUp: return VK_PRIOR;
	case Qt::Key_PageDown: return VK_NEXT;
	case Qt::Key_Left: return VK_LEFT;
	case Qt::Key_Right: return VK_RIGHT;
	case Qt::Key_Up: return VK_UP;
	case Qt::Key_Down: return VK_DOWN;
	case Qt::Key_Pause: return VK_PAUSE;
	case Qt::Key_Print: return VK_SNAPSHOT;
	case Qt::Key_ScrollLock: return VK_SCROLL;
	default: break;
	}
	if (key > 0 && key < 0x10000) {
		// Punctuation such as \ ; [ ] depends on the keyboard layout.
		SHORT r = VkKeyScanW((wchar_t)key);
		if (r != -1)
			return LOBYTE(r);
	}
	return 0;
}

Hotkeys::Hotkeys(QObject *parent) : QObject(parent)
{
	QCoreApplication::instance()->installNativeEventFilter(this);
}

Hotkeys::~Hotkeys()
{
	for (int id : {SaveClip, ToggleRecording, ToggleWindow})
		UnregisterHotKey(nullptr, id);
}

bool Hotkeys::Set(Id id, const QString &sequence)
{
	UnregisterHotKey(nullptr, id);
	QKeySequence ks = QKeySequence::fromString(sequence, QKeySequence::PortableText);
	if (ks.isEmpty())
		return true; // no hotkey

	QKeyCombination combo = ks[0];
	UINT vk = QtKeyToVk(combo.key());
	if (!vk)
		return false;

	Qt::KeyboardModifiers m = combo.keyboardModifiers();
	UINT mods = MOD_NOREPEAT;
	if (m & Qt::AltModifier)
		mods |= MOD_ALT;
	if (m & Qt::ControlModifier)
		mods |= MOD_CONTROL;
	if (m & Qt::ShiftModifier)
		mods |= MOD_SHIFT;
	if (m & Qt::MetaModifier)
		mods |= MOD_WIN;
	return RegisterHotKey(nullptr, id, mods, vk) != 0;
}

bool Hotkeys::nativeEventFilter(const QByteArray &, void *message, qintptr *)
{
	MSG *msg = static_cast<MSG *>(message);
	if (msg->message == WM_HOTKEY) {
		emit Triggered((int)msg->wParam);
		return true;
	}
	return false;
}
