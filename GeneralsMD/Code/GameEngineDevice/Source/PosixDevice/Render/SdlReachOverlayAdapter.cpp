// The reach dispatcher supplies the existing fields; the renderer supplies the
// authoritative main-view/depth owner. Neither is inferred from the camera size.
// Distributed under GPL-3.0; see LICENSE.md.
#include "SdlReachOverlay.h"
#include "SdlSceneContext.h"
#include "PosixDevice9.h"
#include "Common/MapObject.h" // existing field declaration uses Color
#include "GameClient/ReachOverlay.h"
#include "W3DDevice/GameClient/W3DReachOverlay.h"
#include "WW3D2/ww3d.h"
#include <cstring>
#include <vector>

Bool SdlReachOverlay_Draw(CameraClass &, const std::vector<const ReachOverlayField *> &fields)
{
    SdlMainViewSnapshot snapshot;
    if (!PosixRenderer_Main_View(snapshot) || !snapshot.Frame || !snapshot.Frame->Supports_Scene_Depth()
        || !SdlReachOverlay::Supported(*snapshot.Frame)) return FALSE;
    // Empty selections retire arrays without acquiring a new depth capture.
    if (!fields.empty() && !PosixRenderer_Capture_Main_View(SdlSceneCapturePhase::TerrainOverlay, snapshot))
        return FALSE;

    SdlReachOverlay::Inputs input;
    input.Eligible = true;
    input.Target = snapshot.Target;
    input.DeviceGeneration = snapshot.DeviceGeneration;
    input.FrameSerial = snapshot.FrameSerial;
    input.ViewKey = snapshot.ViewKey;
    std::memcpy(input.ViewProjection, snapshot.ViewProjection, sizeof(input.ViewProjection));
    // The shader subtracts its explicit viewport. The post consumer's folded
    // FullTextureWorldFromClip would apply that conversion twice here.
    std::memcpy(input.WorldFromClip, snapshot.WorldFromClip, sizeof(input.WorldFromClip));
    std::memcpy(input.Viewport, snapshot.Viewport, sizeof(input.Viewport));
    std::memcpy(input.Scissor, snapshot.Scissor, sizeof(input.Scissor));
    input.TerrainDepth = snapshot.SampledDepth;
    input.DepthWidth = snapshot.DepthWidth;
    input.DepthHeight = snapshot.DepthHeight;
    input.DepthFormat = snapshot.DepthFormat;
    input.DepthCapture = snapshot.Capture;
    input.VisualSeconds = float(WW3D::Get_Sync_Time() % 600000u) * 0.001f;

    // Metadata is copied into retained scratch capacity. Grid spans are only
    // borrowed during Draw; cache misses copy their bytes before it returns.
    static std::vector<SdlReachOverlay::Field> copied;
    copied.clear();
    struct ClearCopied {
        std::vector<SdlReachOverlay::Field> &Records;
        ~ClearCopied() { Records.clear(); }
    } clearCopied{copied};
    try {
        copied.reserve(fields.size());
        for (const ReachOverlayField *field : fields) {
            if (!field)
                return FALSE;
            SdlReachOverlay::Field record;
            record.Identity = reinterpret_cast<uintptr_t>(field);
            record.Version = field->version;
            record.Colour = field->color;
            record.Width = field->width;
            record.Height = field->height;
            record.OriginX = field->originX;
            record.OriginY = field->originY;
            record.Step = field->step;
            record.DistanceLimit = field->distanceLimit;
            record.Opacity = field->opacity;
            record.Distance = field->distance.data();
            record.DistanceCount = field->distance.size();
            record.Hardness = field->hardness.empty() ? nullptr : field->hardness.data();
            record.HardnessCount = field->hardness.size();
            copied.push_back(record);
        }
        const bool drawn = SdlReachOverlay::Draw(*snapshot.Frame, input, copied.data(), copied.size(),
            SdlReachOverlay::DefaultLook());
        return drawn ? TRUE : FALSE;
    } catch (const std::exception &) {
        return FALSE; // no module packet has been recorded on this path
    }
}

Bool SdlReachOverlay_Release()
{
    // False retains the complete native owner. Only GPU-owner teardown may
    // discard queued readers and retry before destroying the frame/device.
    return SdlReachOverlay::TryRelease() ? TRUE : FALSE;
}
