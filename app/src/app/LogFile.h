#pragma once

// Windows: Burrow is a GUI program without a console, so its messages would go nowhere. They are written to
// %LOCALAPPDATA%\Burrow\burrow.log (started afresh at each launch) so a failed start can be diagnosed.
// Burrow never logs secrets (passwords, OTP keys, tokens). No-op on other platforms and when a console or a
// redirected stderr is attached.
namespace LogFile {
void installIfNoConsole();
}
