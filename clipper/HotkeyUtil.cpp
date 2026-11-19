#include "HotkeyUtil.hpp"

#include <QKeySequence>

static obs_key_t QtKeyToObsKey(int key)
{
	if (key >= Qt::Key_A && key <= Qt::Key_Z)
		return (obs_key_t)(OBS_KEY_A + (key - Qt::Key_A));
	if (key >= Qt::Key_0 && key <= Qt::Key_9)
		return (obs_key_t)(OBS_KEY_0 + (key - Qt::Key_0));
	if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
		return (obs_key_t)(OBS_KEY_F1 + (key - Qt::Key_F1));

	switch (key) {
	case Qt::Key_Backslash: return OBS_KEY_BACKSLASH;
	case Qt::Key_Slash: return OBS_KEY_SLASH;
	case Qt::Key_BracketLeft: return OBS_KEY_BRACKETLEFT;
	case Qt::Key_BracketRight: return OBS_KEY_BRACKETRIGHT;
	case Qt::Key_Semicolon: return OBS_KEY_SEMICOLON;
	case Qt::Key_Apostrophe: return OBS_KEY_APOSTROPHE;
	case Qt::Key_Comma: return OBS_KEY_COMMA;
	case Qt::Key_Period: return OBS_KEY_PERIOD;
	case Qt::Key_Minus: return OBS_KEY_MINUS;
	case Qt::Key_Equal: return OBS_KEY_EQUAL;
	case Qt::Key_QuoteLeft: return OBS_KEY_QUOTELEFT;
	case Qt::Key_Space: return OBS_KEY_SPACE;
	case Qt::Key_Insert: return OBS_KEY_INSERT;
	case Qt::Key_Delete: return OBS_KEY_DELETE;
	case Qt::Key_Home: return OBS_KEY_HOME;
	case Qt::Key_End: return OBS_KEY_END;
	case Qt::Key_PageUp: return OBS_KEY_PAGEUP;
	case Qt::Key_PageDown: return OBS_KEY_PAGEDOWN;
	case Qt::Key_Pause: return OBS_KEY_PAUSE;
	case Qt::Key_Print: return OBS_KEY_PRINT;
	case Qt::Key_ScrollLock: return OBS_KEY_SCROLLLOCK;
	case Qt::Key_Up: return OBS_KEY_UP;
	case Qt::Key_Down: return OBS_KEY_DOWN;
	case Qt::Key_Left: return OBS_KEY_LEFT;
	case Qt::Key_Right: return OBS_KEY_RIGHT;
	default: return OBS_KEY_NONE;
	}
}

obs_data_array_t *HotkeyStringToObsBindings(const QString &seq)
{
	obs_data_array_t *arr = obs_data_array_create();
	QKeySequence ks = QKeySequence::fromString(seq, QKeySequence::PortableText);
	if (ks.isEmpty())
		return arr;

	QKeyCombination combo = ks[0];
	obs_key_t key = QtKeyToObsKey(combo.key());
	if (key == OBS_KEY_NONE)
		return arr;

	Qt::KeyboardModifiers mods = combo.keyboardModifiers();
	OBSDataAutoRelease b = obs_data_create();
	obs_data_set_bool(b, "shift", mods & Qt::ShiftModifier);
	obs_data_set_bool(b, "control", mods & Qt::ControlModifier);
	obs_data_set_bool(b, "alt", mods & Qt::AltModifier);
	obs_data_set_bool(b, "command", mods & Qt::MetaModifier);
	obs_data_set_string(b, "key", obs_key_to_name(key));
	obs_data_array_push_back(arr, b);
	return arr;
}
