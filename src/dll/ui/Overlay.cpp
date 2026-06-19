// D3D11 Present-hook based ImGui overlay (kiero-style vtable hook via MinHook).
//
// We create a throwaway device + swapchain purely to read the IDXGISwapChain
// vtable, hook Present (index 8), then release the throwaway objects. The vtable
// pointer is shared process-wide, so the hook applies to the game's real
// swapchain once it presents. On the first real Present we lazily initialise
// ImGui (device/context/RTV/window come from the live swapchain) and subclass
// the window's WndProc so ImGui can see input and (when the menu is visible)
// swallow it from the game.
//
// This is the riskiest file in the project, so it sticks to stable, well-known
// APIs and never lets a bad pointer reach the game: every frame guards against
// missing D3D objects and still forwards to the original Present.

#include "ui/Overlay.h"

#include "Bot.h"
#include "ui/Menu.h"
#include "Logger.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <atomic>

#include <MinHook.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// Provided by imgui_impl_win32; forward-declared so we can feed the subclassed
// WndProc into ImGui without pulling in extra headers.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

typedef HRESULT(__stdcall* Present_t)(IDXGISwapChain*, UINT, UINT);

Present_t                 oPresent = nullptr;
WNDPROC                   oWndProc = nullptr;
ID3D11Device*             dev = nullptr;
ID3D11DeviceContext*      ctx = nullptr;
ID3D11RenderTargetView*   rtv = nullptr;
HWND                      window = nullptr;
bool                      imguiInit = false;
Bot*                      g_overlayBot = nullptr;
bool                      g_initialized = false;
std::atomic<bool>         g_shuttingDown{false};

// Subclassed window procedure. Always lets ImGui observe the message (so it can
// keep mouse/keyboard state in sync). When the menu is visible and the message
// is an input message, we swallow it from the game by returning early; the menu
// effectively grabs the keyboard/mouse while open. Otherwise we forward to the
// game's original WndProc.
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // During unload, stop touching ImGui entirely and forward to the game.
    if (g_shuttingDown.load()) {
        if (oWndProc) return CallWindowProc(oWndProc, hWnd, msg, wParam, lParam);
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    // Always let ImGui track input.
    ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);

    const bool menuVisible = (g_overlayBot && g_overlayBot->MenuVisible());
    if (menuVisible) {
        switch (msg) {
            // Keyboard.
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_CHAR:
            case WM_SYSCHAR:
            case WM_DEADCHAR:
            // Mouse buttons.
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_MBUTTONDBLCLK:
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
            case WM_XBUTTONDBLCLK:
            // Mouse movement / wheel.
            case WM_MOUSEMOVE:
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
                // Block these from reaching the game while the menu is open.
                return 1;
            default:
                break;
        }
    }

    // Fall back to the game's original handler for everything else.
    if (oWndProc) {
        return CallWindowProc(oWndProc, hWnd, msg, wParam, lParam);
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// Hooked IDXGISwapChain::Present.
HRESULT __stdcall hkPresent(IDXGISwapChain* swap, UINT sync, UINT flags) {
    // Once we're tearing down, do nothing but forward to the real Present.
    if (g_shuttingDown.load()) {
        return oPresent(swap, sync, flags);
    }

    if (!imguiInit) {
        // Lazily grab the live device/context/window from the real swapchain.
        if (SUCCEEDED(swap->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&dev))) && dev) {
            dev->GetImmediateContext(&ctx);

            DXGI_SWAP_CHAIN_DESC sd;
            ZeroMemory(&sd, sizeof(sd));
            swap->GetDesc(&sd);
            window = sd.OutputWindow;

            // Build a render target view for the back buffer.
            ID3D11Texture2D* backBuffer = nullptr;
            if (SUCCEEDED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                          reinterpret_cast<void**>(&backBuffer))) && backBuffer) {
                dev->CreateRenderTargetView(backBuffer, nullptr, &rtv);
                backBuffer->Release();
            }

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;   // don't litter the game dir with imgui.ini
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

            ImGui::StyleColorsDark();

            ImGui_ImplWin32_Init(window);
            ImGui_ImplDX11_Init(dev, ctx);

            // Subclass the window so we can feed ImGui input.
            if (window) {
                oWndProc = reinterpret_cast<WNDPROC>(
                    SetWindowLongPtr(window, GWLP_WNDPROC,
                                     reinterpret_cast<LONG_PTR>(WndProc)));
            }

            imguiInit = true;
        } else {
            // Device wasn't available this frame; try again next Present.
            if (dev) { dev->Release(); dev = nullptr; }
            return oPresent(swap, sync, flags);
        }
    }

    // If any required D3D object is missing, skip our drawing this frame but
    // still let the game present. (rtv is intentionally NOT recreated on a
    // ResizeBuffers; this overlay assumes a stable fullscreen/windowed mode.
    // If the back-buffer is recreated, rtv may become stale until reinit.)
    if (!dev || !ctx || !rtv) {
        return oPresent(swap, sync, flags);
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (g_overlayBot && g_overlayBot->MenuVisible()) {
        menu::Render(*g_overlayBot);
    }

    ImGui::Render();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    return oPresent(swap, sync, flags);
}

} // namespace

