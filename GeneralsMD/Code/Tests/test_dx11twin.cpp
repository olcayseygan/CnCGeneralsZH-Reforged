/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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

// The moved lock.
//
// The engine writes its vertices and indices once, into a write-only D3D9 buffer that nothing can
// read back, so the only chance to build the D3D11 copy is while the write is happening.  What has
// to hold is that the D3D9 side ends up byte for byte what it would have been without the twin and
// the D3D11 side ends up holding the same thing.  Both sides are read back here: the D3D9 one is a
// heap block standing in for the mapped pointer, and the D3D11 one comes back through a staging
// buffer, which is the only way to see what UpdateSubresource actually wrote.

#include "test_harness.h"

#include "dx11device.h"
#include "dx11twin.h"

#include <d3d9.h>
#include <d3d11.h>
#include <string.h>

static const unsigned BUFFER_BYTES = 256;
static const unsigned RANGE_OFFSET = 64;
static const unsigned RANGE_BYTES = 32;
static const unsigned char UNWRITTEN_BYTE = 0xCD;

// What UpdateSubresource left in the buffer.  A default-usage buffer cannot be mapped, so the read
// goes through a staging copy of it.
static bool read_back(DX11DeviceClass & device, ID3D11Buffer * buffer, unsigned char * out,
	unsigned byte_count)
{
	D3D11_BUFFER_DESC desc;
	memset(&desc, 0, sizeof(desc));
	desc.ByteWidth = byte_count;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	ID3D11Buffer * staging = NULL;
	if (FAILED(device.Get_Device()->CreateBuffer(&desc, NULL, &staging))) {
		return false;
	}

	device.Get_Context()->CopyResource(staging, buffer);

	D3D11_MAPPED_SUBRESOURCE mapped;
	const bool mapped_ok =
		SUCCEEDED(device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped));
	if (mapped_ok) {
		memcpy(out, mapped.pData, byte_count);
		device.Get_Context()->Unmap(staging, 0);
	}

	staging->Release();
	return mapped_ok;
}

// Without -dx11 there is no twin, and a lock that asks for one has to come back with nothing so
// every caller keeps the pointer D3D9 gave it.  Ending a lock that never began is the same path a
// buffer takes on an ordinary run, several thousand times a frame.
TEST(dx11twin_a_lock_without_a_twin_hands_back_nothing)
{
	unsigned char d3d9_memory[BUFFER_BYTES];
	memset(d3d9_memory, UNWRITTEN_BYTE, sizeof(d3d9_memory));

	DX11BufferLockClass lock;
	CHECK(lock.Begin(NULL, d3d9_memory, 0, 0, 0) == NULL);
	lock.End();

	CHECK_EQ(d3d9_memory[0], UNWRITTEN_BYTE);
}

// A whole-buffer write, which is what a mesh does once when it loads.  Zero for the count means to
// the end, the way D3D9's own lock reads it.
TEST(dx11twin_a_whole_buffer_write_lands_on_both_sides)
{
	DX11DeviceClass device;
	device.Request_Debug_Layer();
	CHECK(device.Create_Offscreen());

	DX11BufferTwinClass * twin =
		DX11Twin_Create_Vertex_Buffer(device.Get_Device(), device.Get_Context(), BUFFER_BYTES, false);
	CHECK(twin != NULL);

	unsigned char d3d9_memory[BUFFER_BYTES];
	memset(d3d9_memory, UNWRITTEN_BYTE, sizeof(d3d9_memory));

	DX11BufferLockClass lock;
	unsigned char * write = (unsigned char *)lock.Begin(twin, d3d9_memory, 0, 0, 0);
	CHECK(write == twin->Mirror());
	for (unsigned i = 0; i < BUFFER_BYTES; ++i) {
		write[i] = (unsigned char)i;
	}
	lock.End();

	unsigned char d3d11_memory[BUFFER_BYTES];
	CHECK(read_back(device, twin->Buffer(), d3d11_memory, BUFFER_BYTES));

	for (unsigned i = 0; i < BUFFER_BYTES; ++i) {
		CHECK_EQ(d3d9_memory[i], (unsigned char)i);
		CHECK_EQ(d3d11_memory[i], (unsigned char)i);
	}

	delete twin;
}

