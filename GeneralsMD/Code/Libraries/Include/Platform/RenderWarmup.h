/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
// Optional SDL renderer warmup (port rule 2). Windows keeps its own renderer and shader cache.
// This creates shader/pipeline objects only: no scene draw, camera update or simulation tick.
#pragma once

struct IDirect3DDevice9;

struct RenderWarmupStats
{
	unsigned Programs = 0, Pipelines = 0, Attempts = 0, Failed = 0;
	double Milliseconds = 0;
	bool Limited = false;
};

RenderWarmupStats Preload_Render_Pipelines(IDirect3DDevice9 *device, bool modernWater);
