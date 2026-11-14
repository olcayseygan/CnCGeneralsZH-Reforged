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
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : ww3d                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/dx8indexbuffer.cpp                     $*
 *                                                                                             *
 *              Original Author:: Jani Penttinen                                               *
 *                                                                                             *
 *                      $Author:: Jani_p                                                      $*
 *                                                                                             *
 *                     $Modtime:: 11/09/01 3:12p                                              $*
 *                                                                                             *
 *                    $Revision:: 26                                                          $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

//#define INDEX_BUFFER_LOG

#include "dx8indexbuffer.h"
#include "dx8wrapper.h"
#include "dx8caps.h"
#include "dx11runtime.h"
#include "sphere.h"
#include "thread.h"
#include "wwmemlog.h"

// Made full size once, for the reason DEFAULT_VB_SIZE in dx8vertexbuffer.cpp gives: each time the
// buffer fills, the next lock is a DISCARD the Direct3D 11 driver has to rename.
#define DEFAULT_IB_SIZE 65535

static bool _DynamicSortingIndexArrayInUse=false;
static SortingIndexBufferClass* _DynamicSortingIndexArray;
static unsigned short _DynamicSortingIndexArraySize=0;

// Full sorting index arrays, handed out again once the flush has let go of them.  The vertex
// arrays in dx8vertexbuffer.cpp say why: operator new[] zeroes what it allocates, and replacing a
// full array with a freshly sized one every few particle systems was a tenth of a busy frame.
static const unsigned short SORTING_INDEX_ARRAY_SIZE=65535;
static const int MAX_RETIRED_SORTING_INDEX_ARRAYS=16;
static SortingIndexBufferClass* _RetiredSortingIndexArrays[MAX_RETIRED_SORTING_INDEX_ARRAYS];
static unsigned short _DynamicSortingIndexArrayOffset=0;	

static bool _DynamicDX8IndexBufferInUse=false;
static DX8IndexBufferClass* _DynamicDX8IndexBuffer=NULL;
static unsigned short _DynamicDX8IndexBufferSize=DEFAULT_IB_SIZE;
static unsigned short _DynamicDX8IndexBufferOffset=0;	

static int _IndexBufferCount;
static int _IndexBufferTotalIndices;
static int _IndexBufferTotalSize;

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

IndexBufferClass::IndexBufferClass(unsigned type_, unsigned short index_count_)
	:
	index_count(index_count_),
	type(type_),
	engine_refs(0)
{
	WWASSERT(type==BUFFER_TYPE_DX8 || type==BUFFER_TYPE_SORTING);
	WWASSERT(index_count);

	_IndexBufferCount++;
	_IndexBufferTotalIndices+=index_count;
	_IndexBufferTotalSize+=index_count*sizeof(unsigned short);
#ifdef VERTEX_BUFFER_LOG
	WWDEBUG_SAY(("New IB, %d indices, size %d bytes\n",index_count,index_count*sizeof(unsigned short)));
	WWDEBUG_SAY(("Total IB count: %d, total %d indices, total size %d bytes\n",
		_IndexBufferCount,
		_IndexBufferTotalIndices,
		_IndexBufferTotalSize));
#endif
}

IndexBufferClass::~IndexBufferClass()
{
	_IndexBufferCount--;
	_IndexBufferTotalIndices-=index_count;
	_IndexBufferTotalSize-=index_count*sizeof(unsigned short);
#ifdef VERTEX_BUFFER_LOG
	WWDEBUG_SAY(("Delete IB, %d indices, size %d bytes\n",index_count,index_count*sizeof(unsigned short)));
	WWDEBUG_SAY(("Total IB count: %d, total %d indices, total size %d bytes\n",
		_IndexBufferCount,
		_IndexBufferTotalIndices,
		_IndexBufferTotalSize));
#endif
}

unsigned IndexBufferClass::Get_Total_Buffer_Count()
{
	return _IndexBufferCount;
}

unsigned IndexBufferClass::Get_Total_Allocated_Indices()
{
	return _IndexBufferTotalIndices;
}

unsigned IndexBufferClass::Get_Total_Allocated_Memory()
{
	return _IndexBufferTotalSize;
}

void IndexBufferClass::Add_Engine_Ref() const
{
	engine_refs++; 
}