namespace overlay {

bool Init(Bot* bot) {
    g_overlayBot = bot;

    // --- Dummy window so we can create a swapchain -------------------------
    WNDCLASSEX wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = TEXT("HelloNeighorBotDummyWnd");
    RegisterClassEx(&wc);

    HWND dummy = CreateWindow(wc.lpszClassName, TEXT(""), WS_OVERLAPPEDWINDOW,
                              0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
    if (!dummy) {
        LOG_ERROR("overlay: failed to create dummy window (err=%lu)", GetLastError());
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return false;
    }

    // --- Throwaway device + swapchain to grab the Present vtable -----------
    DXGI_SWAP_CHAIN_DESC scd;
    ZeroMemory(&scd, sizeof(scd));
    scd.BufferCount = 1;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.Width = 0;
    scd.BufferDesc.Height = 0;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = dummy;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL requested[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;

    IDXGISwapChain*      tmpSwap = nullptr;
    ID3D11Device*        tmpDev = nullptr;
    ID3D11DeviceContext* tmpCtx = nullptr;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        requested,
        ARRAYSIZE(requested),
        D3D11_SDK_VERSION,
        &scd,
        &tmpSwap,
        &tmpDev,
        &obtained,
        &tmpCtx);

    if (FAILED(hr) || !tmpSwap) {
        LOG_ERROR("overlay: D3D11CreateDeviceAndSwapChain failed (hr=0x%08lX)",
                  static_cast<unsigned long>(hr));
        if (tmpCtx)  tmpCtx->Release();
        if (tmpDev)  tmpDev->Release();
        if (tmpSwap) tmpSwap->Release();
        DestroyWindow(dummy);
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return false;
    }

    // The swapchain's vtable: first member of the object is the vtable pointer.
    void** vtable = *reinterpret_cast<void***>(tmpSwap);
    void*  presentTarget = vtable[8]; // IDXGISwapChain::Present

    bool hookOk = false;
    if (MH_Initialize() == MH_OK) {
        if (MH_CreateHook(presentTarget, reinterpret_cast<void*>(&hkPresent),
                          reinterpret_cast<void**>(&oPresent)) == MH_OK) {
            if (MH_EnableHook(presentTarget) == MH_OK) {
                hookOk = true;
            }
        }
    }

    // The throwaway objects have done their job (vtable pointer is process-wide).
    if (tmpCtx)  tmpCtx->Release();
    if (tmpDev)  tmpDev->Release();
    if (tmpSwap) tmpSwap->Release();
    DestroyWindow(dummy);
    UnregisterClass(wc.lpszClassName, wc.hInstance);

    if (!hookOk) {
        LOG_ERROR("overlay: failed to hook IDXGISwapChain::Present via MinHook");
        MH_Uninitialize();
        oPresent = nullptr;
        return false;
    }

    LOG_INFO("overlay: Present hook installed (feature level 0x%X)",
             static_cast<unsigned>(obtained));
    g_initialized = true;
    return true;
}

void Shutdown() {
    if (!g_initialized) {
        return;
    }

    // 1. Signal hkPresent/WndProc to bail straight to the originals.
    g_shuttingDown.store(true);

    // 2. Stop routing Present through our hook, then give any call already
    //    inside hkPresent/WndProc time to drain before we free anything.
    MH_DisableHook(MH_ALL_HOOKS);
    Sleep(120);

    // 3. Restore the game's WndProc BEFORE tearing down ImGui, so a late
    //    window message can never reach a destroyed ImGui context.
    if (oWndProc && window) {
        SetWindowLongPtr(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(oWndProc));
    }

    // 4. Tear down ImGui.
    if (imguiInit) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        imguiInit = false;
    }

    // 5. Release the D3D objects we AddRef'd.
    if (rtv) { rtv->Release(); rtv = nullptr; }
    if (ctx) { ctx->Release(); ctx = nullptr; }
    if (dev) { dev->Release(); dev = nullptr; }

    oWndProc = nullptr;
    window = nullptr;

    // 6. Free the trampoline LAST, once nothing can still be executing it.
    MH_Uninitialize();

    g_initialized = false;
}

bool IsInitialized() {
    return g_initialized;
}

} // namespace overlay
