#include "include/cef_app.h"
#include "include/wrapper/cef_library_loader.h"

#include <cstdlib>
#include <fstream>
#include <string>

#if defined(CEF_USE_SANDBOX)
#include "include/cef_sandbox_mac.h"
#endif

namespace
{
    void writeHelperLog(const std::string& message)
    {
        const auto* home = std::getenv("HOME");
        if (home == nullptr)
            return;

        const auto path = std::string(home)
            + "/Library/Application Support/KaraokeDesk/AppleMusicChromium/cef.log";
        std::ofstream log(path, std::ios::app);
        if (log)
            log << message << '\n';
    }
}

int main(int argc, char* argv[])
{
    writeHelperLog("CEF helper process started.");
#if defined(CEF_USE_SANDBOX)
    CefScopedSandboxContext sandboxContext;
    if (!sandboxContext.Initialize(argc, argv))
    {
        writeHelperLog("CEF helper sandbox initialization failed.");
        return 1;
    }
#endif

    CefScopedLibraryLoader libraryLoader;
    if (!libraryLoader.LoadInHelper())
    {
        writeHelperLog("CEF helper failed to load CEF framework.");
        return 2;
    }

    CefMainArgs mainArgs(argc, argv);
    const auto result = CefExecuteProcess(mainArgs, nullptr, nullptr);
    writeHelperLog("CEF helper process exited with code " + std::to_string(result) + ".");
    return result;
}