void IndexBufferClass::Release_Engine_Ref() const
{
	engine_refs--;
	WWASSERT(engine_refs>=0);
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

void IndexBufferClass::Copy(unsigned int* indices,unsigned first_index,unsigned count)
{
	WWASSERT(indices);

	if (first_index) {
		DX8IndexBufferClass::AppendLockClass l(this,first_index,count);
		unsigned short* inds=l.Get_Index_Array();
		for (unsigned v=0;v<count;++v) {
			*inds++=(unsigned short)(*indices++);
		}
	}
	else {
		DX8IndexBufferClass::WriteLockClass l(this);
		unsigned short* inds=l.Get_Index_Array();
		for (unsigned v=0;v<count;++v) {
			*inds++=(unsigned short)(*indices++);
		}
	}
}

// ----------------------------------------------------------------------------

void IndexBufferClass::Copy(unsigned short* indices,unsigned first_index,unsigned count)
{
	WWASSERT(indices);

	if (first_index) {
		DX8IndexBufferClass::AppendLockClass l(this,first_index,count);
		unsigned short* inds=l.Get_Index_Array();
		for (unsigned v=0;v<count;++v) {
			*inds++=*indices++;
		}
	}
	else {
		DX8IndexBufferClass::WriteLockClass l(this);
		unsigned short* inds=l.Get_Index_Array();
		for (unsigned v=0;v<count;++v) {
			*inds++=*indices++;
		}
	}
}

// ----------------------------------------------------------------------------
//
//
// ----------------------------------------------------------------------------


IndexBufferClass::WriteLockClass::WriteLockClass(IndexBufferClass* index_buffer_, int flags) : index_buffer(index_buffer_)
{
	DX8_THREAD_ASSERT();
	WWASSERT(index_buffer);
	WWASSERT(!index_buffer->Engine_Refs());
	index_buffer->Add_Ref();
	switch (index_buffer->Type()) {
	case BUFFER_TYPE_DX8:
		//	No device means no D3D buffer; write into the scratch block instead. See
		//	DX8IndexBufferClass::Get_Scratch_Indices.
		if (static_cast<DX8IndexBufferClass*>(index_buffer)->Get_DX8_Index_Buffer()==NULL) {
			indices=static_cast<DX8IndexBufferClass*>(index_buffer)->Get_Scratch_Indices();
			break;
		}
		DX8_Assert();
		indices=NULL;	// a failed Lock need not write it, and the D3D11 twin's End copies into it
		DX8_ErrorCode(static_cast<DX8IndexBufferClass*>(index_buffer)->Get_DX8_Index_Buffer()->Lock(
			0,
			index_buffer->Get_Index_Count()*sizeof(WORD),
			(void**)&indices,
			flags));
		{
			void* mirror=dx11_lock.Begin(
				static_cast<DX8IndexBufferClass*>(index_buffer)->Get_DX11_Twin(),indices,0,0,flags);
			if (mirror) indices=(unsigned short*)mirror;
		}
		break;
	case BUFFER_TYPE_SORTING:
		indices=static_cast<SortingIndexBufferClass*>(index_buffer)->index_buffer;
		break;
	default:
		WWASSERT(0);
		break;
	}
}

// ----------------------------------------------------------------------------
//
//
// ----------------------------------------------------------------------------

IndexBufferClass::WriteLockClass::~WriteLockClass()
{
	DX8_THREAD_ASSERT();
	switch (index_buffer->Type()) {
	case BUFFER_TYPE_DX8:
		if (static_cast<DX8IndexBufferClass*>(index_buffer)->index_buffer==NULL) break;
		DX8_Assert();
		dx11_lock.End();
		DX8_ErrorCode(static_cast<DX8IndexBufferClass*>(index_buffer)->index_buffer->Unlock());
		break;
	case BUFFER_TYPE_SORTING:
		break;
	default:
		WWASSERT(0);
		break;
	}
	index_buffer->Release_Ref();
}

// ----------------------------------------------------------------------------

IndexBufferClass::AppendLockClass::AppendLockClass(IndexBufferClass* index_buffer_,unsigned start_index, unsigned index_range)
	:
	index_buffer(index_buffer_)
{
	DX8_THREAD_ASSERT();
	WWASSERT(start_index+index_range<=index_buffer->Get_Index_Count());
	WWASSERT(index_buffer);
	WWASSERT(!index_buffer->Engine_Refs());
	index_buffer->Add_Ref();
	switch (index_buffer->Type()) {
	case BUFFER_TYPE_DX8:
		DX8_Assert();
		if (static_cast<DX8IndexBufferClass*>(index_buffer)->index_buffer==NULL) {
			indices=static_cast<DX8IndexBufferClass*>(index_buffer)->Get_Scratch_Indices()+start_index;
			break;
		}
		indices=NULL;	// as above
		DX8_ErrorCode(static_cast<DX8IndexBufferClass*>(index_buffer)->index_buffer->Lock(
			start_index*sizeof(unsigned short),
			index_range*sizeof(unsigned short),
			(void**)&indices,
			0));
		{
			void* mirror=dx11_lock.Begin(
				static_cast<DX8IndexBufferClass*>(index_buffer)->Get_DX11_Twin(),
				indices,
				start_index*sizeof(unsigned short),
				index_range*sizeof(unsigned short),
				0);	// Default (no) flags, as above
			if (mirror) indices=(unsigned short*)mirror;
		}
		break;
	case BUFFER_TYPE_SORTING:
		indices=static_cast<SortingIndexBufferClass*>(index_buffer)->index_buffer+start_index;
		break;
	default:
		WWASSERT(0);
		break;
	}
}

// ----------------------------------------------------------------------------

IndexBufferClass::AppendLockClass::~AppendLockClass()
{
	DX8_THREAD_ASSERT();
	switch (index_buffer->Type()) {
	case BUFFER_TYPE_DX8:
		if (static_cast<DX8IndexBufferClass*>(index_buffer)->index_buffer==NULL) break;
		DX8_Assert();
		dx11_lock.End();
		DX8_ErrorCode(static_cast<DX8IndexBufferClass*>(index_buffer)->index_buffer->Unlock());
		break;
	case BUFFER_TYPE_SORTING:
		break;
	default:
		WWASSERT(0);
		break;
	}
	index_buffer->Release_Ref();
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

DX8IndexBufferClass::DX8IndexBufferClass(unsigned short index_count_,UsageType usage)
	:
	IndexBufferClass(BUFFER_TYPE_DX8,index_count_)
{
	DX8_THREAD_ASSERT();
	WWASSERT(index_count);
	index_buffer=NULL;
	scratch_indices=NULL;
	dx11_twin=NULL;

	//	Same as the vertex buffer: no render device, no D3D buffer.  See Create_Vertex_Buffer.
	if (DX8Wrapper::_Get_D3D_Device() == NULL) {
		scratch_indices=W3DNEWARRAY unsigned short[index_count];
		return;
	}

	dx11_twin=Direct3D11_Twin_Index_Buffer(sizeof(WORD)*index_count,(usage&USAGE_DYNAMIC)!=0);

	unsigned usage_flags=
		D3DUSAGE_WRITEONLY|
		((usage&USAGE_DYNAMIC) ? D3DUSAGE_DYNAMIC : 0)|
		((usage&USAGE_NPATCHES) ? D3DUSAGE_NPATCHES : 0)|
		((usage&USAGE_SOFTWAREPROCESSING) ? D3DUSAGE_SOFTWAREPROCESSING : 0);
	if (!DX8Wrapper::Get_Current_Caps()->Support_TnL()) {
		usage_flags|=D3DUSAGE_SOFTWAREPROCESSING;
	}

	RenderResult ret=DX8Wrapper::_Get_D3D_Device()->CreateIndexBuffer(
		sizeof(WORD)*index_count,
		usage_flags,
		D3DFMT_INDEX16,
		(usage&USAGE_DYNAMIC) ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED,
		&index_buffer,
		NULL);	// pSharedHandle, D3D9's extra parameter, reserved and always null

	if (Render_Succeeded(ret)) {
		return;
	}

	//	Not for a dynamic buffer, which is asked for mid-frame: see Create_Vertex_Buffer.
	if (!(usage&USAGE_DYNAMIC)) {
		WWDEBUG_SAY(("Index buffer creation failed, trying to release assets...\n"));

		// Vertex buffer creation failed, so try releasing least used textures and flushing the mesh cache.

		// Free all textures that haven't been used in the last 5 seconds
		TextureClass::Invalidate_Old_Unused_Textures(5000);

		// Invalidate the mesh cache
		WW3D::_Invalidate_Mesh_Cache();

		// Try again...
		ret=DX8Wrapper::_Get_D3D_Device()->CreateIndexBuffer(
			sizeof(WORD)*index_count,
			usage_flags,
			D3DFMT_INDEX16,
			D3DPOOL_MANAGED,
			&index_buffer,
			NULL);	// pSharedHandle, D3D9's extra parameter, reserved and always null

		if (Render_Succeeded(ret)) {
			WWDEBUG_SAY(("...Index buffer creation succesful\n"));
			return;
		}
	}

	//	Not fatal and never was; a refused buffer writes to scratch like Create_Vertex_Buffer's,
	//	and loses its Direct3D 11 twin for the same reason.
	DX8_ErrorCode(ret);
	index_buffer=NULL;
	scratch_indices=W3DNEWARRAY unsigned short[index_count];
	delete dx11_twin;
	dx11_twin=NULL;
}

// ----------------------------------------------------------------------------

DX8IndexBufferClass::~DX8IndexBufferClass()
{
	if (index_buffer) index_buffer->Release();
	if (scratch_indices) delete [] scratch_indices;
	if (dx11_twin) delete dx11_twin;
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

SortingIndexBufferClass::SortingIndexBufferClass(unsigned short index_count_)
	:
	IndexBufferClass(BUFFER_TYPE_SORTING,index_count_)
{
	WWMEMLOG(MEM_RENDERER);
	WWASSERT(index_count);

	index_buffer=W3DNEWARRAY unsigned short[index_count];
}

// ----------------------------------------------------------------------------

SortingIndexBufferClass::~SortingIndexBufferClass()
{
	delete[] index_buffer;
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

DynamicIBAccessClass::DynamicIBAccessClass(unsigned short type_, unsigned short index_count_)
	:
	IndexCount(index_count_),
	IndexBuffer(0),
	Type(type_)
{
	WWASSERT(Type==BUFFER_TYPE_DYNAMIC_DX8 || Type==BUFFER_TYPE_DYNAMIC_SORTING);
	if (Type==BUFFER_TYPE_DYNAMIC_DX8) {
		Allocate_DX8_Dynamic_Buffer();
	}
	else {
		Allocate_Sorting_Dynamic_Buffer();
	}
}

DynamicIBAccessClass::~DynamicIBAccessClass()
{
	REF_PTR_RELEASE(IndexBuffer);
	if (Type==BUFFER_TYPE_DYNAMIC_DX8) {
		_DynamicDX8IndexBufferInUse=false;
		_DynamicDX8IndexBufferOffset+=IndexCount;
	}
	else {
		_DynamicSortingIndexArrayInUse=false;
		_DynamicSortingIndexArrayOffset+=IndexCount;
	}
}

void DynamicIBAccessClass::_Deinit()
{
	WWASSERT ((_DynamicDX8IndexBuffer == NULL) || (_DynamicDX8IndexBuffer->Num_Refs() == 1));
	REF_PTR_RELEASE(_DynamicDX8IndexBuffer);
	_DynamicDX8IndexBufferInUse=false;
	_DynamicDX8IndexBufferSize=DEFAULT_IB_SIZE;
	_DynamicDX8IndexBufferOffset=0;

	WWASSERT ((_DynamicSortingIndexArray == NULL) || (_DynamicSortingIndexArray->Num_Refs() == 1));
	REF_PTR_RELEASE(_DynamicSortingIndexArray);
	for (int i=0;i<MAX_RETIRED_SORTING_INDEX_ARRAYS;++i) {
		REF_PTR_RELEASE(_RetiredSortingIndexArrays[i]);
	}
	_DynamicSortingIndexArrayInUse=false;
	_DynamicSortingIndexArraySize=0;
	_DynamicSortingIndexArrayOffset=0;	
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

// The shared dynamic index buffer goes to its twin alone while Direct3D 11 presents, for the reason
// on the vertex buffer's copy of this in dx8vertexbuffer.cpp.
static bool d3d9_reads_dynamic_buffer(DX8IndexBufferClass* buffer)
{
	return !Direct3D11_Present_Is_Enabled() || buffer->Get_DX11_Twin()==NULL;
}

DynamicIBAccessClass::WriteLockClass::WriteLockClass(DynamicIBAccessClass* ib_access_)
	:
	DynamicIBAccess(ib_access_)
{
	DX8_THREAD_ASSERT();
	DynamicIBAccess->IndexBuffer->Add_Ref();
	switch (DynamicIBAccess->Get_Type()) {
	case BUFFER_TYPE_DYNAMIC_DX8:
		WWASSERT(DynamicIBAccess);
//		WWASSERT(!dynamic_dx8_index_buffer->Engine_Refs());
		if (static_cast<DX8IndexBufferClass*>(DynamicIBAccess->IndexBuffer)->Get_DX8_Index_Buffer()==NULL) {
			Indices=static_cast<DX8IndexBufferClass*>(DynamicIBAccess->IndexBuffer)->Get_Scratch_Indices();
			Indices+=DynamicIBAccess->IndexBufferOffset;
			break;
		}
		DX8_Assert();
		{
			DX8IndexBufferClass* buffer=static_cast<DX8IndexBufferClass*>(DynamicIBAccess->IndexBuffer);
			const bool d3d9_reads=d3d9_reads_dynamic_buffer(buffer);
			const unsigned lock_flags=!DynamicIBAccess->IndexBufferOffset ? D3DLOCK_DISCARD : D3DLOCK_NOOVERWRITE;
			const unsigned byte_offset=DynamicIBAccess->IndexBufferOffset*sizeof(WORD);
			const unsigned byte_count=DynamicIBAccess->Get_Index_Count()*sizeof(WORD);
			Indices=NULL;
			if (d3d9_reads) {
				DX8_ErrorCode(buffer->Get_DX8_Index_Buffer()->Lock(byte_offset,byte_count,(void**)&Indices,lock_flags));
			}
			void* mirror=DX11Lock.Begin(buffer->Get_DX11_Twin(),Indices,byte_offset,byte_count,lock_flags,d3d9_reads);
			if (mirror) Indices=(unsigned short*)mirror;
		}
		break;
	case BUFFER_TYPE_DYNAMIC_SORTING:
		Indices=static_cast<SortingIndexBufferClass*>(DynamicIBAccess->IndexBuffer)->index_buffer;
		Indices+=DynamicIBAccess->IndexBufferOffset;
		break;
	default:
		WWASSERT(0);
		break;
	}
}

DynamicIBAccessClass::WriteLockClass::~WriteLockClass()
{
	DX8_THREAD_ASSERT();
	switch (DynamicIBAccess->Get_Type()) {
	case BUFFER_TYPE_DYNAMIC_DX8:
		if (static_cast<DX8IndexBufferClass*>(DynamicIBAccess->IndexBuffer)->Get_DX8_Index_Buffer()==NULL) break;
		DX8_Assert();
		DX11Lock.End();
		if (d3d9_reads_dynamic_buffer(static_cast<DX8IndexBufferClass*>(DynamicIBAccess->IndexBuffer))) {
			DX8_ErrorCode(static_cast<DX8IndexBufferClass*>(DynamicIBAccess->IndexBuffer)->Get_DX8_Index_Buffer()->Unlock());
		}
		break;
	case BUFFER_TYPE_DYNAMIC_SORTING:
		break;
	default:
		WWASSERT(0);
		break;
	}
	DynamicIBAccess->IndexBuffer->Release_Ref();
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

void DynamicIBAccessClass::Allocate_DX8_Dynamic_Buffer()
{
	WWMEMLOG(MEM_RENDERER);
	WWASSERT(!_DynamicDX8IndexBufferInUse);
	_DynamicDX8IndexBufferInUse=true;

	// If requesting more indices than dynamic index buffer can fit, delete the ib
	// and adjust the size to the new count.
	if (IndexCount>_DynamicDX8IndexBufferSize) {
		REF_PTR_RELEASE(_DynamicDX8IndexBuffer);
		_DynamicDX8IndexBufferSize=IndexCount;
		if (_DynamicDX8IndexBufferSize<DEFAULT_IB_SIZE) _DynamicDX8IndexBufferSize=DEFAULT_IB_SIZE;
	}

	// One made while the device refused buffers is a scratch block; try for a real one again, but
	// only once the device says it is back, not on every draw while it still refuses
	if (_DynamicDX8IndexBuffer && _DynamicDX8IndexBuffer->Get_DX8_Index_Buffer()==NULL
			&& DX8Wrapper::_Get_D3D_Device()!=NULL
			&& DX8Wrapper::_Get_D3D_Device()->TestCooperativeLevel()==D3D_OK) {
		REF_PTR_RELEASE(_DynamicDX8IndexBuffer);
	}

	// Create a new vb if one doesn't exist currently
	if (!_DynamicDX8IndexBuffer) {
		unsigned usage=DX8IndexBufferClass::USAGE_DYNAMIC;
		if (DX8Wrapper::Get_Current_Caps()->Support_NPatches()) {
			usage|=DX8IndexBufferClass::USAGE_NPATCHES;
		}

		_DynamicDX8IndexBuffer=NEW_REF(DX8IndexBufferClass,(
			_DynamicDX8IndexBufferSize,
			(DX8IndexBufferClass::UsageType)usage));
		_DynamicDX8IndexBufferOffset=0;
	}

	// Any room at the end of the buffer?
	if (((unsigned)IndexCount+_DynamicDX8IndexBufferOffset)>_DynamicDX8IndexBufferSize) {
		_DynamicDX8IndexBufferOffset=0;
	}

	REF_PTR_SET(IndexBuffer,_DynamicDX8IndexBuffer);
	IndexBufferOffset=_DynamicDX8IndexBufferOffset;
}

void DynamicIBAccessClass::Allocate_Sorting_Dynamic_Buffer()
{
	WWMEMLOG(MEM_RENDERER);
	WWASSERT(!_DynamicSortingIndexArrayInUse);
	_DynamicSortingIndexArrayInUse=true;

	unsigned new_index_count=_DynamicSortingIndexArrayOffset+IndexCount;
	WWASSERT(new_index_count<65536);
	if (_DynamicSortingIndexArray && new_index_count>_DynamicSortingIndexArraySize) {
		int slot=0;
		while (slot<MAX_RETIRED_SORTING_INDEX_ARRAYS && _RetiredSortingIndexArrays[slot]) {
			++slot;
		}
		if (slot<MAX_RETIRED_SORTING_INDEX_ARRAYS) {
			_RetiredSortingIndexArrays[slot]=_DynamicSortingIndexArray;
			_DynamicSortingIndexArray=NULL;
		}
		else {
			REF_PTR_RELEASE(_DynamicSortingIndexArray);
		}
	}

	if (!_DynamicSortingIndexArray) {
		for (int i=0;i<MAX_RETIRED_SORTING_INDEX_ARRAYS;++i) {
			if (_RetiredSortingIndexArrays[i] && _RetiredSortingIndexArrays[i]->Num_Refs()==1) {
				_DynamicSortingIndexArray=_RetiredSortingIndexArrays[i];
				_RetiredSortingIndexArrays[i]=NULL;
				break;
			}
		}
		if (!_DynamicSortingIndexArray) {
			_DynamicSortingIndexArray=NEW_REF(SortingIndexBufferClass,(SORTING_INDEX_ARRAY_SIZE));
		}
		_DynamicSortingIndexArraySize=SORTING_INDEX_ARRAY_SIZE;
		_DynamicSortingIndexArrayOffset=0;
	}

	REF_PTR_SET(IndexBuffer,_DynamicSortingIndexArray);
	IndexBufferOffset=_DynamicSortingIndexArrayOffset;
}

void DynamicIBAccessClass::_Reset(bool frame_changed)
{
	_DynamicSortingIndexArrayOffset=0;
	if (frame_changed) _DynamicDX8IndexBufferOffset=0;
}

unsigned short DynamicIBAccessClass::Get_Default_Index_Count(void)
{
	return _DynamicDX8IndexBufferSize;
}

/*Added so that VisualC++ doesn't remove our try/catch blocks around index buffer access.
This is needed because of a Windows 2000 Kernal bug as explained in the DX 9.0b readme file.*/
int IndexBufferExceptionFunc(void)
{
	int b=1;

	b += _IndexBufferTotalIndices;
	return b;
}
