#import <Cocoa/Cocoa.h>

#include "AppleMusicChromium.h"
#include "include/cef_app.h"
#include "include/cef_application_mac.h"
#include "include/cef_browser.h"
#include "include/cef_browser_process_handler.h"
#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_library_loader.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
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
    std::atomic<uint64_t> pumpCancellationGeneration { 0 };
    std::atomic<int> scheduledPumpCalls { 0 };
    std::atomic<int> executedPumpCalls { 0 };
    std::atomic<int> startupWatchdogPumpCalls { 0 };
    std::vector<CefRefPtr<CefBrowser>> activeBrowsers;
    juce::String startupError;
    juce::File cefLogFile;

    std::string escapeHtml(std::string text)
    {
        const std::pair<const char*, const char*> replacements[] = {
            { "&", "&amp;" }, { "<", "&lt;" }, { ">", "&gt;" },
            { "\"", "&quot;" }, { "'", "&#39;" }
        };
        for (const auto& [character, replacement] : replacements)
        {
            size_t position = 0;
            while ((position = text.find(character, position)) != std::string::npos)
            {
                text.replace(position, std::char_traits<char>::length(character), replacement);
                position += std::char_traits<char>::length(replacement);
            }
        }
        return text;
    }

    std::string makeDataUrl(const std::string& html)
    {
        std::ostringstream encoded;
        encoded << "data:text/html;charset=utf-8,";
        for (const unsigned char character : html)
        {
            if (std::isalnum(character) || character == '-' || character == '_'
                || character == '.' || character == '~')
                encoded << character;
            else
                encoded << '%' << std::uppercase << std::hex << std::setw(2)
                        << std::setfill('0') << static_cast<int>(character);
        }
        return encoded.str();
    }

    void logBrowserError(const std::string& message)
    {
        cefLogFile.getParentDirectory().createDirectory();
        std::ofstream log(cefLogFile.getFullPathName().toStdString(), std::ios::app);
        if (log)
            log << message << '\n';
    }

    void setStartupError(const juce::String& message)
    {
        startupError = message;
        logBrowserError(message.toStdString());
    }

    juce::File getChromiumDataDirectory()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Application Support/KaraokeDesk/AppleMusicChromium");
    }

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
            const auto cancellationGeneration = ++pumpCancellationGeneration;
            if (scheduledPumpCalls.fetch_add(1) < 10)
                logBrowserError("CEF scheduled message pump (delay "
                                + std::to_string(delayMs) + " ms).");
            const auto delay = std::max<int64_t>(0, delayMs);
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay * NSEC_PER_MSEC),
                           dispatch_get_main_queue(), ^{
                if (cefInitialized.load()
                    && cancellationGeneration == pumpCancellationGeneration.load())
                {
                    if (executedPumpCalls.fetch_add(1) < 10)
                        logBrowserError("CEF message pump callback executed.");
                    CefDoMessageLoopWork();
                }
            });
        }

    private:
        IMPLEMENT_REFCOUNTING(KaraokeDeskCefApp);
    };

    class AppleMusicBrowserClient final : public CefClient,
                                          public CefLifeSpanHandler,
                                          public CefLoadHandler
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

        CefRefPtr<CefLoadHandler> GetLoadHandler() override
        {
            return this;
        }

        void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                         ErrorCode errorCode, const CefString& errorText,
                         const CefString& failedUrl) override
        {
            CEF_REQUIRE_UI_THREAD();
            if (!frame->IsMain() || errorCode == ERR_ABORTED)
                return;

            const auto description = errorText.ToString();
            const auto url = failedUrl.ToString();
            logBrowserError("Apple Music navigation failed (" + std::to_string(errorCode)
                            + "): " + description + " [" + url + "]");

            const auto page =
                "<!doctype html><html lang=\"ru\"><meta charset=\"utf-8\">"
                "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                "<title>Не удалось загрузить Apple Music</title>"
                "<body style=\"margin:0;background:#10131a;color:#eee;font:16px sans-serif\">"
                "<main style=\"max-width:700px;margin:12vh auto;padding:24px\">"
                "<h2>Не удалось загрузить Apple Music</h2>"
                "<p>Проверьте подключение к интернету и попробуйте открыть страницу снова.</p>"
                "<p>Код ошибки Chromium: " + std::to_string(errorCode) + "</p>"
                "<p>" + escapeHtml(description) + "</p>"
                "<p>Адрес: " + escapeHtml(url) + "</p></main></body></html>";
            frame->LoadURL(makeDataUrl(page));
        }

        void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                       int httpStatusCode) override
        {
            CEF_REQUIRE_UI_THREAD();
            if (frame->IsMain())
                logBrowserError("Apple Music navigation completed (HTTP "
                                + std::to_string(httpStatusCode) + "): "
                                + frame->GetURL().ToString());
        }

        void OnAfterCreated(CefRefPtr<CefBrowser> browser) override
        {
            CEF_REQUIRE_UI_THREAD();
            activeBrowsers.push_back(browser);
            logBrowserError("Apple Music browser created.");
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

    const auto cacheRoot = getChromiumDataDirectory();
    cefLogFile = cacheRoot.getChildFile("cef.log");
    if (auto result = cacheRoot.createDirectory(); result.failed())
    {
        error = juce::String::fromUTF8("Не удалось создать папку диагностики Chromium: ")
                + result.getErrorMessage();
        setStartupError(error);
        return false;
    }
    {
        std::ofstream log(cefLogFile.getFullPathName().toStdString(), std::ios::trunc);
        if (!log)
        {
            error = juce::String::fromUTF8("Не удалось создать журнал Chromium: ")
                    + cefLogFile.getFullPathName();
            startupError = error;
            return false;
        }
        log << "KaraokeDesk CEF startup\n";
    }

    if (!libraryLoader.LoadInMain())
    {
        error = juce::String::fromUTF8("Не удалось загрузить Chromium Embedded Framework.");
        setStartupError(error);
        return false;
    }

    @autoreleasepool
    {
        auto* application = [KaraokeDeskCEFApplication sharedApplication];
        if (![application isKindOfClass:[KaraokeDeskCEFApplication class]])
        {
            error = juce::String::fromUTF8("Не удалось создать NSApplication для Chromium.");
            setStartupError(error);
            return false;
        }

        const auto contentsDirectory = juce::File::getSpecialLocation(
            juce::File::currentExecutableFile).getParentDirectory().getParentDirectory();
        const auto helperExecutable = contentsDirectory.getChildFile(
            "Frameworks/KaraokeDesk Helper.app/Contents/MacOS/KaraokeDesk Helper");
        logBrowserError("CEF helper path: " + helperExecutable.getFullPathName().toStdString());
        if (!helperExecutable.existsAsFile())
        {
            error = juce::String::fromUTF8("Не найден Chromium helper: ")
                    + helperExecutable.getFullPathName();
            setStartupError(error);
            return false;
        }

        const auto cachePath = cacheRoot.getChildFile("Default");
        if (auto result = cachePath.createDirectory(); result.failed())
        {
            error = juce::String::fromUTF8("Не удалось создать профиль Chromium: ")
                    + result.getErrorMessage();
            setStartupError(error);
            return false;
        }

        CefMainArgs mainArgs(argc, argv);
        CefSettings settings;
        settings.external_message_pump = true;
        settings.persist_session_cookies = true;
        settings.log_severity = LOGSEVERITY_INFO;
        CefString(&settings.browser_subprocess_path) =
            helperExecutable.getFullPathName().toStdString();
        CefString(&settings.log_file) = cefLogFile.getFullPathName().toStdString();
        CefString(&settings.root_cache_path) = cacheRoot.getFullPathName().toStdString();
        CefString(&settings.cache_path) = cachePath.getFullPathName().toStdString();

        CefRefPtr<KaraokeDeskCefApp> app(new KaraokeDeskCefApp());
        logBrowserError("Calling CefInitialize.");
        if (!CefInitialize(mainArgs, settings, app.get(), nullptr))
        {
            error = juce::String::fromUTF8(
                "Chromium не смог запуститься (CEF initialization failed).\nЖурнал: ")
                    + cefLogFile.getFullPathName();
            setStartupError(error);
            return false;
        }
        logBrowserError("CefInitialize succeeded.");
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

    ++pumpCancellationGeneration;
    cefInitialized.store(false);
    if (activeBrowsers.empty())
        CefShutdown();
}

juce::String getAppleMusicChromiumStartupError()
{
    return startupError;
}

juce::String getAppleMusicChromiumLogPath()
{
    return getChromiumDataDirectory().getChildFile("cef.log").getFullPathName();
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
    if (!cefInitialized.load())
    {
        if (startupError.isEmpty())
            setStartupError(juce::String::fromUTF8("Chromium не был инициализирован."));
        return false;
    }
    if (url.isEmpty())
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
    logBrowserError("Requesting Apple Music browser creation.");
    if (!CefBrowserHost::CreateBrowser(windowInfo, impl->client.get(),
                                       url.toStdString(), browserSettings, nullptr, nullptr))
    {
        impl->client = nullptr;
        setStartupError(juce::String::fromUTF8(
            "CEF отклонил создание браузера. Подробности записаны в cef.log."));
        return false;
    }
    logBrowserError("CEF accepted Apple Music browser creation.");
    return true;
}

bool AppleMusicChromiumBrowser::isBrowserCreated() const
{
    return impl->browser != nullptr;
}

void AppleMusicChromiumBrowser::pumpMessageLoop()
{
    if (!cefInitialized.load() || impl->browser)
        return;

    CEF_REQUIRE_UI_THREAD();
    if (startupWatchdogPumpCalls.fetch_add(1) < 5)
        logBrowserError("CEF startup watchdog pump executed.");
    CefDoMessageLoopWork();
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
