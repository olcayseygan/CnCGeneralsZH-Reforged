// Conservative receiver visibility shared by model and object-tree shadow casters.
#pragma once
#include "frustum.h"
#include "sphere.h"

/** Whether anything a pixel of this frame samples can be shadowed by a caster.  The caster is its
		bounding sphere, and what it can darken lies downwind of it: the sphere swept away from the sun
		from its own sun side down to the lowest ground on the map, widened by how far the receiving
		filter reaches across the map.  That capsule runs from upwind to downwind with this radius.
		Only the four sides of the view are tested: a draw with a depth-biased projection reads the
		map through near and far planes that are not the camera's, and the sides are the same for
		every one of them.  The overlay's quads sit at z = 0 whatever the ground does, so for them the
		capsule runs on to overlayDownwind, swept down to z = 0 when the lowest ground is above it. */
inline bool SunShadowReachesTheFrame( const FrustumClass &view, const Vector3 &upwind,
	const Vector3 &downwind, const Vector3 &overlayDownwind, float radius, float overlayReach )
{
	bool inView = true;
	for (int side = 1; side <= 4 && inView; ++side)
	{
		const PlaneClass &plane = view.Planes[ side ];
		inView = Vector3::Dot_Product( plane.N, upwind ) - plane.D <= radius
			|| Vector3::Dot_Product( plane.N, downwind ) - plane.D <= radius;
	}
	if (inView)
		return true;

	// the 2D scene's and the interface's quads, at the world's origin
	const Vector3 along = overlayDownwind - upwind;
	const float lengthSqr = along.Length2();
	float t = (lengthSqr > 0.0f) ? -Vector3::Dot_Product( upwind, along ) / lengthSqr : 0.0f;
	t = WWMath::Clamp( t, 0.0f, 1.0f );
	const Vector3 nearest = upwind + along * t;
	const float reach = radius + overlayReach;
	return nearest.Length2() <= reach * reach;
}

/** Where the sun's box sits on one axis of its own frame, the look point at zero and lo to hi the
		ground the frame shows, which holds the look point.  The box goes round that stretch.  When the
		stretch is wider than the box it stays as near the look point as it can with no texel past
		either end, so what falls off is the far ground and never the ground at the camera's feet. */
inline float SunBoxMiddle( float lo, float hi, float halfWidth )
{
	if (hi - lo <= 2.0f * halfWidth)
		return (lo + hi) * 0.5f;
	return WWMath::Clamp( 0.0f, lo + halfWidth, hi - halfWidth );
}

struct SunShadowCasterCull {
    const FrustumClass &View;
    Vector3 ToSun;
    float LowestReceiver, CasterPad, FilterReach, OverlayReach;
    bool Enabled;

    bool Visible(const SphereClass &bound) const {
        if (!Enabled) return true;
        const float casterRadius=bound.Radius+CasterPad;
        const float radius=casterRadius+FilterReach;
        const float sweep=WWMath::Max((bound.Center.Z-LowestReceiver+radius)/ToSun.Z,0.0f);
        const float overlaySweep=WWMath::Max((bound.Center.Z-WWMath::Min(LowestReceiver,0.0f)+radius)/ToSun.Z,0.0f);
        return SunShadowReachesTheFrame(View,bound.Center+ToSun*casterRadius,
            bound.Center-ToSun*sweep,bound.Center-ToSun*overlaySweep,radius,OverlayReach);
    }
};
