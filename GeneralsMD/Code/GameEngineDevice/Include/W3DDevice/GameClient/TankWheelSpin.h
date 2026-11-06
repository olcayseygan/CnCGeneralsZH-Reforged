// How far a tank's road wheel turns, shared by W3DTankDraw and its test.
#pragma once
#include <math.h>

/** The wheel's angle about its axle after the hull drove forward along its heading and turned by
		turn radians (counter-clockwise from above).  The wheel rolls over the ground its own side's
		tread covered, the inside of a turn less and the outside more, the split the treads use, so a
		sprocket and a road wheel of different radii keep pace with each other.  Comes back in
		[0, 2 pi) so a long drive never eats the float's precision. */
inline float TankWheelAngle( float angle, float forward, float turn, float halfTrack, bool leftSide,
	float radius )
{
	const float fullTurn = 6.2831853f;
	const float distance = leftSide ? forward - turn * halfTrack : forward + turn * halfTrack;
	angle += distance / radius;
	return angle - fullTurn * floorf( angle / fullTurn );
}
