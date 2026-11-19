#pragma once

#include <QString>
#include <obs.hpp>

/* Converts a portable Qt key-sequence string ("Alt+O", "Ctrl+Shift+F9", "Alt+\")
 * into a libobs hotkey binding array usable with obs_hotkey_load(). */
obs_data_array_t *HotkeyStringToObsBindings(const QString &seq);
