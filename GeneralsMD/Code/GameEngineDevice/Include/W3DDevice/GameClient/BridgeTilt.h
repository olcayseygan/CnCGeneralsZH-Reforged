// Which way a bridge model's up axis points on a sloped span, shared by W3DBridgeBuffer and its test.
#pragma once
#include "vector3.h"
#include <math.h>

/** The world direction a bridge model's Z axis takes on the span from its start to its end: square to
		the span and in the upright plane through it, so the deck tilts along its own length.  EA wrote
		(-slope, 0, cos), which is that only for a span along +X; on any other heading the model's height
		leaned sideways by the slope, and a bridge whose ends stand at very different heights spread its
		piers and side faces across the ground as long stripes.  A level span gives (0, 0, 1) exactly,
		as EA's did. */
inline Vector3 BridgeUpAxis( const Vector3 &span )
{
	const float length = span.Length();
	const float flat = sqrtf( span.X * span.X + span.Y * span.Y );
	if( !( length > 0.0f ) || !( flat > 0.0f ) )
		return Vector3( 0.0f, 0.0f, 1.0f );
	const float sinSlope = span.Z / length;
	// the cosine as EA took it, so a level span's (0, 0, 1) is exact
	const float cosSlope = (float)sqrt( 1.0 - sinSlope * sinSlope );
	return Vector3( -sinSlope * span.X / flat, -sinSlope * span.Y / flat, cosSlope );
}