// A D3D9 lock that failed hands the twin no memory. The write still reaches the D3D11 buffer, and End
// used to copy the mirror into the null pointer on the way.
TEST(dx11twin_a_failed_d3d9_lock_still_uploads_the_write)
{
	DX11DeviceClass device;
	device.Request_Debug_Layer();
	CHECK(device.Create_Offscreen());

	DX11BufferTwinClass * twin =
		DX11Twin_Create_Vertex_Buffer(device.Get_Device(), device.Get_Context(), BUFFER_BYTES, false);
	CHECK(twin != NULL);

	DX11BufferLockClass lock;
	unsigned char * write = (unsigned char *)lock.Begin(twin, NULL, 0, 0, 0);
	CHECK(write != NULL);
	for (unsigned i = 0; i < BUFFER_BYTES; ++i) {
		write[i] = (unsigned char)(i ^ 0x5A);
	}
	lock.End();

	unsigned char d3d11_memory[BUFFER_BYTES];
	CHECK(read_back(device, twin->Buffer(), d3d11_memory, BUFFER_BYTES));
	for (unsigned i = 0; i < BUFFER_BYTES; ++i) {
		CHECK_EQ(d3d11_memory[i], (unsigned char)(i ^ 0x5A));
	}

	delete twin;
}

// The dynamic buffers, which are refilled hundreds of times a frame and are the reason the upload
// has two shapes.  A partial UpdateSubresource cannot rename the buffer, so it waits for every draw
// still reading it; mapping with D3D9's own DISCARD and NOOVERWRITE does not.  What has to hold is
// that the second, non-discarding write leaves the first one intact, because that is exactly what
// the engine assumes when it fills one dynamic buffer in batches and draws between them.
TEST(dx11twin_a_dynamic_buffer_keeps_what_a_no_overwrite_write_did_not_touch)
{
	DX11DeviceClass device;
	device.Request_Debug_Layer();
	CHECK(device.Create_Offscreen());

	DX11BufferTwinClass * twin =
		DX11Twin_Create_Vertex_Buffer(device.Get_Device(), device.Get_Context(), BUFFER_BYTES, true);
	CHECK(twin != NULL);

	unsigned char d3d9_memory[BUFFER_BYTES];
	memset(d3d9_memory, UNWRITTEN_BYTE, sizeof(d3d9_memory));

	DX11BufferLockClass first;
	memset(first.Begin(twin, d3d9_memory, 0, RANGE_BYTES, D3DLOCK_DISCARD), 0x11, RANGE_BYTES);
	first.End();

	DX11BufferLockClass second;
	memset(second.Begin(twin, d3d9_memory + RANGE_OFFSET, RANGE_OFFSET, RANGE_BYTES,
		D3DLOCK_NOOVERWRITE), 0x22, RANGE_BYTES);
	second.End();

	unsigned char d3d11_memory[BUFFER_BYTES];
	CHECK(read_back(device, twin->Buffer(), d3d11_memory, BUFFER_BYTES));

	for (unsigned i = 0; i < RANGE_BYTES; ++i) {
		CHECK_EQ(d3d11_memory[i], 0x11);
		CHECK_EQ(d3d11_memory[RANGE_OFFSET + i], 0x22);
		CHECK_EQ(d3d9_memory[i], 0x11);
		CHECK_EQ(d3d9_memory[RANGE_OFFSET + i], 0x22);
	}

	delete twin;
}

// A dynamic lock while Direct3D 11 presents asks End not to copy into the D3D9 memory, because
// Direct3D 9 skips its indexed draws then and nothing reads a D3D9 buffer back.  The D3D11 buffer
// still has to get the write, and the D3D9 block must come back exactly as it was lent.
TEST(dx11twin_a_lock_that_skips_d3d9_still_uploads_and_leaves_d3d9_alone)
{
	DX11DeviceClass device;
	device.Request_Debug_Layer();
	CHECK(device.Create_Offscreen());

	DX11BufferTwinClass * twin =
		DX11Twin_Create_Vertex_Buffer(device.Get_Device(), device.Get_Context(), BUFFER_BYTES, true);
	CHECK(twin != NULL);

	unsigned char d3d9_memory[BUFFER_BYTES];
	memset(d3d9_memory, UNWRITTEN_BYTE, sizeof(d3d9_memory));

	DX11BufferLockClass lock;
	memset(lock.Begin(twin, d3d9_memory, 0, RANGE_BYTES, D3DLOCK_DISCARD, false), 0x33, RANGE_BYTES);
	lock.End();

	unsigned char d3d11_memory[BUFFER_BYTES];
	CHECK(read_back(device, twin->Buffer(), d3d11_memory, BUFFER_BYTES));

	for (unsigned i = 0; i < RANGE_BYTES; ++i) {
		CHECK_EQ(d3d11_memory[i], 0x33);
	}
	for (unsigned i = 0; i < BUFFER_BYTES; ++i) {
		CHECK_EQ(d3d9_memory[i], UNWRITTEN_BYTE);
	}

	delete twin;
}

