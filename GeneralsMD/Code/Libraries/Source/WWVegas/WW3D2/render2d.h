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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

 /***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : WW3D                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/render2d.h                             $*
 *                                                                                             *
 *                       Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                     $Modtime:: 12/17/01 11:47a                                             $*
 *                                                                                             *
 *                    $Revision:: 26                                                          $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#if defined(_MSC_VER)
#pragma once
#endif

#ifndef RENDER2D_H
#define RENDER2D_H

#include "Lib/WideChar.h"	// WideChar, the engine's text type (B1); its own header, so WW3D2 needs no BaseType.h
#include "always.h"
//#include "simplevec.h"
#include "Vector.H"
#include "vector2.h"

#include "shader.h"
#include "widestring.h"
#include "rect.h"
#include "bittype.h"

class	Font3DInstanceClass;
class TextureClass;
class	Vector3;
class	Vector4;

/*
** Macros
*/

//
//	Vector RGB to INT32 methods (normalized components)
//
#define VRGB_TO_INT32(rgb)		(unsigned(rgb[0]*255.0f)<<16)|(unsigned(rgb[1]*255.0f)<<8)|(unsigned(rgb[2]*255.0f))|0xFF000000
#define VRGBA_TO_INT32(rgb)	(unsigned(rgb[3]*255.0f)<<24)|(unsigned(rgb[0]*255.0f)<<16)|(unsigned(rgb[1]*255.0f)<<8)|(unsigned(rgb[2]*255.0f))

//
//	RGB to INT32 methods (each component is a value from 0 - 255)
//
#define RGB_TO_INT32(r,g,b)		(unsigned(r)<<16)|(unsigned(g)<<8)|(unsigned(b))|0xFF000000
#define RGBA_TO_INT32(r,g,b,a)	(unsigned(a)<<24)|(unsigned(r)<<16)|(unsigned(g)<<8)|(unsigned(b))

//
// Float RGB to INT32 methods (each component is a float between 0.0 and 1.0)
//
#define FRGB_TO_INT32(r,g,b)		(unsigned(r*255.0f)<<16)|(unsigned(g*255.0f)<<8)|(unsigned(b*255.0f))|0xFF000000
#define FRGBA_TO_INT32(r,g,b,a)	(unsigned(a*255.0f)<<24)|(unsigned(r*255.0f)<<16)|(unsigned(g*255.0f)<<8)|(unsigned(b*255.0f))

//
//	INT32 to Vector RGB methods
//
#define INT32_TO_VRGB(color, vrgb)							\
	vrgb[0] = ((color & 0x00FF0000) >> 16) / 256.0F;	\
	vrgb[1] = ((color & 0x0000FF00) >> 8) / 256.0F;		\
	vrgb[2] = ((color & 0x000000FF)) / 256.0F;

#define INT32_TO_VRGBA(color, vrgba)						\
	vrgba[0] = ((color & 0x00FF0000) >> 16) / 256.0F;	\
	vrgba[1] = ((color & 0x0000FF00) >> 8) / 256.0F;	\
	vrgba[2] = ((color & 0x000000FF)) / 256.0F;			\
	vrgba[3] = ((color & 0xFF000000) >> 24) / 256.0F;


/*
** Render2DClass
*/
class Render2DClass : public W3DMPO
{
	W3DMPO_GLUE(Render2DClass)
public:
	Render2DClass( TextureClass* tex = NULL );
	virtual ~Render2DClass(void);

	virtual	void	Reset(void);
	void	Render(void);

	void	Set_Coordinate_Range( const RectClass & range );

	void	Set_Texture(TextureClass* tex);
	TextureClass * Peek_Texture( void )			{ return Texture; }
	void	Set_Texture( const char * filename );
	void	Enable_Additive(bool b);
	void	Enable_Alpha(bool b);
	void	Enable_Grayscale(bool b);///<added for generals to draw disabled button states - MW
	void  Enable_Texturing(bool b);
	
	ShaderClass *			Get_Shader( void ) { return &Shader; }
	static ShaderClass	Get_Default_Shader( void );

