// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameNetwork/GameSpyTitleConfig.h"
#include <cstdio>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
#include <cstring>
int main()
{
    char title[32], key[32];
    CHECK(GameSpy_copyTitleConfig("example-title", "example-key", title, sizeof(title), key, sizeof(key)));
    CHECK(std::strcmp(title, "example-title") == 0 && std::strcmp(key, "example-key") == 0);
    CHECK(!GameSpy_copyTitleConfig("bad title", "example-key", title, sizeof(title), key, sizeof(key)));
    CHECK(!GameSpy_copyTitleConfig("example-title", "", title, sizeof(title), key, sizeof(key)));
    CHECK(!GameSpy_copyTitleConfig("abcdefghijklmnopqrstuvwxyz123456", "example-key", title, sizeof(title), key, sizeof(key)));
    CHECK(!GameSpy_copyTitleConfig("example-title", "example-key", title, 4, key, sizeof(key)));
    return 0;
}
