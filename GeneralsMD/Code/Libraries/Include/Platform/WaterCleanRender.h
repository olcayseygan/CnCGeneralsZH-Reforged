/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "Platform/WaterCleanPrecompute.h"
#ifndef _WIN32
class IDirect3DDevice9;
bool PosixWaterCleanBind(IDirect3DDevice9 *,const WaterCleanPrecompute::Atlas *);
void PosixWaterCleanClear(IDirect3DDevice9 *);
#endif
