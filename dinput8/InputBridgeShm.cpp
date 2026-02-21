#include "stdafx.h"
#include "InputBridgeShm.h"
#include "Logger.h"

bool InputBridgeShm::TryConnect()
{
	if (m_view) return true;

	// Rate-limit reconnection attempts
	ULONGLONG now = GetTickCount64();
	if (now - m_lastRetryTick < RETRY_COOLDOWN_MS) return false;
	m_lastRetryTick = now;

	m_mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, INPUT_BRIDGE_SHM_NAME);
	if (!m_mapping) return false;

	m_view = MapViewOfFile(m_mapping, FILE_MAP_READ, 0, 0, INPUT_BRIDGE_SHM_SIZE);
	if (!m_view) {
		PrintLog("InputBridge: MapViewOfFile failed (error %lu)", GetLastError());
		CloseHandle(m_mapping);
		m_mapping = nullptr;
		return false;
	}

	PrintLog("InputBridge: Shared memory connected");
	return true;
}

void InputBridgeShm::Disconnect()
{
	if (m_view) {
		UnmapViewOfFile(m_view);
		m_view = nullptr;
	}
	if (m_mapping) {
		CloseHandle(m_mapping);
		m_mapping = nullptr;
	}
}

bool InputBridgeShm::ReadState(DIJOYSTATE2* outState)
{
	if (!m_view) return false;

	const volatile DWORD* header = (const volatile DWORD*)m_view;

	// Verify protocol version
	if (header[0] != 1) return false;

	// Check if injection is enabled
	if (!header[1]) return false;

	// Seqlock read: sequence must be even (not mid-write) and stable
	DWORD seq1 = header[2];
	MemoryBarrier();

	if (seq1 & 1) return false;  // Writer is mid-update

	memcpy(outState, (const BYTE*)m_view + 16, sizeof(DIJOYSTATE2));

	MemoryBarrier();
	DWORD seq2 = header[2];

	return (seq1 == seq2);  // Consistent if sequence unchanged
}
