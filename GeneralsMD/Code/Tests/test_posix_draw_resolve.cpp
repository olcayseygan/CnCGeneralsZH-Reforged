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

// The draw's resolve (PosixDevice/Render/PosixDevice9Draw.cpp, A3c) on a real headless device made
// through CreateDevice, its state set through the D3D9 calls the engine makes:
//   - D3D9's documented initial states;
//   - the combiner walk: stage count, the arguments carried as set, whether a texture is bound, a
//     disabled stage 0 as the one-stage diffuse combiner, and a stage whose COLORARG1 is an unbound
//     texture ending the walk as a disabled one does;
//   - the vertex description's D3D9 simplifications: no lighting for pretransformed vertices, a material
//     source the vertex cannot supply read from the material; table fog refused with its reason;
//   - the vertex description: the enabled lights packed down in order, and more than the generator
//     carries refused;
//   - the constants: world * view * projection against a double product, the normal transform of a
//     non-uniform scale, a light carried into camera space (a point moves with the view's translation,
//     a direction does not), the attenuation and cone packing, fog's floats, the alpha reference as a
//     level, and D3DCOLORs as R, G, B, A.
//
// WHAT THIS DOES NOT PROVE: that a draw built from these looks right.  That is the reference's
// comparison (A3b, FFReference), which checks the pixels against D3D9's formulas, not these numbers
// against this file's reading of them.

#include "PosixDevice9.h"
#include "SdlConstants.h"
#include "ffshader.h"
#include "ffvertex.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition) \
	do { if (!(condition)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static bool near_value(double actual, double expected)
{
	return fabs(actual - expected) <= 1e-5 * (1.0 + fabs(expected));
}

static D3DMATRIX matrix_of(const double m[4][4])
{
	D3DMATRIX out;
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			out.m[r][c] = (float)m[r][c];
	return out;
}

static PosixDevice9 *make_device(IDirect3D9 *&d3d)
{
	d3d = Direct3DCreate9(D3D_SDK_VERSION);
	if (d3d == NULL) return NULL;
	D3DPRESENT_PARAMETERS parameters;
	memset(&parameters, 0, sizeof(parameters));
	parameters.BackBufferWidth = 64;
	parameters.BackBufferHeight = 32;
	parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
	parameters.Windowed = 1;
	parameters.EnableAutoDepthStencil = 1;
	parameters.AutoDepthStencilFormat = D3DFMT_D24S8;
	IDirect3DDevice9 *device = NULL;
	if (Render_Failed(d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL, 0, &parameters, &device))) return NULL;
	return static_cast<PosixDevice9 *>(device);
}

static void check_defaults(PosixDevice9 *device)
{
	RenderUInt32 value = 0;
	device->GetRenderState(D3DRS_CULLMODE, &value);		CHECK(value == D3DCULL_CCW);
	device->GetRenderState(D3DRS_ZFUNC, &value);		CHECK(value == D3DCMP_LESSEQUAL);
	device->GetRenderState(D3DRS_ZENABLE, &value);		CHECK(value == D3DZB_TRUE);	// an auto depth surface
	device->GetRenderState(D3DRS_TEXTUREFACTOR, &value);	CHECK(value == 0xFFFFFFFFu);
	device->GetRenderState(D3DRS_COLORWRITEENABLE, &value);	CHECK(value == 0xF);
	device->GetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, &value);	CHECK(value == D3DMCS_COLOR1);
	device->GetRenderState(D3DRS_FOGEND, &value);
	float fog_end;
	memcpy(&fog_end, &value, sizeof(fog_end));
	CHECK(fog_end == 1.0f);
	device->GetTextureStageState(0, D3DTSS_COLOROP, &value);	CHECK(value == D3DTOP_MODULATE);
	device->GetTextureStageState(1, D3DTSS_COLOROP, &value);	CHECK(value == D3DTOP_DISABLE);
	device->GetTextureStageState(0, D3DTSS_ALPHAOP, &value);	CHECK(value == D3DTOP_SELECTARG1);
	device->GetTextureStageState(3, D3DTSS_TEXCOORDINDEX, &value);	CHECK(value == 3);
}

