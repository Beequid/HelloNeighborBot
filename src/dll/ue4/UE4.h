#pragma once
// Minimal Unreal Engine 4 math types. UE4 (the 1.1.6 build) uses 32-bit
// floats for FVector/FRotator components (UE5 switched to doubles).
#include <cmath>
#include <string>

namespace ue4 {

struct FVector {
    float X = 0.f, Y = 0.f, Z = 0.f;

    FVector() = default;
    FVector(float x, float y, float z) : X(x), Y(y), Z(z) {}

    FVector operator+(const FVector& o) const { return {X + o.X, Y + o.Y, Z + o.Z}; }
    FVector operator-(const FVector& o) const { return {X - o.X, Y - o.Y, Z - o.Z}; }
    FVector operator*(float s) const { return {X * s, Y * s, Z * s}; }

    float SizeSquared() const { return X * X + Y * Y + Z * Z; }
    float Size() const { return std::sqrt(SizeSquared()); }
    float Distance(const FVector& o) const { return (*this - o).Size(); }
    FVector Normalized() const {
        float s = Size();
        return s > 1e-6f ? (*this * (1.f / s)) : FVector{};
    }
    bool IsZero() const { return X == 0.f && Y == 0.f && Z == 0.f; }

    std::string ToString() const {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "X=%.1f Y=%.1f Z=%.1f", X, Y, Z);
        return buf;
    }
};

struct FRotator {
    float Pitch = 0.f, Yaw = 0.f, Roll = 0.f;

    FRotator() = default;
    FRotator(float p, float y, float r) : Pitch(p), Yaw(y), Roll(r) {}

    std::string ToString() const {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "P=%.1f Y=%.1f R=%.1f", Pitch, Yaw, Roll);
        return buf;
    }
};

} // namespace ue4
