#include "include/cef_app.h"
#include "include/wrapper/cef_library_loader.h"

#if defined(CEF_USE_SANDBOX)
#include "include/cef_sandbox_mac.h"
#endif

int main(int argc, char* argv[])
{
#if defined(CEF_USE_SANDBOX)
    CefScopedSandboxContext sandboxContext;
    if (!sandboxContext.Initialize(argc, argv))
        return 1;
#endif

    CefScopedLibraryLoader libraryLoader;
    if (!libraryLoader.LoadInHelper())
        return 2;

    CefMainArgs mainArgs(argc, argv);
    return CefExecuteProcess(mainArgs, nullptr, nullptr);
}
