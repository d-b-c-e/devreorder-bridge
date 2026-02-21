#pragma once

// Input Bridge Shared Memory — zero-copy communication between the Input Bridge
// C# console app and this DevReorder bridge DLL.
//
// Layout (288 bytes total):
//   Offset  0: DWORD version    (must be 1)
//   Offset  4: DWORD enabled    (0=passthrough, 1=inject)
//   Offset  8: DWORD sequence   (seqlock: odd=writing, even=stable)
//   Offset 12: DWORD padding
//   Offset 16: DIJOYSTATE2 state (272 bytes)
//
// The C# Input Bridge creates this shared memory and writes joystick state.
// This DLL opens it read-only and injects the state via GetDeviceState hook.
// Seqlock protocol: writer increments sequence before+after each update.
// Reader checks sequence before+after reading — if different, discard (torn read).

#include <windows.h>
#include <dinput.h>
#include <string>

#define INPUT_BRIDGE_SHM_NAME L"Local\\InputBridge_SharedState"
#define INPUT_BRIDGE_SHM_SIZE 288

// Configuration from [bridge] section of devreorder.ini
struct BridgeConfig {
	bool configured = false;
	bool enabled = false;
	std::wstring deviceName;
};

// Read-only shared memory accessor with lazy connection and retry cooldown.
// Thread-safe for single-reader use (called from GetDeviceState on game thread).
class InputBridgeShm {
public:
	~InputBridgeShm() { Disconnect(); }

	// Try to open shared memory. Returns true if connected.
	// Rate-limited to one attempt per 5 seconds on failure.
	bool TryConnect();

	// Close the shared memory mapping.
	void Disconnect();

	// Read joystick state using seqlock protocol.
	// Returns true if a consistent read was obtained.
	// Returns false on torn read, disabled, or not connected.
	bool ReadState(DIJOYSTATE2* outState);

	bool IsConnected() const { return m_view != nullptr; }

private:
	HANDLE m_mapping = nullptr;
	void* m_view = nullptr;
	ULONGLONG m_lastRetryTick = 0;
	static const DWORD RETRY_COOLDOWN_MS = 5000;
};
