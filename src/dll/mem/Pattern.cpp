// Memory utilities for an INTERNAL (injected) module: scanning, RIP-relative
// resolution, SEH-guarded read/write, and Cheat-Engine-style pointer chains.
#include "mem/Pattern.h"

#include <windows.h>

#include <cstring>
#include <string>

namespace mem {

ModuleInfo GetModule(const char* moduleName) {
    ModuleInfo info{};

    HMODULE handle = (moduleName == nullptr)
        ? GetModuleHandleA(NULL)
        : GetModuleHandleA(moduleName);
    if (handle == nullptr) {
        return info; // {0, 0}
    }

    uintptr_t base = reinterpret_cast<uintptr_t>(handle);

    // Parse PE headers to get SizeOfImage. Read them through SafeRead so a
    // malformed/paged-out image can never fault out of here.
    IMAGE_DOS_HEADER dos{};
    if (!SafeRead(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
        return info; // {0, 0}
    }

    IMAGE_NT_HEADERS64 nt{};
    if (!SafeRead(base + static_cast<uintptr_t>(dos.e_lfanew), &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE) {
        return info; // {0, 0}
    }

    info.base = base;
    info.size = static_cast<size_t>(nt.OptionalHeader.SizeOfImage);
    return info;
}

namespace {

// Parse an IDA-style signature ("48 8B ? ?") into parallel byte/mask vectors.
// mask[i] == false means "wildcard" (the corresponding pattern[i] is ignored).
bool ParsePattern(const char* idaPattern,
                  std::vector<uint8_t>& pattern,
                  std::vector<bool>& mask) {
    pattern.clear();
    mask.clear();
    if (idaPattern == nullptr) {
        return false;
    }

    const char* p = idaPattern;
    while (*p != '\0') {
        // Skip whitespace separators between tokens.
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            ++p;
            continue;
        }

        if (*p == '?') {
            // Wildcard token: "?" or "??".
            pattern.push_back(0x00);
            mask.push_back(false);
            ++p;
            if (*p == '?') {
                ++p;
            }
            continue;
        }

        // Hex byte token: one or two hex digits.
        auto hexVal = [](char c, int& out) -> bool {
            if (c >= '0' && c <= '9') { out = c - '0'; return true; }
            if (c >= 'a' && c <= 'f') { out = c - 'a' + 10; return true; }
            if (c >= 'A' && c <= 'F') { out = c - 'A' + 10; return true; }
            return false;
        };

        int hi = 0;
        if (!hexVal(*p, hi)) {
            return false; // Unexpected character.
        }
        ++p;

        int lo = 0;
        if (hexVal(*p, lo)) {
            // Two-digit byte.
            pattern.push_back(static_cast<uint8_t>((hi << 4) | lo));
            ++p;
        } else {
            // Single-digit byte.
            pattern.push_back(static_cast<uint8_t>(hi));
        }
        mask.push_back(true);
    }

    return !pattern.empty();
}

} // namespace

uintptr_t PatternScan(uintptr_t base, size_t size, const char* idaPattern) {
    if (base == 0 || size == 0) {
        return 0;
    }

    std::vector<uint8_t> pattern;
    std::vector<bool> mask;
    if (!ParsePattern(idaPattern, pattern, mask)) {
        return 0;
    }

    const size_t len = pattern.size();
    if (len == 0 || len > size) {
        return 0;
    }

    // Module memory is committed, so reading via a direct pointer is fine; still
    // be defensive by not scanning past the end of the range.
    const uint8_t* region = reinterpret_cast<const uint8_t*>(base);
    const size_t lastStart = size - len;

    for (size_t i = 0; i <= lastStart; ++i) {
        bool matched = true;
        for (size_t j = 0; j < len; ++j) {
            if (mask[j] && region[i + j] != pattern[j]) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return base + static_cast<uintptr_t>(i);
        }
    }

    return 0;
}

uintptr_t ResolveRipRelative(uintptr_t address, int dispOffset, int instructionLength) {
    int32_t disp = 0;
    std::memcpy(&disp, reinterpret_cast<const void*>(address + static_cast<uintptr_t>(dispOffset)),
                sizeof(disp));
    return address + static_cast<uintptr_t>(instructionLength) + static_cast<uintptr_t>(static_cast<intptr_t>(disp));
}

bool SafeRead(uintptr_t address, void* buffer, size_t size) {
    if (address == 0 || buffer == nullptr || size == 0) {
        return false;
    }
    __try {
        std::memcpy(buffer, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeWrite(uintptr_t address, const void* buffer, size_t size) {
    if (address == 0 || buffer == nullptr || size == 0) {
        return false;
    }
    __try {
        std::memcpy(reinterpret_cast<void*>(address), buffer, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uintptr_t ReadChain(uintptr_t base, const std::vector<uintptr_t>& offsets) {
    uintptr_t cur = base;
    const size_t count = offsets.size();
    for (size_t i = 0; i < count; ++i) {
        cur += offsets[i];
        if (i != count - 1) {
            // Not the last offset: dereference (SEH-guarded).
            uintptr_t next = 0;
            if (!SafeRead(cur, &next, sizeof(next))) {
                return 0;
            }
            cur = next;
        }
    }
    return cur;
}

} // namespace mem
