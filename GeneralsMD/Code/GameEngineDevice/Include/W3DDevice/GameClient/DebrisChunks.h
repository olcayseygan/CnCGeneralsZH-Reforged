// The dark chunks a dying vehicle throws (CreateDebris in its death OCL), thinned out in the Reforged
// look on the drawing side alone; shared by W3DDebrisDraw and its test.
#pragma once
#include <string.h>
#include <ctype.h>

/** A tank's loose black chunks: EA's generic GXMammoth_D01 to _D12 that every tank death throws, and
		AVScrap2 to 5, which the fork's death lists add.  A unit's own pieces (an Overlord's turret,
		NVOvrlrd_D2), building rubble (ABPwrPlant...) and civilian wreckage are not chunks. */
inline bool DebrisIsTankChunk( const char *model )
{
	static const char *const CHUNKS[] = { "GXMammoth_D", "AVScrap" };
	if (model == 0)
		return false;
	for (size_t c = 0; c < sizeof( CHUNKS ) / sizeof( CHUNKS[ 0 ] ); ++c)
	{
		const char *p = CHUNKS[ c ];
		const char *m = model;
		while (*p && *m && tolower( (unsigned char)*p ) == tolower( (unsigned char)*m ))
		{
			++p;
			++m;
		}
		if (*p == 0)
			return true;
	}
	return false;
}

/** How many of one death's chunks are drawn, and how the death is told apart: the chunks it throws
		are all drawn first on the same client frame, near where it died. */
enum
{
	DEBRIS_CHUNKS_SHOWN = 2,
	DEBRIS_CHUNK_GROUPS = 32		///< deaths remembered at once; one frame never holds more in practice
};
static const float DEBRIS_CHUNK_GROUP_RADIUS = 60.0f;

/** The chunks drawn so far, by death.  admit says whether a chunk first drawn on client frame
		`frame` at x, y is one of its death's DEBRIS_CHUNKS_SHOWN; the rest are not drawn.  Chunks come in
		creation order, so every machine and every replay picks the same ones; nothing here is read by
		the logic. */
struct DebrisChunkBudget
{
	struct Group
	{
		unsigned frame;
		float x, y;
		int shown;
	};
	Group groups[ DEBRIS_CHUNK_GROUPS ];
	int count;
	unsigned next;

	DebrisChunkBudget() : count( 0 ), next( 0 ) {}

	bool admit( unsigned frame, float x, float y )
	{
		for (int i = 0; i < count; ++i)
		{
			Group &g = groups[ i ];
			const float dx = g.x - x, dy = g.y - y;
			if (g.frame == frame && dx * dx + dy * dy <= DEBRIS_CHUNK_GROUP_RADIUS * DEBRIS_CHUNK_GROUP_RADIUS)
			{
				if (g.shown >= DEBRIS_CHUNKS_SHOWN)
					return false;
				++g.shown;
				return true;
			}
		}
		Group &g = groups[ next % DEBRIS_CHUNK_GROUPS ];
		g.frame = frame;
		g.x = x;
		g.y = y;
		g.shown = 1;
		++next;
		if (count < DEBRIS_CHUNK_GROUPS)
			++count;
		return true;
	}
};

/** A drawn chunk's opacity `ageSeconds` after it was first drawn: whole for a second and a half, then
		gone half a second later, so it does not lie about the field for the debris' whole life. */
inline float DebrisChunkOpacity( float ageSeconds )
{
	const float HOLD = 1.5f, FADE = 0.5f;
	if (ageSeconds <= HOLD)
		return 1.0f;
	if (ageSeconds >= HOLD + FADE)
		return 0.0f;
	return 1.0f - ( ageSeconds - HOLD ) / FADE;
}