	// Add Quad
	void	Add_Quad( const Vector2 & v0, const Vector2 & v1, const Vector2 & v2, const Vector2 & v3, const RectClass & uv, unsigned long color = 0xFFFFFFFF  );
	void	Add_Quad_Backfaced( const Vector2 & v0, const Vector2 & v1, const Vector2 & v2, const Vector2 & v3, const RectClass & uv, unsigned long color = 0xFFFFFFFF  );
	void	Add_Quad( const RectClass & screen, const RectClass & uv, unsigned long color = 0xFFFFFFFF  );
	void	Add_Quad( const Vector2 & v0, const Vector2 & v1, const Vector2 & v2, const Vector2 & v3, unsigned long color = 0xFFFFFFFF  );
	void	Add_Quad( const RectClass & screen, unsigned long color = 0xFFFFFFFF );

	void	Add_Quad_VGradient( const Vector2 & v0, const Vector2 & v1, const Vector2 & v2, const Vector2 & v3, const RectClass & uv, unsigned long top_color, unsigned long bottom_color );
	void	Add_Quad_VGradient( const RectClass & screen, unsigned long top_color, unsigned long bottom_color );
	void	Add_Quad_HGradient( const Vector2 & v0, const Vector2 & v1, const Vector2 & v2, const Vector2 & v3, const RectClass & uv, unsigned long left_color, unsigned long right_color );
	void	Add_Quad_HGradient( const RectClass & screen, unsigned long left_color, unsigned long right_color );

	// Add Tri
	void	Add_Tri( const Vector2 & v0, const Vector2 & v1, const Vector2 & v2, const Vector2 & uv0, const Vector2 & uv1, const Vector2 & uv2, unsigned long color = 0xFFFFFFFF  );

	// Primitive support
	void	Add_Line( const Vector2 & a, const Vector2 & b, float width, unsigned long color = 0xFFFFFFFF );
	void	Add_Line( const Vector2 & a, const Vector2 & b, float width, const RectClass & uv, unsigned long color = 0xFFFFFFFF );
	void	Add_Line( const Vector2 & a, const Vector2 & b, float width, unsigned long color , unsigned long color2);
	void	Add_Line( const Vector2 & a, const Vector2 & b, float width, const RectClass & uv, unsigned long color, unsigned long color2);
	void	Add_Outline( const RectClass & rect, float width = 1.0F, unsigned long color = 0xFFFFFFFF );
	void	Add_Outline( const RectClass & rect, float width, const RectClass & uv, unsigned long color = 0xFFFFFFFF );
	void	Add_Rect( const RectClass & rect, float border_width = 1.0F, uint32 border_color = 0xFF000000, uint32 fill_color = 0xFFFFFFFF);

	void Set_Hidden( bool hide )			{ IsHidden = hide; }

	// Z-value support (this is usefull for playing tricks with the z-buffer)
	void	Set_Z_Value (float z_value)	{ ZValue = z_value; }

	// Move all verts 
	void	Move( const Vector2 & a );

	// Force all alphas 
	void	Force_Alpha( float alpha );
	void	Force_Color( int color );

	// Color access
	DynamicVectorClass<unsigned long> &	Get_Color_Array (void)	{ return Colors; }

	// What the batch holds, all of its draws together
	int	Get_Vertex_Count( void ) const		{ return Vertices.Count(); }
	int	Get_Index_Count( void ) const		{ return Indices.Count(); }

	// The dynamic vertex and index buffers count in an unsigned short and an index is one, so a
	// batch is drawn in pieces of at most this much; every piece is whole quads and triangles
	enum { MAX_DRAW_VERTICES = 65532, MAX_DRAW_INDICES = 65532 };

	// The first batch this process had to split: what it would have held, vertices and indices,
	// had it stayed one draw.  False while no batch has gone over.
	static bool	Get_First_Split( int & vertices, int & indices );