static void check_combiner(PosixDevice9 *device)
{
	CombinerDescription description;
	// The default stage 0 reads COLORARG1 = TEXTURE: with nothing bound, D3D9 ends the cascade there.
	device->Build_Combiner_Description(description);
	CHECK(description.StageCount == 1);
	CHECK(description.Stages[0].ColourOperation == D3DTOP_SELECTARG1 && description.Stages[0].ColourArgument1 == D3DTA_DIFFUSE);

	IDirect3DTexture9 *texture = NULL;
	CHECK(device->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, NULL) == D3D_OK);
	device->SetTexture(0, texture);
	device->Build_Combiner_Description(description);
	CHECK(description.StageCount == 1);
	CHECK(description.Stages[0].ColourOperation == D3DTOP_MODULATE);
	CHECK(description.Stages[0].ColourArgument1 == D3DTA_TEXTURE && description.Stages[0].ColourArgument2 == D3DTA_CURRENT);
	CHECK(description.Stages[0].TextureBound);

	device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_ADD);
	device->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TFACTOR | D3DTA_COMPLEMENT);
	device->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
	device->SetRenderState(D3DRS_ALPHATESTENABLE, 1);
	device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
	device->Build_Combiner_Description(description);
	CHECK(description.StageCount == 2);
	CHECK(description.Stages[1].ColourOperation == D3DTOP_ADD);
	CHECK(description.Stages[1].ColourArgument1 == (D3DTA_TFACTOR | D3DTA_COMPLEMENT));
	CHECK(description.Stages[1].TextureCoordinateIndex == 0);
	CHECK(description.PixelPipeline.AlphaTestEnabled && description.PixelPipeline.AlphaFunction == D3DCMP_GREATEREQUAL);
	// D3DRS_SPECULARENABLE is the pixel program's specular add.
	CHECK(!description.SpecularAdd);
	device->SetRenderState(D3DRS_SPECULARENABLE, 1);
	device->Build_Combiner_Description(description);
	CHECK(description.SpecularAdd && CombinerShader_Key(description).find(":SP") != std::string::npos);
	device->SetRenderState(D3DRS_SPECULARENABLE, 0);

	// No texturing: the diffuse colour and alpha, as one stage.
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
	device->Build_Combiner_Description(description);
	CHECK(description.StageCount == 1);
	CHECK(description.Stages[0].ColourOperation == D3DTOP_SELECTARG1 && description.Stages[0].ColourArgument1 == D3DTA_DIFFUSE);
	CHECK(description.Stages[0].AlphaOperation == D3DTOP_SELECTARG1 && description.Stages[0].AlphaArgument1 == D3DTA_DIFFUSE);
	VertexPipelineDescription vertex;
	CHECK(device->Build_Vertex_Description(vertex));
	CHECK(vertex.StageCount == 1);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);

	// Stage 1 with COLORARG1 = TEXTURE and nothing bound at 1 ends the walk after stage 0.
	device->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	device->Build_Combiner_Description(description);
	CHECK(description.StageCount == 1);
	CHECK(device->Build_Vertex_Description(vertex) && vertex.StageCount == 1);

	device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	device->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	device->SetRenderState(D3DRS_ALPHATESTENABLE, 0);
	device->SetTexture(0, NULL);
	texture->Release();

	// D3D9's simplifications in the vertex description.
	device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
	device->SetRenderState(D3DRS_LIGHTING, 1);
	CHECK(device->Build_Vertex_Description(vertex) && !vertex.LightingEnabled);
	device->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL);
	device->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR2);
	device->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
	CHECK(device->Build_Vertex_Description(vertex) && vertex.LightingEnabled);
	CHECK(vertex.SpecularMaterialSource == D3DMCS_MATERIAL && vertex.DiffuseMaterialSource == D3DMCS_MATERIAL);
	device->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE);
	CHECK(device->Build_Vertex_Description(vertex) && vertex.DiffuseMaterialSource == D3DMCS_COLOR1);
	// D3DRS_LOCALVIEWER, TRUE by default, reaches the description, and the key of a lit specular program.
	device->SetRenderState(D3DRS_SPECULARENABLE, 1);
	CHECK(device->Build_Vertex_Description(vertex) && vertex.LocalViewer);
	CHECK(VertexShader_Key(vertex).find(":V") != std::string::npos);
	device->SetRenderState(D3DRS_LOCALVIEWER, 0);
	CHECK(device->Build_Vertex_Description(vertex) && !vertex.LocalViewer);
	CHECK(VertexShader_Key(vertex).find(":V") == std::string::npos);
	device->SetRenderState(D3DRS_LOCALVIEWER, 1);
	device->SetRenderState(D3DRS_SPECULARENABLE, 0);
	// Lit without specular still writes the halfway vector, so its key still says which (a review's reading).
	CHECK(device->Build_Vertex_Description(vertex) && VertexShader_Key(vertex).find(":V") != std::string::npos);
	device->SetRenderState(D3DRS_FOGENABLE, 1);
	device->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_LINEAR);
	std::string refusal;
	CHECK(!device->Build_Vertex_Description(vertex, &refusal) && refusal == "table fog");
	device->SetRenderState(D3DRS_FOGENABLE, 0);
	device->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
	device->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR2);
	device->SetFVF(0);
}

