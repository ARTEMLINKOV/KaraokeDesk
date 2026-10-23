#import <Cocoa/Cocoa.h>

#include "AppleMusicChromium.h"
#include "include/cef_app.h"
#include "include/cef_application_mac.h"
#include "include/cef_browser.h"
#include "include/cef_browser_process_handler.h"
#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_library_loader.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>
#include <utility>
#include <vector>

@interface KaraokeDeskCEFApplication : NSApplication<CefAppProtocol> {
 @private
    BOOL handlingSendEvent_;
}
@end

@implementation KaraokeDeskCEFApplication
- (BOOL)isHandlingSendEvent
{
    return handlingSendEvent_;
}

- (void)setHandlingSendEvent:(BOOL)handlingSendEvent
{
    handlingSendEvent_ = handlingSendEvent;
}

- (void)sendEvent:(NSEvent*)event
{
    CefScopedSendingEvent sendingEventScoper;
    [super sendEvent:event];
}
@end

namespace
{
    CefScopedLibraryLoader libraryLoader;
    std::atomic<bool> cefInitialized { false };
    std::atomic<uint64_t> pumpGeneration { 0 };
    std::vector<CefRefPtr<CefBrowser>> activeBrowsers;
    juce::String startupError;

    class KaraokeDeskCefApp final : public CefApp,
                                    public CefBrowserProcessHandler
    {
    public:
        CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override
        {
            return this;
        }

        void OnScheduleMessagePumpWork(int64_t delayMs) override
        {
            const auto generation = ++pumpGeneration;
            if (delayMs < 0)
                return;

            const auto delay = std::max<int64_t>(0, delayMs);
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay * NSEC_PER_MSEC),
                           dispatch_get_main_queue(), ^{
                if (cefInitialized.load() && generation == pumpGeneration.load())
                    CefDoMessageLoopWork();
            });
        }

    private:
        IMPLEMENT_REFCOUNTING(KaraokeDeskCefApp);
    };

    class AppleMusicBrowserClient final : public CefClient,
                                          public CefLifeSpanHandler
    {
    public:
        using BrowserCallback = std::function<void(CefRefPtr<CefBrowser>)>;

        AppleMusicBrowserClient(BrowserCallback onCreatedCallback,
                                BrowserCallback onClosedCallback)
            : onCreated(std::move(onCreatedCallback)),
              onClosed(std::move(onClosedCallback))
        {
        }

        CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override
        {
            return this;
        }

        void OnAfterCreated(CefRefPtr<CefBrowser> browser) override
        {
            CEF_REQUIRE_UI_THREAD();
            activeBrowsers.push_back(browser);
            if (onCreated)
                onCreated(browser);
        }

        void OnBeforeClose(CefRefPtr<CefBrowser> browser) override
        {
            CEF_REQUIRE_UI_THREAD();
            if (onClosed)
                onClosed(browser);
            const int identifier = browser->GetIdentifier();
            activeBrowsers.erase(
                std::remove_if(activeBrowsers.begin(), activeBrowsers.end(),
                    [identifier](const CefRefPtr<CefBrowser>& active)
                    {
                        return active->GetIdentifier() == identifier;
                    }),
                activeBrowsers.end());
        }

    private:
        BrowserCallback onCreated;
        BrowserCallback onClosed;
        IMPLEMENT_REFCOUNTING(AppleMusicBrowserClient);
    };
}

struct AppleMusicChromiumBrowser::Impl
{
    CefRefPtr<AppleMusicBrowserClient> client;
    CefRefPtr<CefBrowser> browser;
};