// That lock writes straight into the mapped D3D11 buffer rather than the mirror.  A second,
// non-discarding write to another range has to leave the first intact, because the engine fills
// one dynamic buffer in batches and draws between them.
TEST(dx11twin_direct_dynamic_writes_keep_what_a_no_overwrite_write_did_not_touch)
{
	DX11DeviceClass device;
	device.Request_Debug_Layer();
	CHECK(device.Create_Offscreen());

	DX11BufferTwinClass * twin =
		DX11Twin_Create_Vertex_Buffer(device.Get_Device(), device.Get_Context(), BUFFER_BYTES, true);
	CHECK(twin != NULL);

	unsigned char d3d9_memory[BUFFER_BYTES];
	memset(d3d9_memory, UNWRITTEN_BYTE, sizeof(d3d9_memory));

	DX11BufferLockClass first;
	unsigned char * first_write =
		(unsigned char *)first.Begin(twin, d3d9_memory, 0, RANGE_BYTES, D3DLOCK_DISCARD, false);
	CHECK(first_write != twin->Mirror());
	memset(first_write, 0x44, RANGE_BYTES);
	first.End();

	DX11BufferLockClass second;
	memset(second.Begin(twin, d3d9_memory + RANGE_OFFSET, RANGE_OFFSET, RANGE_BYTES,
		D3DLOCK_NOOVERWRITE, false), 0x55, RANGE_BYTES);
	second.End();

	unsigned char d3d11_memory[BUFFER_BYTES];
	CHECK(read_back(device, twin->Buffer(), d3d11_memory, BUFFER_BYTES));

	for (unsigned i = 0; i < RANGE_BYTES; ++i) {
		CHECK_EQ(d3d11_memory[i], 0x44);
		CHECK_EQ(d3d11_memory[RANGE_OFFSET + i], 0x55);
	}
	for (unsigned i = 0; i < BUFFER_BYTES; ++i) {
		CHECK_EQ(d3d9_memory[i], UNWRITTEN_BYTE);
	}

	delete twin;
}

// An append lock, which is how the terrain and the dynamic buffers fill one region at a time.  The
// pointer starts at the offset, and everything outside the range is left as it was - a twin that
// uploaded the whole buffer on every partial write would still draw correctly and would cost the
// bandwidth of the entire buffer per lock.
TEST(dx11twin_a_ranged_write_touches_only_its_range)
{
	DX11DeviceClass device;
	device.Request_Debug_Layer();
	CHECK(device.Create_Offscreen());

	DX11BufferTwinClass * twin =
		DX11Twin_Create_Index_Buffer(device.Get_Device(), device.Get_Context(), BUFFER_BYTES, false);
	CHECK(twin != NULL);

	memset(twin->Mirror(), 0, BUFFER_BYTES);
	twin->Upload(0, 0, false);

	unsigned char d3d9_memory[BUFFER_BYTES];
	memset(d3d9_memory, UNWRITTEN_BYTE, sizeof(d3d9_memory));

	DX11BufferLockClass lock;
	unsigned char * write =
		(unsigned char *)lock.Begin(twin, d3d9_memory + RANGE_OFFSET, RANGE_OFFSET, RANGE_BYTES, 0);
	CHECK(write == twin->Mirror() + RANGE_OFFSET);
	memset(write, 0x7F, RANGE_BYTES);
	lock.End();

	unsigned char d3d11_memory[BUFFER_BYTES];
	CHECK(read_back(device, twin->Buffer(), d3d11_memory, BUFFER_BYTES));

	for (unsigned i = 0; i < BUFFER_BYTES; ++i) {
		const bool in_range = i >= RANGE_OFFSET && i < RANGE_OFFSET + RANGE_BYTES;
		CHECK_EQ(d3d9_memory[i], in_range ? 0x7F : UNWRITTEN_BYTE);
		CHECK_EQ(d3d11_memory[i], in_range ? 0x7F : 0x00);
	}

	delete twin;
}