static void check_lights(PosixDevice9 *device)
{
	D3DLIGHT9 light;
	memset(&light, 0, sizeof(light));
	light.Type = D3DLIGHT_DIRECTIONAL;
	device->SetLight(0, &light);
	light.Type = D3DLIGHT_POINT;
	device->SetLight(2, &light);
	light.Type = D3DLIGHT_SPOT;
	device->SetLight(5, &light);
	device->LightEnable(0, 1);
	device->LightEnable(2, 1);
	device->LightEnable(5, 1);
	VertexPipelineDescription description;
	CHECK(device->Build_Vertex_Description(description));
	CHECK(description.LightCount == 3);
	CHECK(description.Lights[0].Type == D3DLIGHT_DIRECTIONAL);
	CHECK(description.Lights[1].Type == D3DLIGHT_POINT);
	CHECK(description.Lights[2].Type == D3DLIGHT_SPOT);

	device->LightEnable(1, 1);
	device->LightEnable(3, 1);	// five: one more than the generator carries
	CHECK(!device->Build_Vertex_Description(description));
	for (RenderUInt32 index = 0; index < 8; ++index) device->LightEnable(index, 0);
}

static void check_constants(PosixDevice9 *device)
{
	const double world[4][4] = { { 2, 0, 0, 0 }, { 0, 3, 0, 0 }, { 0, 0, 4, 0 }, { 5, 6, 7, 1 } };
	const double view[4][4] = { { 0, 1, 0, 0 }, { -1, 0, 0, 0 }, { 0, 0, 1, 0 }, { 10, 20, 30, 1 } };
	const double projection[4][4] = { { 1.5, 0, 0, 0 }, { 0, 2, 0, 0 }, { 0, 0, 1.01, 1 }, { 0, 0, -0.5, 0 } };
	D3DMATRIX m = matrix_of(world);		device->SetTransform(D3DTS_WORLD, &m);
	m = matrix_of(view);				device->SetTransform(D3DTS_VIEW, &m);
	m = matrix_of(projection);			device->SetTransform(D3DTS_PROJECTION, &m);

	D3DLIGHT9 light;
	memset(&light, 0, sizeof(light));
	light.Type = D3DLIGHT_SPOT;
	light.Position.x = 1; light.Position.y = 2; light.Position.z = 3;
	light.Direction.x = 1; light.Direction.y = 0; light.Direction.z = 0;
	light.Diffuse.r = 0.25f; light.Diffuse.a = 1.0f;
	light.Attenuation0 = 1; light.Attenuation1 = 0.5f; light.Attenuation2 = 0.25f; light.Range = 100;
	light.Theta = 1.0f; light.Phi = 2.0f; light.Falloff = 1.5f;
	device->SetLight(3, &light);
	device->LightEnable(3, 1);

	device->SetRenderState(D3DRS_FOGSTART, 0x42C80000u);		// 100.0f
	device->SetRenderState(D3DRS_FOGEND, 0x44FA0000u);			// 2000.0f
	device->SetRenderState(D3DRS_ALPHAREF, 0x60);
	device->SetRenderState(D3DRS_TEXTUREFACTOR, 0x80FF4000u);	// A 0x80, R 0xFF, G 0x40, B 0x00
	device->SetRenderState(D3DRS_AMBIENT, 0x00336699u);

	SdlVertexConstants vertex;
	SdlPixelConstants pixel;
	device->Build_Constants(vertex, pixel);

	// world * view * projection, in double.
	double world_view[4][4], wvp[4][4];
	for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) {
		world_view[r][c] = 0;
		for (int k = 0; k < 4; ++k) world_view[r][c] += world[r][k] * view[k][c];
	}
	for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) {
		wvp[r][c] = 0;
		for (int k = 0; k < 4; ++k) wvp[r][c] += world_view[r][k] * projection[k][c];
	}
	bool product = true;
	for (int i = 0; i < 16; ++i) product = product && near_value(vertex.WorldViewProjection[i], wvp[i / 4][i % 4])
		&& near_value(vertex.WorldView[i], world_view[i / 4][i % 4]);
	CHECK(product);

	// The normal transform of scale(2, 3, 4) under a rotation of 90 degrees is the rotation with the
	// scale inverted: (inverse(S R))^T = S^-1 R for a rotation R, in D3D's row-vector order.
	const double expected_normal[3][3] = { { 0, 0.5, 0 }, { -1.0 / 3.0, 0, 0 }, { 0, 0, 0.25 } };
	bool normal = true;
	for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) normal = normal && near_value(vertex.NormalTransform[r * 4 + c], expected_normal[r][c]);
	CHECK(normal);

	// The light, packed into slot 0 (the only one enabled), in camera space.
	const float *position = vertex.LightFields[0][0];
	const float *direction = vertex.LightFields[0][1];
	// (1, 2, 3, 1) * view: x = -2 + 10, y = 1 + 20, z = 3 + 30.
	CHECK(near_value(position[0], 8) && near_value(position[1], 21) && near_value(position[2], 33) && near_value(position[3], 1));
	// (1, 0, 0, 0) * view: the translation does not reach a direction.
	CHECK(near_value(direction[0], 0) && near_value(direction[1], 1) && near_value(direction[2], 0) && near_value(direction[3], 0));
	CHECK(near_value(vertex.LightFields[0][2][0], 0.25) && near_value(vertex.LightFields[0][2][3], 1.0));
	CHECK(vertex.LightFields[0][4][0] == 1.0f && vertex.LightFields[0][4][1] == 0.5f && vertex.LightFields[0][4][2] == 0.25f
		&& vertex.LightFields[0][4][3] == 100.0f);
	CHECK(near_value(vertex.LightFields[0][5][0], cos(0.5)) && near_value(vertex.LightFields[0][5][1], cos(1.0))
		&& vertex.LightFields[0][5][2] == 1.5f);

	CHECK(vertex.FogParameters[0] == 100.0f && vertex.FogParameters[1] == 2000.0f);
	CHECK(near_value(vertex.ViewportInverse[0], 1.0 / 64) && near_value(vertex.ViewportInverse[1], 1.0 / 32));
	CHECK(pixel.AlphaReference[0] == 96.0f);
	CHECK(pixel.TextureFactor[0] == 1.0f && near_value(pixel.TextureFactor[1], 0x40 / 255.0)
		&& pixel.TextureFactor[2] == 0.0f && near_value(pixel.TextureFactor[3], 0x80 / 255.0));
	CHECK(near_value(vertex.GlobalAmbient[0], 0x33 / 255.0) && near_value(vertex.GlobalAmbient[2], 0x99 / 255.0));

	// Water must reconstruct world coordinates from the same projection that drew its geometry.
	// Check the inverse by composing it with an independently calculated view/projection product.
	const float water_registers[6][4] = { { 12.5f, 1, 1, 0 }, { -0.8f, -0.6f, 0.9f, 0.7f },
		{ 0.2f, -0.3f, 0.8f, 0 }, { 0.7f, 0.6f, 0.5f, 0 }, { 160, 5120, 159.375f, 1 },
		{ 2.7f, 0.75f, 1.0f, 0 } };
	device->SetPixelShaderConstantF(1, &water_registers[0][0], 6);
	SdlWaterPixelConstants water;
	device->Build_Water_Constants(pixel, water);
	bool inverse = true;
	for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) {
		double identity = 0;
		for (int k = 0; k < 4; ++k) for (int j = 0; j < 4; ++j)
			identity += view[r][j] * projection[j][k] * water.WorldFromClip[k * 4 + c];
		inverse = inverse && fabs(identity - (r == c ? 1.0 : 0.0)) < 0.0001;
	}
	CHECK(inverse);
	// The uploaded forward projection replaces the per-pixel inverse in SSR. Verify it against
	// the independently calculated view/projection, not a second call to the inverse helper.
	for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) {
		double forward = 0;
		for (int j = 0; j < 4; ++j) forward += view[r][j] * projection[j][c];
		CHECK(near_value(water.ClipFromWorld[r * 4 + c], forward));
	}
	CHECK(near_value(water.CameraPosition[0], -20) && near_value(water.CameraPosition[1], 10)
		&& near_value(water.CameraPosition[2], -30));
	CHECK(memcmp(&water.Combiner, &pixel, sizeof(pixel)) == 0);
	CHECK(memcmp(water.WaterParameters, water_registers[0], 16) == 0);
	CHECK(memcmp(water.ReflectionArea, water_registers[1], 16) == 0);
	CHECK(memcmp(water.SunDirection, water_registers[2], 16) == 0);
	CHECK(memcmp(water.SunColour, water_registers[3], 16) == 0);
	CHECK(memcmp(water.TerrainParameters, water_registers[4], 16) == 0);
	CHECK(memcmp(water.OpticalParameters, water_registers[5], 16) == 0);
	// Capability-dependent FFT enablement is supplied only after native preparation succeeds,
	// never inherited from the engine register bank or an earlier ocean draw.
	CHECK(water.FftParameters[0]==0 && water.FftParameters[1]==0);
	// The last vessel's last contour vertex must survive the complete register-to-uniform path.
	// A shorter legacy pixel bank silently loses this end of the runtime hull field.
	float hull_registers[81][4] = {};
	hull_registers[0][0] = 8;
	for (int r=1;r<81;++r) for (int c=0;c<4;++c) hull_registers[r][c]=float(r*4+c)+0.25f;
	CHECK(device->SetPixelShaderConstantF(7,&hull_registers[0][0],81)==D3D_OK);
	float standing_parameters[4]={12.5f,0,1,0};
	device->SetPixelShaderConstantF(1,standing_parameters,1);
	device->Build_Water_Constants(pixel,water);
	CHECK(memcmp(water.HullParameters,hull_registers[0],16)==0);
	CHECK(memcmp(water.HullData,hull_registers[1],sizeof(water.HullData))==0);
	// The optional foam contour is a distinct tail, never a replacement for the mean hull used
	// by displacement. Exercise every register, including the twelfth vertex of hull eight.
	CHECK(sizeof(water)==190*16 && offsetof(SdlWaterPixelConstants,ContactHullData)==100*16);
	float contact_registers[80][4]={};
	for(int r=0;r<80;++r)for(int c=0;c<4;++c)contact_registers[r][c]=-float(r*4+c)-0.75f;
	CHECK(device->SetPixelShaderConstantF(96,&contact_registers[0][0],80)==D3D_OK);
	device->Build_Water_Constants(pixel,water);
	CHECK(memcmp(water.ContactHullData,contact_registers,sizeof(contact_registers))==0);
	CHECK(memcmp(water.HullParameters,hull_registers[0],16)==0);
	CHECK(memcmp(water.HullData,hull_registers[1],sizeof(water.HullData))==0);
	// Fill lights use the old gap before contact registers. Removing lights must explicitly
	// clear their colours/main enable so a later map or time-of-day cannot inherit illumination.
	CHECK(offsetof(SdlWaterPixelConstants,FoamFillLights)==180*16);
	float fill_registers[4][4]={{.6f,0,.8f,1},{.3f,.2f,.1f,0},{0,.8f,.6f,0},{.1f,.2f,.3f,0}};
	CHECK(device->SetPixelShaderConstantF(90,&fill_registers[0][0],4)==D3D_OK);
	device->Build_Water_Constants(pixel,water);
	CHECK(memcmp(water.FoamFillLights,fill_registers,sizeof(fill_registers))==0);
	CHECK(memcmp(water.ContactHullData,contact_registers,sizeof(contact_registers))==0);
	memset(fill_registers,0,sizeof(fill_registers));fill_registers[0][3]=1;
	CHECK(device->SetPixelShaderConstantF(90,&fill_registers[0][0],4)==D3D_OK);
	device->Build_Water_Constants(pixel,water);CHECK(memcmp(water.FoamFillLights,fill_registers,sizeof(fill_registers))==0);
	// Material state has explicit registers between fill lights and contact geometry.
	// It must survive packing without borrowing light components, and clear on the next draw.
	const float material[2][4]={{45/255.0f,58/255.0f,11/255.0f,.67f},{1,.8f,1,0}};
	CHECK(device->SetPixelShaderConstantF(ModernWaterMaterial::RegisterColor,material[0],2)==D3D_OK);
	device->Build_Water_Constants(pixel,water);
	CHECK(memcmp(water.MaterialColor,material[0],16)==0 && memcmp(water.MaterialParameters,material[1],16)==0);
	CHECK(memcmp(water.ContactHullData,contact_registers,sizeof(contact_registers))==0);
	CHECK(memcmp(water.FoamFillLights,fill_registers,sizeof(fill_registers))==0);
	const float clearMaterial[2][4]={};
	CHECK(device->SetPixelShaderConstantF(ModernWaterMaterial::RegisterColor,clearMaterial[0],2)==D3D_OK);
	device->Build_Water_Constants(pixel,water);
	CHECK(memcmp(water.MaterialColor,clearMaterial[0],16)==0 && memcmp(water.MaterialParameters,clearMaterial[1],16)==0);
	fill_registers[0][3]=0;CHECK(device->SetPixelShaderConstantF(90,&fill_registers[0][0],4)==D3D_OK);
	device->Build_Water_Constants(pixel,water);CHECK(memcmp(water.FoamFillLights,fill_registers,sizeof(fill_registers))==0);
	CHECK(device->SetPixelShaderConstantF(175,contact_registers[79],1)==D3D_OK);
	CHECK(device->SetPixelShaderConstantF(WaterHullVisualLayout::PixelRegisterCount,contact_registers[79],1)==D3DERR_INVALIDCALL);
	// Exact mesh transport extends the emulated bank deliberately, without moving the old
	// 186-register native vertex prefix or copying the reserved engine-register gap into metadata.
	using namespace WaterHullVisualLayout;
	CHECK(PosixDevice9::PIXEL_SHADER_CONSTANT_COUNT==896);
	CHECK(sizeof(SdlWaterVisualPixelConstants)==198*16 && offsetof(SdlWaterVisualPixelConstants,VisualHullMeta)==190*16);
	CHECK(VertexCommonRegisters==186 && FragmentCommonRegisters==194);
	CHECK(BlockOffset(0)==0 && BlockOffset(1)==256 && BlockOffset(2)==512);
	CHECK(BlockRegisters(0)==256 && BlockRegisters(1)==256 && BlockRegisters(2)==192);
	float visualMeta[8][4]={};float visualPacket[704][4]={};float reserved[8][4]={};
	for(unsigned h=0;h<8;++h)for(unsigned c=0;c<4;++c){visualMeta[h][c]=float(h*4+c)+.5f;reserved[h][c]=-99;}
	for(unsigned r=0;r<704;++r)for(unsigned c=0;c<4;++c)visualPacket[r][c]=float(r*4+c)+.25f;
	CHECK(device->SetPixelShaderConstantF(176,visualMeta[0],8)==D3D_OK);
	CHECK(device->SetPixelShaderConstantF(184,reserved[0],8)==D3D_OK);
	CHECK(device->SetPixelShaderConstantF(192,visualPacket[0],704)==D3D_OK);
	CHECK(device->SetPixelShaderConstantF(895,visualPacket[703],1)==D3D_OK);
	CHECK(device->SetPixelShaderConstantF(895,visualPacket[703],2)==D3DERR_INVALIDCALL);
	CHECK(device->SetPixelShaderConstantF(896,visualPacket[703],1)==D3DERR_INVALIDCALL);
	SdlWaterVisualPixelConstants visual;
	device->Build_Water_Visual_Constants(water,visual);
	CHECK(memcmp(&visual.Water,&water,sizeof(water))==0);
	CHECK(memcmp(visual.VisualHullMeta,visualMeta,sizeof(visualMeta))==0);
	// After an ocean, missing/zero metadata must replace every previously populated slot.
	memset(visualMeta,0,sizeof(visualMeta));CHECK(device->SetPixelShaderConstantF(176,visualMeta[0],8)==D3D_OK);
	device->Build_Water_Visual_Constants(water,visual);CHECK(memcmp(visual.VisualHullMeta,visualMeta,sizeof(visualMeta))==0);
	// The river/empty-hull safeguard is independent of the caller remembering to clear metadata.
	CHECK(device->SetPixelShaderConstantF(176,reserved[0],8)==D3D_OK);
	SdlWaterPixelConstants emptyWater=water;emptyWater.HullParameters[0]=0;
	device->Build_Water_Visual_Constants(emptyWater,visual);CHECK(memcmp(visual.VisualHullMeta,visualMeta,sizeof(visualMeta))==0);
	CHECK(device->SetPixelShaderConstantF(176,visualMeta[0],8)==D3D_OK);
	// Sparse unsupported layouts cannot read bindings left behind by a different draw.
	for(unsigned vs=0;vs<5;++vs)for(unsigned ps=0;ps<6;++ps)for(unsigned wv=0;wv<2;++wv)for(unsigned wp=0;wp<2;++wp) {
		const bool expected=(vs<=1 || (wv && vs==2)) && (ps<=1 || (wp && ps==4));
		CHECK(Sdl_Draw_Uniform_Buffers_Supported(wv,wp,vs,ps)==expected);
	}

	// Missing/failed contact geometry is uploaded as a complete zero block. A reused uniform
	// object must not retain any preceding contour after the engine explicitly disables it.
	memset(contact_registers,0,sizeof(contact_registers));
	CHECK(device->SetPixelShaderConstantF(96,&contact_registers[0][0],80)==D3D_OK);
	device->Build_Water_Constants(pixel,water);
	CHECK(memcmp(water.ContactHullData,contact_registers,sizeof(contact_registers))==0);
	CHECK(memcmp(water.HullData,hull_registers[1],sizeof(water.HullData))==0);
	// New flat meshes repurpose UV0 only when this per-draw flag reaches the shader. A legacy
	// grid immediately following them must clear the flag while retaining all other optical data.
	float correction_optics[4]={2.7f,.75f,1,1};
	CHECK(device->SetPixelShaderConstantF(6,correction_optics,1)==D3D_OK);
	device->Build_Water_Constants(pixel,water);
	CHECK(memcmp(water.OpticalParameters,correction_optics,sizeof(correction_optics))==0);
	correction_optics[3]=0;
	CHECK(device->SetPixelShaderConstantF(6,correction_optics,1)==D3D_OK);
	device->Build_Water_Constants(pixel,water);
	CHECK(water.OpticalParameters[3]==0 && memcmp(water.OpticalParameters,water_registers[5],16)==0);
	CHECK(device->SetPixelShaderConstantF(PosixDevice9::PIXEL_SHADER_CONSTANT_COUNT-1,
		&hull_registers[0][0],2)==D3DERR_INVALIDCALL);
	device->SetPixelShaderConstantF(1,water_registers[0],1);
	device->Build_Water_Constants(pixel,water);
	CHECK(water.HullParameters[0]==0); // river following an ocean draw cannot reuse vessel foam
	CHECK(near_value(water.Viewport[2], 64) && near_value(water.Viewport[3], 32));
	// Scene depth is the hardware viewport depth, including a nondefault MinZ/MaxZ. Project a known
	// point independently and reconstruct it, as both surface and submerged-object samples must do.
	D3DVIEWPORT9 depth_viewport;
	device->GetViewport(&depth_viewport);
	depth_viewport.MinZ = 0.2f;
	depth_viewport.MaxZ = 0.85f;
	device->SetViewport(&depth_viewport);
	device->Build_Water_Constants(pixel, water);
	const double world_point[4] = { 17, -9, 53, 1 };
	double clip[4] = {};
	for (unsigned int c = 0; c < 4; ++c)
		for (unsigned int r = 0; r < 4; ++r)
			for (unsigned int j = 0; j < 4; ++j) clip[c] += world_point[r] * view[r][j] * projection[j][c];
	const double device_point[4] = { clip[0] / clip[3], clip[1] / clip[3],
		depth_viewport.MinZ + clip[2] / clip[3] * (depth_viewport.MaxZ - depth_viewport.MinZ), 1 };
	double reconstructed[4] = {};
	for (unsigned int c = 0; c < 4; ++c)
		for (unsigned int r = 0; r < 4; ++r) reconstructed[c] += device_point[r] * water.WorldFromClip[r * 4 + c];
	for (unsigned int c = 0; c < 3; ++c) CHECK(near_value(reconstructed[c] / reconstructed[3], world_point[c]));
	depth_viewport.MinZ = 0;
	depth_viewport.MaxZ = 1;
	device->SetViewport(&depth_viewport);
	memset(&m, 0, sizeof(m));
	device->SetTransform(D3DTS_PROJECTION, &m);
	device->Build_Water_Constants(pixel, water);
	bool fallback = true;
	for (int i = 0; i < 16; ++i) fallback = fallback && water.WorldFromClip[i] == (i % 5 == 0 ? 1.0f : 0.0f);
	CHECK(fallback);
}

