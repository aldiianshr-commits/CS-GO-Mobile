// CS:GO Mobile - utilitas umum (log, byte-swap, matematika)
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "CSGO"
#define LOGI(...) csgo_log(1, __VA_ARGS__)
#define LOGW(...) csgo_log(2, __VA_ARGS__)
#define LOGE(...) csgo_log(3, __VA_ARGS__)
#else
#include <cstdio>
#define LOGI(...) csgo_log(1, __VA_ARGS__)
#define LOGW(...) csgo_log(2, __VA_ARGS__)
#define LOGE(...) csgo_log(3, __VA_ARGS__)
#endif

// Log ke logcat/stdout dan (jika diset) ke file /CSGO/logs/last.log
void csgo_log(int level, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
void csgo_set_log_file(const std::string& path);

namespace cs {

inline uint16_t bswap16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
inline uint32_t bswap32(uint32_t v) { return __builtin_bswap32(v); }

inline uint16_t rd16le(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t rd32le(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
inline uint16_t rd16be(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline uint32_t rd32be(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]; }
inline uint32_t rd24le(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16); }
inline uint32_t rd24be(const uint8_t* p) { return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2]; }
inline float rdf32be(const uint8_t* p) { uint32_t u = rd32be(p); float f; memcpy(&f, &u, 4); return f; }

// Membalik byte sesuai layout: 'b' = 1 byte, 'h' = 2 byte, 'i' = 4 byte
// Dipakai untuk lump BSP big-endian Xbox 360 -> little-endian ARM.
inline size_t layoutSize(const char* layout) {
    size_t s = 0;
    for (const char* c = layout; *c; ++c) s += (*c == 'b') ? 1 : (*c == 'h') ? 2 : 4;
    return s;
}
inline void swapByLayout(uint8_t* data, size_t count, const char* layout) {
    size_t stride = layoutSize(layout);
    for (size_t n = 0; n < count; ++n) {
        uint8_t* p = data + n * stride;
        for (const char* c = layout; *c; ++c) {
            if (*c == 'h') { std::swap(p[0], p[1]); p += 2; }
            else if (*c == 'i') { std::swap(p[0], p[3]); std::swap(p[1], p[2]); p += 4; }
            else p += 1;
        }
    }
}

inline std::string toLower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}
inline bool endsWith(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}
inline bool startsWith(const std::string& s, const std::string& pre) {
    return s.size() >= pre.size() && s.compare(0, pre.size(), pre) == 0;
}
// Normalisasi path: huruf kecil, '/' sebagai pemisah, tanpa '//' ganda
inline std::string normPath(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        char d = (c == '\\') ? '/' : (char)tolower((unsigned char)c);
        if (d == '/' && (out.empty() || out.back() == '/')) continue;
        out.push_back(d);
    }
    while (startsWith(out, "./")) out.erase(0, 2);
    return out;
}

// ---------------------------------------------------------------- matematika
struct Vec2 { float x = 0, y = 0; };
struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    float& operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }
};
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(const Vec3& a) { float l = length(a); return l > 1e-8f ? a * (1.0f / l) : Vec3(0, 0, 0); }
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

struct Mat4 {
    float m[16]; // kolom-mayor (seperti GLSL)
    static Mat4 identity() { Mat4 r{}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1; return r; }
    Mat4 operator*(const Mat4& b) const {
        Mat4 r{};
        for (int c = 0; c < 4; ++c)
            for (int rr = 0; rr < 4; ++rr) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += m[k * 4 + rr] * b.m[c * 4 + k];
                r.m[c * 4 + rr] = s;
            }
        return r;
    }
};

// Proyeksi perspektif Vulkan (depth 0..1, sumbu Y clip ke bawah)
inline Mat4 perspectiveVk(float fovyRad, float aspect, float zn, float zf) {
    Mat4 r{};
    float f = 1.0f / std::tan(fovyRad * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = -f;
    r.m[10] = zf / (zn - zf);
    r.m[11] = -1.0f;
    r.m[14] = (zn * zf) / (zn - zf);
    return r;
}

// Matriks view dari posisi + sudut Source (pitch, yaw dalam derajat).
// Koordinat Source: X depan, Y kiri, Z atas. Diubah ke ruang kamera: X kanan, Y atas, -Z depan.
inline Mat4 viewSource(const Vec3& eye, float pitchDeg, float yawDeg) {
    float p = pitchDeg * (float)M_PI / 180.f, y = yawDeg * (float)M_PI / 180.f;
    Vec3 fwd(std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), -std::sin(p));
    Vec3 right = normalize(cross(fwd, Vec3(0, 0, 1)));
    if (length(right) < 0.5f) right = Vec3(std::sin(y), -std::cos(y), 0);
    Vec3 up = cross(right, fwd);
    Mat4 r = Mat4::identity();
    r.m[0] = right.x; r.m[4] = right.y; r.m[8] = right.z;
    r.m[1] = up.x;    r.m[5] = up.y;    r.m[9] = up.z;
    r.m[2] = -fwd.x;  r.m[6] = -fwd.y;  r.m[10] = -fwd.z;
    r.m[12] = -dot(right, eye);
    r.m[13] = -dot(up, eye);
    r.m[14] = dot(fwd, eye);
    return r;
}

inline uint16_t floatToHalf(float f) {
    uint32_t x; memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000;
    int32_t exp = (int32_t)((x >> 23) & 0xff) - 127 + 15;
    uint32_t mant = x & 0x7fffff;
    if (exp <= 0) return (uint16_t)sign;
    if (exp >= 31) return (uint16_t)(sign | 0x7bff);
    return (uint16_t)(sign | (exp << 10) | (mant >> 13));
}

} // namespace cs
