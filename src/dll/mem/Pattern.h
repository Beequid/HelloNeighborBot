#pragma once
// Memory utilities for an INTERNAL (injected) module: scanning, RIP-relative
// resolution, SEH-guarded read/write, and Cheat-Engine-style pointer chains.
#include <cstdint>
#include <cstddef>
#include <vector>

namespace mem {

struct ModuleInfo {
    uintptr_t base = 0;
    size_t    size = 0;
};

// Base+size of a loaded module. Pass nullptr for the main executable.
ModuleInfo GetModule(const char* moduleName);

// IDA-style signature scan, e.g. "48 8B 05 ? ? ? ?". '?' / '??' = wildcard.
// Returns the absolute address of the first match within [base, base+size), or 0.
uintptr_t PatternScan(uintptr_t base, size_t size, const char* idaPattern);

// Resolve a RIP-relative reference. `address` = start of the instruction,
// `dispOffset` = byte offset of the 4-byte signed displacement,
// `instructionLength` = full instruction length. Returns the absolute target.
uintptr_t ResolveRipRelative(uintptr_t address, int dispOffset, int instructionLength);

// SEH-guarded raw access in this process. Returns false on access violation.
bool SafeRead(uintptr_t address, void* buffer, size_t size);
bool SafeWrite(uintptr_t address, const void* buffer, size_t size);

// Cheat-Engine pointer chain from `base`. For each offset: add it, then
// dereference -- EXCEPT the final offset, which is only added. Returns the
// final ADDRESS (not dereferenced), or 0 if any dereference faults.
uintptr_t ReadChain(uintptr_t base, const std::vector<uintptr_t>& offsets);

template <typename T>
T Read(uintptr_t address) {
    T value{};
    SafeRead(address, &value, sizeof(T));
    return value;
}

template <typename T>
bool Write(uintptr_t address, const T& value) {
    return SafeWrite(address, &value, sizeof(T));
}

} // namespace mem
