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
#include "json.hpp"
#include "offsets.h"

using json = nlohmann::json;

// Constants
static const uint32_t PSZ_DEFAULT = 1065353216u;
static const uint32_t PSZ_TINY = 985353216u;
static const uint32_t PSZ_NOCOLL = 0u;
static const uint32_t JUMP_INF = 6400u;

static LPDIRECT3D9 g_d3d = nullptr;
static LPDIRECT3DDEVICE9 g_device = nullptr;
static D3DPRESENT_PARAMETERS g_d3dpp = {};
static bool g_deviceLost = false;

static HANDLE hProc = nullptr;
static DWORD pid = 0;
static uintptr_t modBase = 0;
static uintptr_t playerPtr = 0;

static bool noclip = false;
static bool noCollision = false;
static bool infJump = false;
static bool showMenu = true;
static float maxSpeedValue = 0.0f; // (unused, kept for compatibility)
static bool freezeSpeed = false; // (unused)
// Player position UI values
static float playerPosX = 0.0f; // West/East (X)
static float playerPosZ = 0.0f; // North/South (Z)
static float playerPosY = 0.0f; // Height (Y)
static uintptr_t lastPlayerPtrForPos = 0;
// Hide name toggle (client-side byte at hideNameOffset)
static bool hideName = false;

struct KeyBind {
    int key = 0;
    bool ctrl = false, shift = false, alt = false;
};

static KeyBind bindNoclip, bindNoColl, bindInfJump, bindToggleUI{ VK_INSERT };
static KeyBind bindHideName;
static int listeningFor = 0;

struct Account {
    std::string user, pass;
};
static std::vector<Account> accounts;
static int selectedAcc = -1;
static std::string statusMsg = "Ready";

// Forward declarations
bool CreateD3D(HWND hwnd);
void CleanupD3D();
void ResetD3D();
LRESULT WINAPI WndProc(HWND, UINT, WPARAM, LPARAM);

DWORD GetProcId(const std::wstring& name);
uintptr_t GetModuleBase(DWORD pid, const std::wstring& modName);
bool Attach();
void Detach();
void TryAttachOrRefresh();

bool ReadU32(uintptr_t base, uintptr_t off, uint32_t& v);
bool WriteU32(uintptr_t base, uintptr_t off, uint32_t v);

std::string KeyToString(const KeyBind& b);
void HandleHotkeys();
std::string AppDataPath(const std::string& fn);
void LoadAccounts();
void SaveAccounts();
bool AddAccount(const std::string& u, const std::string& p);
void WriteConnectionCfg(const Account& acc);
void LaunchGame();
void LoginSelected();

void LoadSettings();
void SaveSettings();
void RenderInterface();
void ApplyCheats();

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
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 6.f;
    style.FrameRounding = 4.f;
    style.Colors[ImGuiCol_WindowBg] = { 0.0f, 0.03f, 0.03f, 0.90f };
    style.Colors[ImGuiCol_ModalWindowDimBg] = { 0,0,0,0 };

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

