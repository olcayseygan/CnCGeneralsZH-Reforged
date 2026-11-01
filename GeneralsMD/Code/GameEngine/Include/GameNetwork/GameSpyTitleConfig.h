// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#pragma once
#include <cstdlib>
#include <cstring>

// The peer SDK copies into 32-byte title/server-browser credential buffers.
// Validate before copying, using the narrowest limit shared by every SDK sink.
inline bool GameSpy_validTitleField(const char *value)
{
    if (!value || !*value || std::strlen(value) > 31) return false;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return false;
    return true;
}
inline bool GameSpy_copyTitleConfig(const char *title, const char *key,
    char *titleOut, size_t titleCapacity, char *keyOut, size_t keyCapacity)
{
    if (!titleOut || !keyOut || !titleCapacity || !keyCapacity) return false;
    titleOut[0] = keyOut[0] = 0;
    if (!GameSpy_validTitleField(title) || !GameSpy_validTitleField(key) ||
        std::strlen(title) >= titleCapacity || std::strlen(key) >= keyCapacity) return false;
    std::memcpy(titleOut, title, std::strlen(title)+1);
    std::memcpy(keyOut, key, std::strlen(key)+1);
    return true;
}
inline bool GameSpy_readTitleConfig(const char *defaultTitle,
    char *titleOut, size_t titleCapacity, char *keyOut, size_t keyCapacity)
{
    const char *title = std::getenv("ZH_GAMESPY_TITLE");
    return GameSpy_copyTitleConfig(title ? title : defaultTitle, std::getenv("ZH_GAMESPY_SECRET_KEY"),
        titleOut, titleCapacity, keyOut, keyCapacity);
}
