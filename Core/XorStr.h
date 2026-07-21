#pragma once
#include <cstdint>
#include <cstring>
#include <atomic>

// ============================================================================
// Compile-time XOR string encryption — thread-safe
// ============================================================================

namespace Xor
{
    static constexpr uint8_t g_key[] = {
        0x7A, 0xB3, 0x4C, 0x9F, 0x2E, 0xD1, 0x65, 0x88
    };
    static constexpr size_t g_keyLen = sizeof(g_key);
}

/// Compile-time XOR-encrypted narrow string (thread-safe)
template <size_t N>
struct XorString
{
    char data[N];
    std::atomic<bool> decrypted{false};

    consteval XorString(const char(&str)[N])
    {
        for (size_t i = 0; i < N; i++)
            data[i] = str[i] ^ Xor::g_key[i % Xor::g_keyLen];
    }

    const char* get()
    {
        if (!decrypted.load(std::memory_order_acquire)) {
            for (size_t i = 0; i < N; i++)
                data[i] ^= Xor::g_key[i % Xor::g_keyLen];
            decrypted.store(true, std::memory_order_release);
        }
        return data;
    }
};

/// Compile-time XOR-encrypted wide string (thread-safe)
template <size_t N>
struct XorStringW
{
    wchar_t data[N];
    std::atomic<bool> decrypted{false};

    consteval XorStringW(const wchar_t(&str)[N])
    {
        for (size_t i = 0; i < N; i++)
            data[i] = str[i] ^ Xor::g_key[i % Xor::g_keyLen];
    }

    const wchar_t* get()
    {
        if (!decrypted.load(std::memory_order_acquire)) {
            for (size_t i = 0; i < N; i++)
                data[i] ^= Xor::g_key[i % Xor::g_keyLen];
            decrypted.store(true, std::memory_order_release);
        }
        return data;
    }
};

#define XSTR(str) ([]() {      \
    static XorString _enc(str); \
    return _enc.get();          \
}())

#define XWSTR(str) ([]() {      \
    static XorStringW _enc(str); \
    return _enc.get();           \
}())

#define XWCSSTR(buf, str)  wcsstr(buf, XWSTR(str))
#define XSTRSTR(buf, str)  strstr(buf, XSTR(str))
