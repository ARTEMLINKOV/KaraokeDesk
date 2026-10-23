#import <AppKit/AppKit.h>

extern "C" void configureKaraokeWindowForSpaces(void* nativeWindow);

extern "C" void configureKaraokeWindowForSpaces(void* nativeWindow)
{
    NSView* view = (__bridge NSView*) nativeWindow;
    NSWindow* window = view.window;
    if (window == nil)
        return;

    window.collectionBehavior |= NSWindowCollectionBehaviorCanJoinAllSpaces
        | NSWindowCollectionBehaviorFullScreenAuxiliary;
}
