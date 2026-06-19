#include "Bot.h"
#include <windows.h>

// The single global Bot instance, created on the bot thread. Subsystems (overlay,
// menu) reach the orchestrator through this pointer.
Bot* g_bot = nullptr;

// Non-capturing thread proc: create the Bot and run it. Bot::Run blocks until the
// DLL is unloaded and ends by calling FreeLibraryAndExitThread, so this thread
// never returns normally; the `return 0` only satisfies the signature.
static DWORD WINAPI BotThreadProc(LPVOID param)
{
    g_bot = new Bot();
    g_bot->Run(static_cast<HMODULE>(param));
    return 0;
}

BOOL APIENTRY DllMain(HINSTANCE hinst, DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hinst);
        HANDLE thread = CreateThread(nullptr, 0, BotThreadProc, hinst, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }
    return TRUE;
}