static void check_foam_history_constants(PosixDevice9 *device)
{
	// The two new registers fit after all eight complete hulls. A stale sampler binding or
	// missing runtime texture must never activate history on the next material or water level.
	SdlPixelConstants pixel={};SdlWaterPixelConstants water;
	float parameters[4]={24.5f,0,1,0};
	float history[2][4]={{-128,64,1.0f/1024,1.0f/512},{1,7,2,24.25f}};
	CHECK(device->SetPixelShaderConstantF(1,parameters,1)==D3D_OK);
	CHECK(device->SetPixelShaderConstantF(88,history[0],2)==D3D_OK);
	device->Build_Water_Constants(pixel,water);
	CHECK(water.FoamHistoryParameters[0]==0 && !device->Water_Foam_History_Available());
	IDirect3DTexture9 *texture=NULL,*attachment=NULL;
	CHECK(device->CreateTexture(4,4,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,NULL)==D3D_OK);
	CHECK(device->CreateTexture(4,4,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&attachment,NULL)==D3D_OK);
	CHECK(device->SetTexture(14,texture)==D3D_OK);
	IDirect3DBaseTexture9 *held=NULL;CHECK(device->GetTexture(14,&held)==D3D_OK && held==texture);if(held)held->Release();
	device->Build_Water_Constants(pixel,water);
	CHECK(device->Water_Foam_History_Available());
	CHECK(memcmp(water.FoamHistoryArea,history[0],16)==0 && memcmp(water.FoamHistoryParameters,history[1],16)==0);
	CHECK(water.HullData[79][3]==320.0f+3.25f); // the prior test's last vertex survived c88/c89
	parameters[1]=1;device->SetPixelShaderConstantF(1,parameters,1);device->Build_Water_Constants(pixel,water);
	CHECK(water.FoamHistoryParameters[0]==0 && water.HullParameters[0]==0);
	parameters[1]=0;device->SetPixelShaderConstantF(1,parameters,1);
	for(unsigned row=0;row<2;++row)for(unsigned c=0;c<4;++c) {
		const float saved=history[row][c];history[row][c]=NAN;
		device->SetPixelShaderConstantF(88,history[0],2);device->Build_Water_Constants(pixel,water);
		CHECK(water.FoamHistoryParameters[0]==0);history[row][c]=saved;
	}
	for(unsigned field=0;field<3;++field) {
		float *value=field<2?&history[0][2+field]:&history[1][2];const float saved=*value;*value=0;
		device->SetPixelShaderConstantF(88,history[0],2);device->Build_Water_Constants(pixel,water);
		CHECK(water.FoamHistoryParameters[0]==0);*value=saved;
	}
	device->SetPixelShaderConstantF(88,history[0],2);CHECK(device->SetTexture(14,attachment)==D3D_OK);
	device->Build_Water_Constants(pixel,water);CHECK(water.FoamHistoryParameters[0]==0);
	CHECK(device->SetTexture(14,texture)==D3D_OK);device->Build_Water_Constants(pixel,water);CHECK(water.FoamHistoryParameters[0]==1);
	for(float flags:{2.f,3.f,5.f,6.f,7.f}) {
		history[1][0]=flags;device->SetPixelShaderConstantF(88,history[0],2);device->Build_Water_Constants(pixel,water);
		CHECK(device->Water_Foam_History_Available() && water.FoamHistoryParameters[0]==flags);
	}
	for(float flags:{4.f,8.f,5.5f}) {
		history[1][0]=flags;device->SetPixelShaderConstantF(88,history[0],2);device->Build_Water_Constants(pixel,water);
		CHECK(!device->Water_Foam_History_Available() && water.FoamHistoryParameters[0]==0);
	}
	IDirect3DTexture9 *odd=NULL;
	CHECK(device->CreateTexture(4,3,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&odd,NULL)==D3D_OK);
	CHECK(device->SetTexture(14,odd)==D3D_OK);
	history[1][0]=5;device->SetPixelShaderConstantF(88,history[0],2);CHECK(!device->Water_Foam_History_Available());
	history[1][0]=1;device->SetPixelShaderConstantF(88,history[0],2);CHECK(device->Water_Foam_History_Available());
	if(odd)odd->Release();
	CHECK(device->SetTexture(14,NULL)==D3D_OK);device->Build_Water_Constants(pixel,water);CHECK(water.FoamHistoryParameters[0]==0);
	if(texture)texture->Release();if(attachment)attachment->Release();
	printf("posix_draw_resolve_selfcheck: history c88/c89, sampler14, invalid mapping, missing texture, river and attachment fallback checked\n");
}

int main()
{
	IDirect3D9 *d3d = NULL;
	PosixDevice9 *device = make_device(d3d);
	CHECK(device != NULL);
	if (device == NULL) {
		printf("posix_draw_resolve_selfcheck: no headless device\n");
		return 1;
	}
	CHECK(device->Get_Gpu() == NULL);	// headless: no GPU device
	check_defaults(device);
	check_combiner(device);
	check_lights(device);
	check_constants(device);
	check_foam_history_constants(device);
	device->Release();
	d3d->Release();
	if (failures != 0) {
		printf("posix_draw_resolve_selfcheck: %d FAILED\n", failures);
		return 1;
	}
	printf("posix_draw_resolve_selfcheck: defaults, combiner walk, lights and constants all hold\n");
	return 0;
}