	// statics to access the Screen Resolution in Pixels
	static void	Set_Screen_Resolution( const RectClass & screen );
	static const RectClass & Get_Screen_Resolution( void )			{ return ScreenResolution; }

protected:
	Vector2										CoordinateScale;
	Vector2										CoordinateOffset;
	Vector2										BiasedCoordinateOffset;
	TextureClass *								Texture;
	ShaderClass									Shader;
	DynamicVectorClass<unsigned short>	Indices;
	unsigned short								PreAllocatedIndices[60];
	DynamicVectorClass<Vector2>				Vertices;
	Vector2										PreAllocatedVertices[60];
	DynamicVectorClass<Vector2>				UVCoordinates;
	Vector2										PreAllocatedUVCoordinates[60];
	DynamicVectorClass<unsigned long>		Colors;
	unsigned long								PreAllocatedColors[60];
	bool											IsHidden;
	bool											IsGrayScale;
	float											ZValue;

	// A batch past one draw's room is split into draws; each later one starts here, its vertex
	// first and its index second, and its indices count from that vertex.  Empty under the cap.
	DynamicVectorClass<int>					DrawStarts;
	int											DrawVertexStart;
	int											DrawIndexStart;
	static int									FirstSplitVertices;
	static int									FirstSplitIndices;

	static RectClass							ScreenResolution;

	Vector2 Convert_Vert( const Vector2 & v );
	void	  Convert_Vert( Vector2 & vert_out, const Vector2 & vert_in );
	void	  Convert_Vert( Vector2 & vert_out, float x_in, float y_in );
	void	  Update_Bias( void );

	void	Internal_Add_Quad_Vertices( const Vector2 & v0, const Vector2 & v1, const Vector2 & v2, const Vector2 & v3 );
	void	Internal_Add_Quad_Vertices( const RectClass & screen );
	void	Internal_Add_Quad_UVs( const RectClass & uv );
	void	Internal_Add_Quad_Colors( unsigned long color );
	void	Internal_Add_Quad_VColors( unsigned long color1, unsigned long color2 );
	void	Internal_Add_Quad_HColors( unsigned long color1, unsigned long color2 );
	void	Internal_Add_Quad_Indicies( int start_vert_index, bool backfaced = false );
	int	Draw_Vertex_Base( int new_vertices, int new_indices );
	void	Render_Draw( int first_vertex, int vertex_count, int first_index, int index_count );
};


/*
** Render2DTextClass
*/
class Render2DTextClass : public Render2DClass {
public:
	Render2DTextClass(Font3DInstanceClass *font=NULL);
	~Render2DTextClass();

	virtual	void	Reset(void);

	Font3DInstanceClass *	Peek_Font( void )				{ return Font; }
	void	Set_Font( Font3DInstanceClass *font );

	void	Set_Location( const Vector2 & loc )				{ Location = loc; Cursor = loc; }
	void	Set_Wrapping_Width (float width)					{ WrapWidth = width; }
	
	// Clipping support
	void	Set_Clipping_Rect( const RectClass &rect )	{ ClipRect = rect; IsClippedEnabled = true; }
	bool	Is_Clipping_Enabled( void ) const				{ return IsClippedEnabled; }
	void	Enable_Clipping( bool onoff )						{ IsClippedEnabled = onoff; }

	void	Draw_Text( const char * text, unsigned long color = 0xFFFFFFFF );
	void	Draw_Text( const WideChar * text, unsigned long color = 0xFFFFFFFF );

	void	Draw_Block( const RectClass & screen, unsigned long color = 0xFFFFFFFF );

	const RectClass & Get_Draw_Extents( void )			{ return DrawExtents; }
	const RectClass & Get_Total_Extents( void )			{ return TotalExtents; }
	const Vector2 & Get_Cursor( void )						{ return Cursor; }

	Vector2	Get_Text_Extents( const WideChar * text );

private:
	Font3DInstanceClass* Font;
	Vector2					Location;
	Vector2					Cursor;
	float						WrapWidth;
	RectClass				DrawExtents;
	RectClass				TotalExtents;
	RectClass				BlockUV;
	RectClass				ClipRect;
	bool						IsClippedEnabled;

	void	Draw_Char( WideChar ch, unsigned long color );
};

#endif	// RENDER2D_H