DWORD GetProcId(const std::wstring& name) {
    DWORD pid = 0;
    PROCESSENTRY32W pe{ sizeof(pe) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (name == pe.szExeFile) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

uintptr_t GetModuleBase(DWORD pid, const std::wstring& mod) {
    MODULEENTRY32W me{ sizeof(me) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    uintptr_t base = 0;
    if (Module32FirstW(snap, &me)) {
        do {
            if (mod == me.szModule) { base = (uintptr_t)me.modBaseAddr; break; }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return base;
}

bool Attach() {
    if (hProc) return true;
    pid = GetProcId(targetProcessName);
    if (!pid) return false;
    hProc = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProc) return false;
    modBase = GetModuleBase(pid, targetProcessName);
    if (!modBase) { CloseHandle(hProc); hProc = nullptr; return false; }

    uint32_t ptr = 0;
    if (!ReadProcessMemory(hProc, (LPCVOID)(modBase + moduleBaseOffset), &ptr, 4, nullptr)) {
        CloseHandle(hProc); hProc = nullptr; return false;
    }
    playerPtr = ptr;
    return true;
}

void Detach() {
    if (hProc) CloseHandle(hProc);
    hProc = nullptr; pid = 0; modBase = 0; playerPtr = 0;
}

void TryAttachOrRefresh() {
    if (hProc) {
        DWORD ec; if (!GetExitCodeProcess(hProc, &ec) || ec != STILL_ACTIVE) { Detach(); return; }
        uintptr_t mb = GetModuleBase(pid, targetProcessName);
        if (!mb) { Detach(); return; }
        if (mb != modBase) modBase = mb;

        uint32_t p = 0;
        if (ReadProcessMemory(hProc, (LPCVOID)(modBase + moduleBaseOffset), &p, 4, nullptr))
            playerPtr = p;
        else
            Detach();
    }
    else {
        Attach();
    }
}

bool ReadU32(uintptr_t base, uintptr_t off, uint32_t& v) {
    if (!hProc || !base) return false;
    return ReadProcessMemory(hProc, (LPCVOID)(base + off), &v, 4, nullptr);
}

bool WriteU32(uintptr_t base, uintptr_t off, uint32_t v) {
    if (!hProc || !base) return false;
    return WriteProcessMemory(hProc, (LPVOID)(base + off), &v, 4, nullptr);
}

std::string KeyToString(const KeyBind& b) {
    if (!b.key) return "None";
    std::string s;
    if (b.ctrl) s += "Ctrl+";
    if (b.shift) s += "Shift+";
    if (b.alt) s += "Alt+";
    if (b.key >= 'A' && b.key <= 'Z') return s + char(b.key);
    if (b.key >= '0' && b.key <= '9') return s + char(b.key);
    if (b.key >= VK_F1 && b.key <= VK_F12) s += "F" + std::to_string(b.key - VK_F1 + 1);
    else if (b.key == VK_SPACE) s += "Space";
    else if (b.key == VK_INSERT) s += "Insert";
    else if (b.key == VK_DELETE) s += "Delete";
    else s += "Key";
    return s;
}

bool IsPressed(const KeyBind& b) {
    if (!b.key) return false;
    if (!(GetAsyncKeyState(b.key) & 0x8000)) return false;
    bool c = GetAsyncKeyState(VK_CONTROL) & 0x8000;
    bool s = GetAsyncKeyState(VK_SHIFT) & 0x8000;
    bool a = GetAsyncKeyState(VK_MENU) & 0x8000;
    return (b.ctrl == !!c) && (b.shift == !!s) && (b.alt == !!a);
}

bool IsModifier(int vk) {
    switch (vk) {
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
        return true;
    default: return false;
    }
}

void HandleHotkeys() {
    if (listeningFor != 0) {
        if (GetAsyncKeyState(VK_ESCAPE) & 1) {
            listeningFor = 0;
            return;
        }
        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        for (int k = 8; k <= 255; ++k) {
            if (IsModifier(k) || k == VK_ESCAPE) continue;
            if (GetAsyncKeyState(k) & 1) {
                KeyBind newBind = { k, ctrl, shift, alt };
                if (listeningFor == 1) bindNoclip = newBind;
                else if (listeningFor == 2) bindNoColl = newBind;
                else if (listeningFor == 3) bindInfJump = newBind;
                else if (listeningFor == 4) bindToggleUI = newBind;
                else if (listeningFor == 5) bindHideName = newBind;
                listeningFor = 0;
                return;
            }
        }
        return;
    }

    static bool prevState[5] = { false, false, false, false, false };
    bool now[5] = { IsPressed(bindNoclip), IsPressed(bindNoColl), IsPressed(bindInfJump), IsPressed(bindToggleUI), IsPressed(bindHideName) };

    if (now[0] && !prevState[0]) noclip = !noclip;
    if (now[1] && !prevState[1]) noCollision = !noCollision;
    if (now[2] && !prevState[2]) infJump = !infJump;
    if (now[3] && !prevState[3]) showMenu = !showMenu;
    if (now[4] && !prevState[4]) {
        hideName = !hideName;
        uint8_t v = hideName ? 1u : 0u;
        WriteByteAt(hProc, playerPtr, hideNameOffset, v);
    }

    memcpy(prevState, now, sizeof(prevState));
}

std::string AppDataPath(const std::string& fn) {
    char p[MAX_PATH]{};
    if (FAILED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, p))) return "";
    std::string dir = std::string(p) + "\\Cubic";
    CreateDirectoryA(dir.c_str(), NULL);
    return dir + "\\" + fn;
}

void LoadAccounts() {
    accounts.clear();
    std::ifstream f(AppDataPath("accounts.json"));
    if (!f) return;
    try {
        json j; f >> j;
        if (!j.is_array()) return;
        for (auto& e : j) {
            std::string u = e.value("username", "");
            std::string p = e.value("password", "");
            u.erase(0, u.find_first_not_of(" \t\r\n"));
            u.erase(u.find_last_not_of(" \t\r\n") + 1);
            if (!u.empty()) accounts.push_back({ u,p });
        }
    }
    catch (...) {}
}

void SaveAccounts() {
    json arr = json::array();
    for (auto& a : accounts) {
        std::string u = a.user;
        u.erase(0, u.find_first_not_of(" \t\r\n"));
        u.erase(u.find_last_not_of(" \t\r\n") + 1);
        if (!u.empty()) arr.push_back({ {"username", a.user}, {"password", a.pass} });
    }
    std::ofstream f(AppDataPath("accounts.json"), std::ios::trunc);
    if (f) f << arr.dump(2);
}

bool AddAccount(const std::string& u, const std::string& p) {
    std::string tu = u, tp = p;
    tu.erase(0, tu.find_first_not_of(" \t\r\n")); tu.erase(tu.find_last_not_of(" \t\r\n") + 1);
    tp.erase(0, tp.find_first_not_of(" \t\r\n")); tp.erase(tp.find_last_not_of(" \t\r\n") + 1);
    if (tu.empty() || tp.empty()) return false;
    for (auto& a : accounts) if (a.user == tu) return false;
    accounts.push_back({ tu, tp });
    SaveAccounts();
    return true;
}

void WriteConnectionCfg(const Account& a) {
    std::ofstream f(AppDataPath("Connection.cfg"), std::ios::trunc);
    if (!f) return;
    f << "Game.RememberMe=true\nGame.Username=" << a.user << "\nGame.Password=" << a.pass << "\n";
}

void LaunchGame() {
    ShellExecuteW(NULL, L"open", L"steam://rungameid/317470", NULL, NULL, SW_SHOWNORMAL);
}

void LoginSelected() {
    if (selectedAcc < 0 || selectedAcc >= (int)accounts.size()) return;
    if (GetProcId(L"Cubic.exe")) Sleep(800);
    WriteConnectionCfg(accounts[selectedAcc]);
    LaunchGame();
}

void LoadSettings() {
    std::ifstream f(AppDataPath("config.json"));
    if (!f) return;
    try {
        json j; f >> j;
        noclip = j.value("noclip", false);
        noCollision = j.value("noCollision", false);
        infJump = j.value("infJump", false);

        bindNoclip = { j.value("noclipKey",0), j.value("noclipCtrl",false), j.value("noclipShift",false), j.value("noclipAlt",false) };
        bindNoColl = { j.value("noCollKey",0), j.value("noCollCtrl",false), j.value("noCollShift",false), j.value("noCollAlt",false) };
        bindInfJump = { j.value("infJumpKey",0), j.value("infJumpCtrl",false), j.value("infJumpShift",false), j.value("infJumpAlt",false) };
        bindToggleUI = { j.value("toggleKey",VK_INSERT), j.value("toggleCtrl",false), j.value("toggleShift",false), j.value("toggleAlt",false) };
        bindHideName = { j.value("hideNameKey",0), j.value("hideNameCtrl",false), j.value("hideNameShift",false), j.value("hideNameAlt",false) };
    }
    catch (...) {}
}

void SaveSettings() {
    json j;
    j["noclip"] = noclip;
    j["noCollision"] = noCollision;
    j["infJump"] = infJump;
    j["noclipKey"] = bindNoclip.key; j["noclipCtrl"] = bindNoclip.ctrl; j["noclipShift"] = bindNoclip.shift; j["noclipAlt"] = bindNoclip.alt;
    j["noCollKey"] = bindNoColl.key; j["noCollCtrl"] = bindNoColl.ctrl; j["noCollShift"] = bindNoColl.shift; j["noCollAlt"] = bindNoColl.alt;
    j["infJumpKey"] = bindInfJump.key; j["infJumpCtrl"] = bindInfJump.ctrl; j["infJumpShift"] = bindInfJump.shift; j["infJumpAlt"] = bindInfJump.alt;
    j["toggleKey"] = bindToggleUI.key; j["toggleCtrl"] = bindToggleUI.ctrl; j["toggleShift"] = bindToggleUI.shift; j["toggleAlt"] = bindToggleUI.alt;
    j["hideNameKey"] = bindHideName.key; j["hideNameCtrl"] = bindHideName.ctrl; j["hideNameShift"] = bindHideName.shift; j["hideNameAlt"] = bindHideName.alt;
    std::ofstream f(AppDataPath("config.json"), std::ios::trunc);
    if (f) f << j.dump(2);
}

void RenderInterface() {
    // Set an initial window size only on first use so the user can resize by dragging edges later.
    ImGui::SetNextWindowSize({ 375, 0 }, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("CastleWare", nullptr, 0)) {
        ImGui::End();
        return;
    }


    if (ImGui::BeginTabBar("MainTabBar")) {
        if (ImGui::BeginTabItem("Cheats")) {
            // Display currency values if attached
            if (hProc && modBase) {
                uint32_t cubitsVal = 0, recubesVal = 0;
                // currency values are at [ptr + cubitsOffset]. Read the pointer first, then the values.
                uint32_t basePtr = 0;
                if (ReadProcessMemory(hProc, (LPCVOID)(modBase + cubitsBaseOffset), &basePtr, sizeof(basePtr), nullptr) && basePtr) {
                    ReadProcessMemory(hProc, (LPCVOID)(basePtr + cubitsOffset), &cubitsVal, sizeof(cubitsVal), nullptr);
                    ReadProcessMemory(hProc, (LPCVOID)(basePtr + recubesOffset), &recubesVal, sizeof(recubesVal), nullptr);
                }
                else {
                    // Fallback: try absolute reads at module + base + offset (older layout or direct addresses)
                    uintptr_t addrCubits = modBase + cubitsBaseOffset + cubitsOffset;
                    ReadProcessMemory(hProc, (LPCVOID)addrCubits, &cubitsVal, sizeof(cubitsVal), nullptr);
                    uintptr_t addrRecubes = modBase + cubitsBaseOffset + recubesOffset;
                    ReadProcessMemory(hProc, (LPCVOID)addrRecubes, &recubesVal, sizeof(recubesVal), nullptr);
                }
                ImGui::TextColored(ImVec4(1.0f, 0.95f, 0.6f, 1.0f), "Cubits : %u", cubitsVal);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Recubes : %u", recubesVal);
            }
            ImGui::Checkbox("Noclip", &noclip);
            ImGui::SameLine();
            if (ImGui::SmallButton("Bind##1")) listeningFor = 1;
            ImGui::Text("Bound: %s", KeyToString(bindNoclip).c_str());

            ImGui::Checkbox("No Collision", &noCollision);
            ImGui::SameLine();
            if (ImGui::SmallButton("Bind##2")) listeningFor = 2;
            ImGui::Text("Bound: %s", KeyToString(bindNoColl).c_str());

            ImGui::Checkbox("Infinite Jump", &infJump);
            ImGui::SameLine();
            if (ImGui::SmallButton("Bind##3")) listeningFor = 3;
            ImGui::Text("Bound: %s", KeyToString(bindInfJump).c_str());


            if (ImGui::Checkbox("Hide Name (Client)", &hideName)) {
                uint8_t v = hideName ? 1u : 0u;
                if (hProc && playerPtr) WriteByteAt(hProc, playerPtr, hideNameOffset, v);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Bind##HideName")) listeningFor = 5;
            ImGui::Text("Bound: %s", KeyToString(bindHideName).c_str());

            ImGui::Separator();

            ImGui::Separator();


            if (hProc && playerPtr) {
                float currentX = 0.0f, currentZ = 0.0f, currentY = 0.0f;
                ReadFloatAt(hProc, playerPtr, posWestEastOffsets[0], currentX);
                ReadFloatAt(hProc, playerPtr, posNorthSouthOffsets[0], currentZ);
                ReadFloatAt(hProc, playerPtr, posHeightOffsets[0], currentY);

                if (lastPlayerPtrForPos != playerPtr) {
                    playerPosX = currentX;
                    playerPosZ = currentZ;
                    playerPosY = currentY;
                    // Initialize hideName from memory for the new player pointer
                    uint8_t hb = 0;
                    if (ReadByteAt(hProc, playerPtr, hideNameOffset, hb)) hideName = (hb != 0);
                    lastPlayerPtrForPos = playerPtr;
                }

                // Show the currently read X/Z/Y values (from the first offsets)
                ImGui::Text("Current X: %.3f", currentX);
                ImGui::Text("Current Z: %.3f", currentZ);
                ImGui::Text("Current Y: %.3f", currentY);

                ImGui::InputFloat("X (W/E)", &playerPosX, 0.1f, 1.0f, "%.3f");
                ImGui::SameLine();
                if (ImGui::SmallButton("Apply X")) {
                    size_t cnt = sizeof(posWestEastOffsets) / sizeof(posWestEastOffsets[0]);
                    for (size_t i = 0; i < cnt; ++i) {
                        uintptr_t off = posWestEastOffsets[i];
                        if (off) WriteFloatAt(hProc, playerPtr, off, playerPosX);
                    }
                    // Read back primary value to update display immediately
                    ReadFloatAt(hProc, playerPtr, posWestEastOffsets[0], currentX);
                    playerPosX = currentX;
                }

                ImGui::InputFloat("Z (N/S)", &playerPosZ, 0.1f, 1.0f, "%.3f");
                ImGui::SameLine();
                if (ImGui::SmallButton("Apply Z")) {
                    size_t cnt = sizeof(posNorthSouthOffsets) / sizeof(posNorthSouthOffsets[0]);
                    for (size_t i = 0; i < cnt; ++i) {
                        uintptr_t off = posNorthSouthOffsets[i];
                        if (off) WriteFloatAt(hProc, playerPtr, off, playerPosZ);
                    }
                    // Read back primary value to update display immediately
                    ReadFloatAt(hProc, playerPtr, posNorthSouthOffsets[0], currentZ);
                    playerPosZ = currentZ;
                }

                // Height (Y)
                ImGui::InputFloat("Y (U/D)", &playerPosY, 0.1f, 1.0f, "%.3f");
                ImGui::SameLine();
                if (ImGui::SmallButton("Apply Y")) {
                    size_t cnt = sizeof(posHeightOffsets) / sizeof(posHeightOffsets[0]);
                    for (size_t i = 0; i < cnt; ++i) {
                        uintptr_t off = posHeightOffsets[i];
                        if (off) WriteFloatAt(hProc, playerPtr, off, playerPosY);
                    }
                    ReadFloatAt(hProc, playerPtr, posHeightOffsets[0], currentY);
                    playerPosY = currentY;
                }

            }
            else {
                ImGui::Separator();
                ImGui::TextColored({ 0.8f, 0.5f, 0.5f, 1.0f }, "Game not attached");
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Accounts")) {
            static char userBuf[128]{}, passBuf[128]{};

            if (ImGui::BeginListBox("##accs", { 0, 140 })) {
                for (size_t i = 0; i < accounts.size(); ++i) {
                    bool sel = (selectedAcc == (int)i);
                    std::string disp = accounts[i].user.empty() ? "<empty>" : accounts[i].user;
                    if (ImGui::Selectable(disp.c_str(), sel)) {
                        selectedAcc = (int)i;
                        strncpy_s(userBuf, accounts[i].user.c_str(), _TRUNCATE);
                        strncpy_s(passBuf, accounts[i].pass.c_str(), _TRUNCATE);
                    }
                }
                ImGui::EndListBox();
            }

            ImGui::InputText("Username", userBuf, sizeof(userBuf));
            ImGui::InputText("Password", passBuf, sizeof(passBuf), ImGuiInputTextFlags_Password);

            if (ImGui::Button("Add")) {
                if (AddAccount(userBuf, passBuf)) {
                    LoadAccounts();
                    statusMsg = "Added.";
                    userBuf[0] = passBuf[0] = 0;
                }
                else statusMsg = "Failed (dup/empty)";
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete") && selectedAcc >= 0) {
                accounts.erase(accounts.begin() + selectedAcc);
                SaveAccounts();
                LoadAccounts();
                selectedAcc = -1;
                userBuf[0] = passBuf[0] = 0;
                statusMsg = "Deleted.";
            }
            ImGui::SameLine();
            if (ImGui::Button("Login")) {
                LoginSelected();
                statusMsg = "Login triggered.";
            }
            ImGui::TextColored({ 0.6f,1.0f,0.6f,1 }, "%s", statusMsg.c_str());
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Settings")) {
            ImGui::Text("Menu toggle:");
            ImGui::SameLine();
            if (ImGui::SmallButton("Bind##4")) listeningFor = 4;
            ImGui::Text("Bound: %s", KeyToString(bindToggleUI).c_str());
            if (ImGui::Button("Save settings")) SaveSettings();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    if (listeningFor) {
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize({ 320,0 });
        if (ImGui::Begin("Bind key", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
            const char* names[] = { "", "Noclip", "No Collision", "Inf Jump", "UI Toggle" };
            ImGui::Text("Press key for %s (ESC = cancel)", names[listeningFor]);
            ImGui::End();
        }
    }

    ImGui::End();
}

void ApplyCheats() {
    if (!hProc || !modBase) return;

    // Player cheats
    if (playerPtr) {
        uint32_t targetSize = noCollision ? PSZ_NOCOLL : (noclip ? PSZ_TINY : PSZ_DEFAULT);
        WriteU32(playerPtr, playerSizeOffset, targetSize);

        if (infJump) {
            WriteU32(playerPtr, jumpPotentialOffset, JUMP_INF);
        }
    }

}
