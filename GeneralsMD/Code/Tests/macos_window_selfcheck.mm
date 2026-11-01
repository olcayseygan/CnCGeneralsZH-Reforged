// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.

// Manual Cocoa check: briefly opens a window and enters/exits a fullscreen Space.
// No game data, screen recording or Accessibility permission is needed. Run from
// an interactive desktop; exit 77 means that Cocoa/a WindowServer is unavailable.
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <SDL3/SDL.h>
#include "SdlDevice/Common/SdlMacWindow.h"
#include "SdlDevice/Common/SdlWindowPolicy.h"
#include "../GameEngineDevice/Source/PosixDevice/Render/PosixDevice9.h"
#include "../GameEngineDevice/Source/PosixDevice/Render/SdlGpuFrame.h"

#include <stdio.h>
#include <cmath>
#include <cstring>

namespace {

int failures = 0;
const NSApplicationPresentationOptions HiddenEdges =
    NSApplicationPresentationHideDock | NSApplicationPresentationHideMenuBar;

bool check( bool condition, const char *message )
{
    fprintf( stderr, "%s: %s\n", condition ? "PASS" : "FAIL", message );
    if (!condition)
        ++failures;
    return condition;
}

void pump( SDL_Window *window )
{
    for (int iteration = 0; iteration < 20; ++iteration) {
        SDL_Event event;
        while (SDL_PollEvent( &event )) {}
        SdlWindowPolicy_updateScreenSaver( window != NULL ? SDL_GetWindowFlags( window ) : 0 );
        SdlMacWindow_updatePresentation( window );
        SDL_Delay( 10 );
    }
}

// Cleanup also runs after an early failure, including a failed fullscreen transition.
struct DesktopState
{
    SDL_Window *window = NULL;
    IDirect3DDevice9 *renderer = NULL;
    NSApplicationPresentationOptions presentation = NSApplicationPresentationDefault;
    NSRunningApplication *previousApplication = [[NSWorkspace sharedWorkspace] frontmostApplication];
    bool videoStarted = false;

