// Which sub-objects a ShowSubObject or HideSubObject line carries along, shared by W3DModelDraw and its test.
#pragma once

namespace SubObjectBoneWalk
{

/** EA's walk (doHideShowBoneSubObjs): a part rides bone `boneIdx`, and a sub-object on `childBone` takes
		its new state when some bone above `childBone` is `boneIdx`.  parentOf(bone) is the hierarchy's parent
		index, and the root is bone 0.  A skin rides the root, so every part on any other bone is "below" it. */
template <class ParentOf>
inline bool isBelow( const ParentOf &parentOf, int boneIdx, int childBone )
{
	int bone = childBone;
	while( bone != 0 )
	{
		bone = parentOf( bone );
		if( bone == boneIdx )
			return true;
	}
	return false;
}

/** Whether a show from a part on `boneIdx` leaves a part the model file hides as it is.  Only a show
		from the root does: there the walk reaches every rigid part of the model, and the GLA Worker's
		ShowSubObject = BOX (a skin, so the root) put the Demolition General's suicide vest and the Worker
		Shoes, written hidden for their upgrades, on every worker that ever carried a crate.  A hide still
		reaches them, a show from any other bone still does (retail turrets bring their hidden muzzle flash
		meshes along), and an upgrade shows its own part by name, not through the walk. */
inline bool keepsFileHidden( int boneIdx, bool hide, bool hiddenInFile )
{
	return !hide && boneIdx == 0 && hiddenInFile;
}

}
