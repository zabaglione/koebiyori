#pragma once

namespace LauncherSupport {
bool available();
// Returns to the Launcher boot screen. Call only after closing the voice session.
void restart();
}