bool initializeAppleMusicChromium(int argc, char* argv[], juce::String& error)
{
    if (cefInitialized.load())
        return true;

    if (!libraryLoader.LoadInMain())
    {
        error = "Не удалось загрузить Chromium Embedded Framework.";
        startupError = error;
        return false;
    }

    @autoreleasepool
    {
        [NSApplication sharedApplication];
        if (![NSApp isKindOfClass:[KaraokeDeskCEFApplication class]])
        {
            error = "В приложении не настроен главный класс CEF.";
            startupError = error;
            return false;
        }

        const auto appData = juce::File::getSpecialLocation(
            juce::File::userApplicationDataDirectory).getChildFile("KaraokeDesk");
        const auto cacheRoot = appData.getChildFile("AppleMusicChromium");
        const auto cachePath = cacheRoot.getChildFile("Default");
        if (auto result = cachePath.createDirectory(); result.failed())
        {
            error = "Не удалось создать профиль Chromium: " + result.getErrorMessage();
            startupError = error;
            return false;
        }

        CefMainArgs mainArgs(argc, argv);
        CefSettings settings;
        settings.external_message_pump = true;
        settings.persist_session_cookies = true;
        settings.log_severity = LOGSEVERITY_WARNING;
        CefString(&settings.root_cache_path) = cacheRoot.getFullPathName().toStdString();
        CefString(&settings.cache_path) = cachePath.getFullPathName().toStdString();

        CefRefPtr<KaraokeDeskCefApp> app(new KaraokeDeskCefApp());
        if (!CefInitialize(mainArgs, settings, app.get(), nullptr))
        {
            error = "Chromium не смог запуститься (CEF initialization failed).";
            startupError = error;
            return false;
        }
    }

    startupError.clear();
    cefInitialized.store(true);
    return true;
}

void shutdownAppleMusicChromium()
{
    if (!cefInitialized.load())
        return;

    const auto browsersToClose = activeBrowsers;
    for (const auto& browser : browsersToClose)
        browser->GetHost()->CloseBrowser(true);

    for (int attempt = 0; attempt < 1000 && !activeBrowsers.empty(); ++attempt)
    {
        CefDoMessageLoopWork();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!activeBrowsers.empty())
        juce::Logger::writeToLog("CEF browsers did not close before shutdown.");

    ++pumpGeneration;
    cefInitialized.store(false);
    if (activeBrowsers.empty())
        CefShutdown();
}

juce::String getAppleMusicChromiumStartupError()
{
    return startupError;
}

AppleMusicChromiumBrowser::AppleMusicChromiumBrowser()
    : impl(std::make_shared<Impl>())
{
    NSView* hostView = [[NSView alloc] initWithFrame:NSZeroRect];
    hostView.wantsLayer = YES;
    setView((void*)hostView);
    [hostView release];
}

AppleMusicChromiumBrowser::~AppleMusicChromiumBrowser()
{
    if (impl->browser)
        impl->browser->GetHost()->CloseBrowser(true);
}

bool AppleMusicChromiumBrowser::loadUrl(const juce::String& url)
{
    if (!cefInitialized.load() || url.isEmpty())
        return false;

    if (impl->browser)
    {
        impl->browser->GetMainFrame()->LoadURL(url.toStdString());
        return true;
    }

    const std::weak_ptr<Impl> weakImpl = impl;
    impl->client = new AppleMusicBrowserClient(
        [weakImpl](CefRefPtr<CefBrowser> browser)
        {
            if (const auto state = weakImpl.lock())
            {
                if (!state->browser)
                    state->browser = browser;
            }
            else
                browser->GetHost()->CloseBrowser(true);
        },
        [weakImpl](CefRefPtr<CefBrowser> browser)
        {
            if (const auto state = weakImpl.lock(); state && state->browser
                && state->browser->GetIdentifier() == browser->GetIdentifier())
                state->browser = nullptr;
        });
    CefWindowInfo windowInfo;
    windowInfo.SetAsChild(static_cast<CefWindowHandle>(getView()),
                          CefRect(0, 0, juce::jmax(1, getWidth()), juce::jmax(1, getHeight())));
    CefBrowserSettings browserSettings;
    if (!CefBrowserHost::CreateBrowser(windowInfo, impl->client.get(),
                                       url.toStdString(), browserSettings, nullptr, nullptr))
    {
        impl->client = nullptr;
        return false;
    }
    return true;
}

void AppleMusicChromiumBrowser::pauseMedia()
{
    if (impl->browser && impl->browser->GetMainFrame())
        impl->browser->GetMainFrame()->ExecuteJavaScript(
            "document.querySelectorAll('audio,video').forEach(e=>e.pause());",
            impl->browser->GetMainFrame()->GetURL(), 0);
}

void AppleMusicChromiumBrowser::resized()
{
    if (impl->browser)
        impl->browser->GetHost()->WasResized();
}