    ~DesktopState()
    {
        if (renderer != NULL)
            renderer->Release();
        SdlMacWindow_updatePresentation( NULL );
        if (window != NULL) {
            SDL_SetWindowFullscreen( window, false );
            SDL_SyncWindow( window );
            SDL_DestroyWindow( window );
        }
        if (videoStarted) {
            SdlWindowPolicy_updateScreenSaver( 0 );
            SDL_Quit();
        }
        @try {
            if (NSApp != nil)
                NSApp.presentationOptions = presentation;
            if (previousApplication != nil)
                [previousApplication activateWithOptions:0];
        } @catch (NSException *exception) {
            (void)exception;
            check( false, "restore desktop presentation" );
        }
    }
};

CAMetalLayer *metalLayer( NSView *view )
{
    if ([view.layer isKindOfClass:[CAMetalLayer class]])
        return (CAMetalLayer *)view.layer;
    for (NSView *child in view.subviews) {
        CAMetalLayer *found = metalLayer( child );
        if (found != nil)
            return found;
    }
    return nil;
}

bool checkPresentation( DesktopState &desktop, NSWindow *native )
{
    PosixDirect3D9 *adapter = new PosixDirect3D9;
    D3DPRESENT_PARAMETERS parameters = {};
    parameters.BackBufferWidth = 64;
    parameters.BackBufferHeight = 48;
    parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
    parameters.Windowed = 1;
    parameters.hDeviceWindow = reinterpret_cast<RenderWindow>(desktop.window);
    parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    const RenderResult created = adapter->CreateDevice( 0, D3DDEVTYPE_HAL,
        parameters.hDeviceWindow, 0, &parameters, &desktop.renderer );
    adapter->Release();
    if (!check( created == D3D_OK && desktop.renderer != NULL, "create windowed GPU with VSync disabled" ))
        return false;
    PosixDevice9 *device = static_cast<PosixDevice9 *>(desktop.renderer);
    SdlGpuFrame *frame = device->Get_Gpu();
    if (!check( frame != NULL, "windowed D3D device owns an SDL GPU frame" ))
        return false;
    const bool immediate = SDL_WindowSupportsGPUPresentMode( frame->Device(), desktop.window,
        SDL_GPU_PRESENTMODE_IMMEDIATE );
    check( frame->Using_VSync() == !immediate, "initial presentation follows VSync or its supported fallback" );
    CAMetalLayer *layer = metalLayer( native.contentView );
    if (!check( layer != nil, "window exposes its actual Metal swapchain layer" ))
        return false;
    check( layer.displaySyncEnabled == !immediate, "Metal layer uses the initial presentation choice" );

    parameters.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    check( device->Reset( &parameters ) == D3D_OK, "reset enables VSync" );
    check( frame->Using_VSync() && layer.displaySyncEnabled, "VSync reset reaches Metal" );
    parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    check( device->Reset( &parameters ) == D3D_OK, "reset disables VSync when supported" );
    check( frame->Using_VSync() == !immediate && layer.displaySyncEnabled == !immediate,
        "immediate reset reaches Metal or retains the supported fallback" );
    D3DPRESENT_PARAMETERS invalid = parameters;
    invalid.BackBufferWidth = 0;
    invalid.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    check( device->Reset( &invalid ) == D3DERR_INVALIDCALL,
        "invalid reset is refused before changing presentation" );
    check( frame->Using_VSync() == !immediate && layer.displaySyncEnabled == !immediate,
        "refused reset preserves the previous Metal presentation mode" );
    parameters.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
    check( device->Reset( &parameters ) == D3D_OK, "reset selects default presentation" );
    check( frame->Using_VSync() && layer.displaySyncEnabled, "default presentation retains VSync" );
    return true;
}

void checkDisplayGeometry( DesktopState &desktop, NSWindow *native, bool fullDisplay )
{
    NSScreen *screen = native.screen;
    NSView *view = native.contentView;
    if (!check( screen != nil && view != nil, "fullscreen has a display and content view" ))
        return;
    const NSRect content = [native convertRectToScreen:[view convertRect:view.bounds toView:nil]];
    int width = 0, height = 0, pixelWidth = 0, pixelHeight = 0;
    check( SDL_GetWindowSize( desktop.window, &width, &height )
        && width == std::lround( view.bounds.size.width ) && height == std::lround( view.bounds.size.height ),
        "SDL window coordinates match the actual content view" );
    const NSRect backing = [view convertRectToBacking:view.bounds];
    check( SDL_GetWindowSizeInPixels( desktop.window, &pixelWidth, &pixelHeight )
        && pixelWidth == std::lround( backing.size.width ) && pixelHeight == std::lround( backing.size.height ),
        "SDL pixel size matches the actual Retina backing geometry" );
    fprintf( stderr, "INFO %s: screen %.0fx%.0f, content %dx%d, drawable %dx%d, camera top %.0f\n",
        fullDisplay ? "full-display" : "native Space", screen.frame.size.width, screen.frame.size.height,
        width, height, pixelWidth, pixelHeight, screen.safeAreaInsets.top );
    CAMetalLayer *layer = metalLayer( view );
    check( layer != nil && layer.drawableSize.width == pixelWidth && layer.drawableSize.height == pixelHeight,
        "Metal drawable covers the entire SDL pixel area" );
    if (fullDisplay) {
        check( NSEqualRects( content, screen.frame ), "full-display content reaches every physical display edge" );
        if (!NSIsEmptyRect( screen.auxiliaryTopLeftArea ) && !NSIsEmptyRect( screen.auxiliaryTopRightArea )) {
            check( NSContainsRect( content, screen.auxiliaryTopLeftArea )
                && NSContainsRect( content, screen.auxiliaryTopRightArea ),
                "content includes both unobscured regions beside the camera housing" );
            SDL_Rect housing = {};
            check( SdlMacWindow_getDisplayCutout( desktop.window, &housing )
                && housing.y == 0 && housing.w > 0 && housing.h == std::ceil( screen.safeAreaInsets.top )
                && housing.x > 0 && housing.x + housing.w < width,
                "camera housing is reported separately from the usable side regions" );
        } else {
            SDL_Rect housing = { 1, 2, 3, 4 };
            check( !SdlMacWindow_getDisplayCutout( desktop.window, &housing )
                && housing.x == 0 && housing.y == 0 && housing.w == 0 && housing.h == 0,
                "display without a camera housing has no cutout" );
        }
    }

    // Query a real acquired swapchain texture as well as its CAMetalLayer metadata.
    PosixDevice9 *device = static_cast<PosixDevice9 *>(desktop.renderer);
    SDL_GPUDevice *gpu = device->Get_Gpu()->Device();
    SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer( gpu );
    if (!check( commands != NULL, "acquire a command buffer for fullscreen presentation" ))
        return;
    SDL_GPUTexture *swapchain = NULL;
    Uint32 drawableWidth = 0, drawableHeight = 0;
    if (!check( SDL_WaitAndAcquireGPUSwapchainTexture( commands, desktop.window, &swapchain,
        &drawableWidth, &drawableHeight ) && swapchain != NULL, "acquire an actual fullscreen drawable" )) {
        SDL_CancelGPUCommandBuffer( commands );
        return;
    }
    check( drawableWidth == Uint32(pixelWidth) && drawableHeight == Uint32(pixelHeight),
        "acquired Metal drawable matches the full backing pixel dimensions" );
    SDL_GPUColorTargetInfo target = {};
    target.texture = swapchain;
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    target.clear_color = { 0.02f, 0.04f, 0.06f, 1.0f };
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass( commands, &target, 1, NULL );
    check( pass != NULL, "clear the acquired fullscreen drawable" );
    if (pass != NULL) SDL_EndGPURenderPass( pass );
    check( SDL_SubmitGPUCommandBuffer( commands ), "submit the fullscreen drawable" );
    check( SDL_WaitForGPUIdle( gpu ), "complete fullscreen drawable presentation" );
}

int run( bool fullDisplay )
{
    DesktopState desktop;
    if ([[NSScreen screens] count] == 0 || desktop.previousApplication == nil) {
        fprintf( stderr, "SKIP: an interactive macOS desktop is required\n" );
        return 77;
    }

    SdlWindowPolicy_configure();
    SdlMacWindow_configureFullscreen();
    if (SDL_GetEnvironmentVariable( SDL_GetEnvironment(), SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES ) == NULL) {
        bool cameraHousing = false;
        for (NSScreen *screen in [NSScreen screens])
            cameraHousing = cameraHousing || (screen.safeAreaInsets.top > 0
                && (!NSIsEmptyRect( screen.auxiliaryTopLeftArea ) || !NSIsEmptyRect( screen.auxiliaryTopRightArea )));
        check( SDL_GetHintBoolean( SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, true ) == !cameraHousing,
            "camera-housing detection selects full-panel fullscreen only where needed" );
    }
    SDL_SetHintWithPriority( SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "1", SDL_HINT_OVERRIDE );
    SdlMacWindow_configureFullscreen();
    check( SDL_GetHintBoolean( SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, false ),
        "an explicit native-Space choice overrides full-panel detection" );
    // Each mode is selected before SDL initializes its Cocoa backend.
    SDL_SetHintWithPriority( SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, fullDisplay ? "0" : "1", SDL_HINT_OVERRIDE );
    SDL_SetHintWithPriority( SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY, "0", SDL_HINT_OVERRIDE );
    SDL_SetHintWithPriority( SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "0", SDL_HINT_OVERRIDE );
    SdlWindowPolicy_configure();
    if (!SDL_Init( SDL_INIT_VIDEO )) {
        fprintf( stderr, "SKIP: SDL video is unavailable: %s\n", SDL_GetError() );
        return 77;
    }
    desktop.videoStarted = true;
    desktop.presentation = NSApp != nil ? NSApp.presentationOptions : NSApplicationPresentationDefault;
    if (SDL_strcmp( SDL_GetCurrentVideoDriver(), "cocoa" ) != 0) {
        fprintf( stderr, "SKIP: this check requires SDL's Cocoa driver\n" );
        return 77;
    }

    SdlWindowPolicy_updateScreenSaver( 0 );
    check( SDL_ScreenSaverEnabled(), "no window releases idle display sleep" );
    desktop.window = SDL_CreateWindow( "Reforged macOS compatibility check", 480, 320,
        SDL_WINDOW_HIGH_PIXEL_DENSITY );
    if (!check( desktop.window != NULL, "create native window" ))
        return 1;
    SDL_Window *window = desktop.window;
    NSWindow *native = (__bridge NSWindow *)SDL_GetPointerProperty( SDL_GetWindowProperties( window ),
        SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL );
    if (!check( native != nil, "SDL exposes its native Cocoa window" ))
        return 1;
    [native makeKeyAndOrderFront:nil];
    if (@available(macOS 14.0, *))
        [NSApp activate];
    else
        [NSApp activateIgnoringOtherApps:YES];
    pump( window );
    if (!checkPresentation( desktop, native ))
        return 1;
    // Activation is cooperative on current macOS; another app's user activity
    // can defer it until the fullscreen transition brings this window forward.
    int pixelsWidth = 0, pixelsHeight = 0;
    SDL_GetWindowSizeInPixels( window, &pixelsWidth, &pixelsHeight );
    check( pixelsWidth >= 480 && pixelsHeight >= 320, "Retina drawable has valid pixel size" );
    const NSRect windowedFrame = native.frame;
    SDL_Rect housing = { 1, 2, 3, 4 };
    check( !SdlMacWindow_getDisplayCutout( window, &housing )
        && housing.x == 0 && housing.y == 0 && housing.w == 0 && housing.h == 0,
        "windowed content is clear of the camera housing" );

    if (!check( SDL_SetWindowFullscreen( window, true ), "request fullscreen" )
        || !check( SDL_SyncWindow( window ), "complete fullscreen transition" ))
        return 1;
    pump( window );
    check( ((native.styleMask & NSWindowStyleMaskFullScreen) != 0) == !fullDisplay,
        fullDisplay ? "full-display mode uses SDL's supported borderless fullscreen"
                    : "native fullscreen retains its macOS Space" );
    check( [NSApp isActive] && [native isKeyWindow], "native foreground activation" );
    check( !SDL_ScreenSaverEnabled(), "foreground inhibits idle display sleep" );
    check( (NSApp.presentationOptions & HiddenEdges) == HiddenEdges, "fullscreen suppresses Dock and menu hot edges" );
    check( (NSApp.presentationOptions & (NSApplicationPresentationAutoHideDock
        | NSApplicationPresentationAutoHideMenuBar | NSApplicationPresentationAutoHideToolbar)) == 0,
        "fullscreen presentation options are compatible" );
    checkDisplayGeometry( desktop, native, fullDisplay );

    // Explicitly visible/automatic menus must leave presentation policy to SDL/AppKit.
    const char *menuChoices[] = { "1", "auto" };
    for (const char *choice : menuChoices) {
        SdlMacWindow_updatePresentation( NULL );
        SDL_SetHintWithPriority( SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY, choice, SDL_HINT_OVERRIDE );
        NSApp.presentationOptions = NSApplicationPresentationDefault;
        const NSApplicationPresentationOptions externalOptions = NSApp.presentationOptions;
        SdlMacWindow_updatePresentation( window );
        check( NSApp.presentationOptions == externalOptions,
            choice[0] == '1' ? "visible menu leaves Dock/menu policy unchanged"
                             : "automatic menu leaves Dock/menu policy unchanged" );
    }

    SDL_SetHintWithPriority( SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY, "0", SDL_HINT_OVERRIDE );
    SdlMacWindow_updatePresentation( window );
    check( (NSApp.presentationOptions & HiddenEdges) == HiddenEdges, "helper owns an intact fullscreen policy" );
    NSApp.presentationOptions = NSApplicationPresentationDefault;
    // AppKit can normalize Default while a fullscreen Space is still active.
    // Preserve its resulting policy rather than assuming the getter returns zero.
    const NSApplicationPresentationOptions externalOptions = NSApp.presentationOptions;
    SdlMacWindow_updatePresentation( NULL );
    check( NSApp.presentationOptions == externalOptions,
        "external AppKit reset is not reversed during release" );

    if (!check( SDL_SetWindowFullscreen( window, false ), "request windowed mode" )
        || !check( SDL_SyncWindow( window ), "complete windowed transition" ))
        return 1;
    pump( window );
    check( (native.styleMask & NSWindowStyleMaskFullScreen) == 0, "native fullscreen exits cleanly" );
    check( NSEqualRects( native.frame, windowedFrame ), "fullscreen restores the original windowed frame" );
    check( (NSApp.presentationOptions & HiddenEdges) == 0, "desktop Dock and menu policy restored" );
    SDL_HideWindow( window );
    pump( window );
    check( SDL_ScreenSaverEnabled(), "hidden window releases idle display sleep" );
    check( (NSApp.presentationOptions & HiddenEdges) == 0, "hidden window leaves desktop presentation unchanged" );
    return failures != 0 ? 1 : 0;
}

} // namespace

int main( int argc, char **argv )
{
    const bool fullDisplay = argc == 2 && std::strcmp( argv[1], "--full-display" ) == 0;
    if (argc > 1 && !fullDisplay) {
        fprintf( stderr, "Usage: macos_window_selfcheck [--full-display]\n" );
        return 2;
    }
    @autoreleasepool {
        const int result = run( fullDisplay );
        return result == 77 ? 77 : failures != 0 ? 1 : result;
    }
}
