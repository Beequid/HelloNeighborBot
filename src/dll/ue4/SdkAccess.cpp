#include "ue4/SdkAccess.h"

#include "mem/Pattern.h"
#include "Logger.h"

bool SdkAccess::Init(const Config& config) {
    mem::ModuleInfo mod = mem::GetModule(config.module_name.c_str());
    if (mod.base == 0) {
        LOG_ERROR("SdkAccess::Init: module '%s' not found", config.module_name.c_str());
        return false;
    }

    moduleBase_ = mod.base;
    moduleSize_ = mod.size;
    player_ = config.player;

    const ResolveSpec& g = config.gworld;
    if (g.mode == "rva") {
        gworldVarAddr_ = moduleBase_ + g.rva;
    } else {
        uintptr_t hit = mem::PatternScan(moduleBase_, moduleSize_, g.pattern.c_str());
        if (hit == 0) {
            LOG_ERROR("SdkAccess::Init: GWorld pattern '%s' not found", g.pattern.c_str());
            return false;
        }
        if (g.rip_relative) {
            gworldVarAddr_ = mem::ResolveRipRelative(hit, g.pattern_offset, g.instruction_length);
        } else {
            gworldVarAddr_ = hit + g.pattern_offset;
        }
    }

    LOG_INFO("SdkAccess::Init: GWorld variable address resolved to 0x%llX",
             static_cast<unsigned long long>(gworldVarAddr_));

    return gworldVarAddr_ != 0;
}

uintptr_t SdkAccess::World() const {
    return mem::Read<uintptr_t>(gworldVarAddr_);
}

uintptr_t SdkAccess::locationAddr() const {
    if (player_.location_chain.empty()) {
        return 0;
    }
    return mem::ReadChain(gworldVarAddr_, player_.location_chain);
}

uintptr_t SdkAccess::velocityAddr() const {
    if (player_.velocity_chain.empty()) {
        return 0;
    }
    return mem::ReadChain(gworldVarAddr_, player_.velocity_chain);
}

uintptr_t SdkAccess::rotationAddr() const {
    if (player_.rotation_chain.empty()) {
        return 0;
    }
    return mem::ReadChain(gworldVarAddr_, player_.rotation_chain);
}

bool SdkAccess::GetLocation(ue4::FVector& out) const {
    uintptr_t a = locationAddr();
    if (a == 0) {
        return false;
    }
    out = mem::Read<ue4::FVector>(a);
    return true;
}

bool SdkAccess::SetLocation(const ue4::FVector& v) const {
    uintptr_t a = locationAddr();
    if (a == 0) {
        return false;
    }
    return mem::Write(a, v);
}

bool SdkAccess::GetVelocity(ue4::FVector& out) const {
    uintptr_t a = velocityAddr();
    if (a == 0) {
        return false;
    }
    out = mem::Read<ue4::FVector>(a);
    return true;
}

bool SdkAccess::SetVelocity(const ue4::FVector& v) const {
    uintptr_t a = velocityAddr();
    if (a == 0) {
        return false;
    }
    return mem::Write(a, v);
}

bool SdkAccess::GetRotation(ue4::FRotator& out) const {
    uintptr_t a = rotationAddr();
    if (a == 0) {
        return false;
    }
    out = mem::Read<ue4::FRotator>(a);
    return true;
}

bool SdkAccess::SetRotation(const ue4::FRotator& r) const {
    uintptr_t a = rotationAddr();
    if (a == 0) {
        return false;
    }
    return mem::Write(a, r);
}
