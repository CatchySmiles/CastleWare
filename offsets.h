#pragma once
#include <cstdint>
#include <string>
#include <windows.h>

static const std::wstring targetProcessName = L"Cubic.exe";

// Player base pointer: "Cubic.exe"+002F9A28
static constexpr uintptr_t moduleBaseOffset = 0x002F9A28;
static constexpr uintptr_t playerSizeOffset = 0x4AC;
static constexpr uintptr_t jumpPotentialOffset = 0x478;

// West/East (X) and North/South (Z) position floats — there are multiple entries each. Use the first for display,
// write to all entries when changing position so the game picks up the new coordinates.
static constexpr uintptr_t posWestEastOffsets[] = {
    0x24, 0x28, 0x2C, 0x30, 0x34, 0x3C, 0x40, 0x44, 0x48, 0x4C, 0x50
};

static constexpr uintptr_t posNorthSouthOffsets[] = {
    0x6C, 0x60, 0x64, 0x68, 0x70, 0x74, 0x78, 0x7C, 0x80, 0x84, 0x88, 0x8C
};

// Height (Y) offsets — multiple entries. Use the first for display, write to all when changing height.
static constexpr uintptr_t posHeightOffsets[] = {
    0xC8, 0xC4, 0xC0, 0xBC, 0xB8, 0xB4, 0xB0, 0xAC, 0xA8, 0xA4, 0xA0, 0x9C
};


// Client-side hide name byte offset (0 = visible, 1 = hidden)
static constexpr uintptr_t hideNameOffset = 0x1224;

// In-game currency offsets (relative to module + cubitsBaseOffset)
static constexpr uintptr_t cubitsBaseOffset = 0x002F9A30;
static constexpr uintptr_t cubitsOffset = 0x4DA8; // Cubits
static constexpr uintptr_t recubesOffset = 0x4DCC; // Recubes

// Game speed pointer: Cubic.exe+301EB8 -> read pointer, then +0xBC4 is the float value
static constexpr uintptr_t gameSpeedPointerBaseOffset = 0x301E04; // module + this -> pointer 
static constexpr uintptr_t gameSpeedPointerInnerOffset = 0xBC4; // pointer + this -> float


// Generic helpers to read/write floats at a base + offset using a process handle.
inline bool ReadFloatAt(HANDLE hProcess, uintptr_t base, uintptr_t off, float& out) {
    if (!hProcess || !base) return false;
    return ReadProcessMemory(hProcess, (LPCVOID)(base + off), &out, sizeof(float), nullptr);
}

inline bool WriteFloatAt(HANDLE hProcess, uintptr_t base, uintptr_t off, float v) {
    if (!hProcess || !base) return false;
    return WriteProcessMemory(hProcess, (LPVOID)(base + off), &v, sizeof(float), nullptr);
}

// Read/write a single byte at base+off
inline bool ReadByteAt(HANDLE hProcess, uintptr_t base, uintptr_t off, uint8_t& out) {
    if (!hProcess || !base) return false;
    return ReadProcessMemory(hProcess, (LPCVOID)(base + off), &out, sizeof(uint8_t), nullptr);
}

inline bool WriteByteAt(HANDLE hProcess, uintptr_t base, uintptr_t off, uint8_t v) {
    if (!hProcess || !base) return false;
    return WriteProcessMemory(hProcess, (LPVOID)(base + off), &v, sizeof(uint8_t), nullptr);
}
