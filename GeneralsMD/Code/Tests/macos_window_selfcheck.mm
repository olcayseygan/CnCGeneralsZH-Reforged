// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.

// Manual Cocoa check: briefly opens a window and enters/exits a fullscreen Space.
// No game data, screen recording or Accessibility permission is needed. Run from
// an interactive desktop; exit 77 means that Cocoa/a WindowServer is unavailable.
#import <AppKit/AppKit.h>

#include <SDL3/SDL.h>
#include "SdlDevice/Common/SdlMacWindow.h"
#include "SdlDevice/Common/SdlWindowPolicy.h"

#include <stdio.h>

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
    NSApplicationPresentationOptions presentation = NSApplicationPresentationDefault;
    NSRunningApplication *previousApplication = [[NSWorkspace sharedWorkspace] frontmostApplication];
    bool videoStarted = false;

    ~DesktopState()
    {
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

int run()
{
    DesktopState desktop;
    if ([[NSScreen screens] count] == 0 || desktop.previousApplication == nil) {
        fprintf( stderr, "SKIP: an interactive macOS desktop is required\n" );
        return 77;
    }

    // Exercise the native path even if the caller normally selects legacy fullscreen.
    SDL_SetHintWithPriority( SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "1", SDL_HINT_OVERRIDE );
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
    // Activation is cooperative on current macOS; another app's user activity
    // can defer it until the fullscreen transition brings this window forward.
    int pixelsWidth = 0, pixelsHeight = 0;
    SDL_GetWindowSizeInPixels( window, &pixelsWidth, &pixelsHeight );
    check( pixelsWidth >= 480 && pixelsHeight >= 320, "Retina drawable has valid pixel size" );

    if (!check( SDL_SetWindowFullscreen( window, true ), "request native fullscreen" )
        || !check( SDL_SyncWindow( window ), "complete native fullscreen transition" ))
        return 1;
    pump( window );
    check( (native.styleMask & NSWindowStyleMaskFullScreen) != 0, "fullscreen uses a native macOS Space" );
    check( [NSApp isActive] && [native isKeyWindow], "native foreground activation" );
    check( !SDL_ScreenSaverEnabled(), "foreground inhibits idle display sleep" );
    check( (NSApp.presentationOptions & HiddenEdges) == HiddenEdges, "fullscreen suppresses Dock and menu hot edges" );
    check( (NSApp.presentationOptions & (NSApplicationPresentationAutoHideDock
        | NSApplicationPresentationAutoHideMenuBar | NSApplicationPresentationAutoHideToolbar)) == 0,
        "fullscreen presentation options are compatible" );

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
    check( (NSApp.presentationOptions & HiddenEdges) == 0, "desktop Dock and menu policy restored" );
    SDL_HideWindow( window );
    pump( window );
    check( SDL_ScreenSaverEnabled(), "hidden window releases idle display sleep" );
    check( (NSApp.presentationOptions & HiddenEdges) == 0, "hidden window leaves desktop presentation unchanged" );
    return failures != 0 ? 1 : 0;
}

} // namespace

int main()
{
    @autoreleasepool {
        const int result = run();
        return result == 77 ? 77 : failures != 0 ? 1 : result;
    }
}
