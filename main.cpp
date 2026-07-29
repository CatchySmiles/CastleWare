#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include <d3d9.h>
#include <tchar.h>
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <shlobj.h>
#include <shellapi.h>
#include <fstream>
#include "backend.h"
#include "frontend.h"
#include "offsets.h"



static LPDIRECT3D9 g_d3d = nullptr;
static LPDIRECT3DDEVICE9 g_device = nullptr;
static D3DPRESENT_PARAMETERS g_d3dpp = {};
static bool g_deviceLost = false;

bool CreateD3D(HWND hwnd);
void CleanupD3D();
void ResetD3D();
LRESULT WINAPI WndProc(HWND, UINT, WPARAM, LPARAM);

int main(int, char**) {
    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"CastleWare";
    ::RegisterClassExW(&wc);

    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);
    HWND hwnd = ::CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST, wc.lpszClassName, L"CastleWare",
        WS_POPUP | WS_VISIBLE, 0, 0, sx, sy, nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateD3D(hwnd)) {
        CleanupD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);
    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    ImGui::StyleColorsDark();
    ImGuiIO& ioRef = ImGui::GetIO();
    // Try to load a nicer system font (Segoe UI). Fallback to default if not found.
    ImFont* loadedFont = nullptr;
    const char* sysFontPath = "C:\\Windows\\Fonts\\segoeui.ttf";
    if (GetFileAttributesA(sysFontPath) != INVALID_FILE_ATTRIBUTES) {
        loadedFont = ioRef.Fonts->AddFontFromFileTTF(sysFontPath, 16.0f);
    }
    if (!loadedFont) ioRef.Fonts->AddFontDefault();
    // Slightly scale fonts for clearer rendering on modern displays
    ioRef.FontGlobalScale = 1.05f;
    auto& style = ImGui::GetStyle();
    // Enable anti-aliasing for lines/fills to reduce pixelated appearance
    style.AntiAliasedLines = true;
    style.AntiAliasedFill = true;
    style.WindowRounding = 10.0f;
    style.FrameRounding = 8.0f;
    style.TabRounding = 8.0f;
    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 8);
    style.ScrollbarRounding = 8.0f;
    style.Colors[ImGuiCol_Text] = ImVec4(0.94f, 0.97f, 0.94f, 1.00f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.03f, 0.05f, 0.04f, 0.98f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.045f, 0.08f, 0.06f, 0.96f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.07f, 0.14f, 0.09f, 0.99f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.09f, 0.12f, 0.09f, 0.88f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.34f, 0.22f, 0.98f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.38f, 0.26f, 1.00f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.08f, 0.12f, 0.09f, 0.92f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.40f, 0.26f, 0.98f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.45f, 0.30f, 1.00f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.07f, 0.12f, 0.09f, 0.86f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.36f, 0.22f, 0.98f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.28f, 0.44f, 0.28f, 1.00f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.06f, 0.10f, 0.08f, 0.92f);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.40f, 0.26f, 0.98f);
    style.Colors[ImGuiCol_TabActive] = ImVec4(0.30f, 0.46f, 0.30f, 1.00f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.09f, 0.13f, 0.09f, 0.65f);
    style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.30f);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_device);

    LoadAccounts();
    LoadSettings();

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_deviceLost) {
            if (g_device->TestCooperativeLevel() == D3DERR_DEVICENOTRESET) ResetD3D();
            g_deviceLost = false;
        }

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        TryAttachOrRefresh();
        HandleHotkeys();

        if (showMenu) RenderInterface();

        ApplyCheats();

        ImGui::EndFrame();

        g_device->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        g_device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_RGBA(0, 0, 0, 0), 1.f, 0);

        if (g_device->BeginScene() >= 0) {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_device->EndScene();
        }

        if (g_device->Present(nullptr, nullptr, nullptr, nullptr) == D3DERR_DEVICELOST)
            g_deviceLost = true;

        ::Sleep(6);
    }

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    if (hProc) CloseHandle(hProc);
    return 0;
}

bool CreateD3D(HWND hwnd) {
    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_d3d) return false;

    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    return SUCCEEDED(g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, &g_device));
}

void CleanupD3D() {
    if (g_device) { g_device->Release(); g_device = nullptr; }
    if (g_d3d) { g_d3d->Release(); g_d3d = nullptr; }
}

void ResetD3D() {
    ImGui_ImplDX9_InvalidateDeviceObjects();
    g_device->Reset(&g_d3dpp);
    ImGui_ImplDX9_CreateDeviceObjects();
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            g_d3dpp.BackBufferWidth = LOWORD(lp);
            g_d3dpp.BackBufferHeight = HIWORD(lp);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}


