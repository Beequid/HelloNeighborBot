#pragma once
#include <cstdint>
#include "Config.h"
#include "ue4/UE4.h"

// Resolves the player's transform fields from config-driven pointer chains and
// reads/writes them. Kept deliberately simple: no full UObject/FName SDK is
// required to move the player -- just GWorld + pointer chains to the FVectors.
class SdkAccess {
public:
    // Resolve the GWorld variable address from config. Returns false on failure.
    bool Init(const Config& config);
    bool IsReady() const { return gworldVarAddr_ != 0; }

    uintptr_t GWorldVarAddr() const { return gworldVarAddr_; }
    // Live UWorld* (the dereference of GWorld). 0 when not in a level.
    uintptr_t World() const;

    bool GetLocation(ue4::FVector& out) const;
    bool SetLocation(const ue4::FVector& v) const;
    bool GetVelocity(ue4::FVector& out) const;
    bool SetVelocity(const ue4::FVector& v) const;
    bool GetRotation(ue4::FRotator& out) const;
    bool SetRotation(const ue4::FRotator& r) const;

private:
    uintptr_t locationAddr() const; // resolved address of the location FVector, or 0
    uintptr_t velocityAddr() const;
    uintptr_t rotationAddr() const;

    uintptr_t moduleBase_ = 0;
    size_t    moduleSize_ = 0;
    uintptr_t gworldVarAddr_ = 0;
    PlayerSpec player_;
};
