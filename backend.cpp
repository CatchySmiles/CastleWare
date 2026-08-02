#include "backend.h"
#include <windows.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <shellapi.h>
#include <fstream>
#include <cstring>

HANDLE hProc = nullptr;
DWORD pid = 0;
uintptr_t modBase = 0;
uintptr_t playerPtr = 0;

bool noclip = false;
bool noCollision = false;
bool infJump = false;
bool showMenu = true;
bool hideName = false;
bool flyEnabled = false;
bool gameSpeedFreeze = false;
float gameSpeedValue = 100.0f;

KeyBind bindNoclip;
KeyBind bindNoColl;
KeyBind bindInfJump;
KeyBind bindToggleUI{ VK_INSERT };
KeyBind bindHideName;
KeyBind bindFly;
int listeningFor = 0;

float playerPosX = 0.0f;
float playerPosY = 0.0f;
float playerPosZ = 0.0f;
uintptr_t lastPlayerPtrForPos = 0;

std::vector<Account> accounts;
int selectedAcc = -1;
std::string statusMsg = "Ready";

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

uintptr_t GetModuleBase(DWORD pid, const std::wstring& modName) {
    MODULEENTRY32W me{ sizeof(me) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    uintptr_t base = 0;
    if (Module32FirstW(snap, &me)) {
        do {
            if (modName == me.szModule) { base = (uintptr_t)me.modBaseAddr; break; }
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

static bool IsModifier(int vk) {
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
        if (GetAsyncKeyState(VK_ESCAPE) & 1) { listeningFor = 0; return; }
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
                    else if (listeningFor == 6) bindFly = newBind;
                listeningFor = 0;
                return;
            }
        }
        return;
    }

    static bool prevState[6] = { false, false, false, false, false, false };
    bool now[6] = { IsPressed(bindNoclip), IsPressed(bindNoColl), IsPressed(bindInfJump), IsPressed(bindToggleUI), IsPressed(bindHideName), IsPressed(bindFly) };

    if (now[0] && !prevState[0]) noclip = !noclip;
    if (now[1] && !prevState[1]) noCollision = !noCollision;
    if (now[2] && !prevState[2]) infJump = !infJump;
    if (now[3] && !prevState[3]) showMenu = !showMenu;
    if (now[4] && !prevState[4]) {
        hideName = !hideName;
        uint8_t v = hideName ? 1u : 0u;
        WriteByteAt(hProc, playerPtr, hideNameOffset, v);
    }
    if (now[5] && !prevState[5]) flyEnabled = !flyEnabled;

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

void ApplyCheats() {
    if (!hProc || !modBase) return;
    if (playerPtr) {
        uint32_t targetSize = noCollision ? PSZ_NOCOLL : (noclip ? PSZ_TINY : PSZ_DEFAULT);
        WriteU32(playerPtr, playerSizeOffset, targetSize);
        if (infJump) WriteU32(playerPtr, jumpPotentialOffset, JUMP_INF);
    }
    // Game speed freeze: read pointer at module + base offset, then write float at pointer + inner offset
    if (gameSpeedFreeze) {
        uint32_t p = 0;
        if (ReadU32(modBase, gameSpeedPointerBaseOffset, p) && p) {
            WriteFloatAt(hProc, p, gameSpeedPointerInnerOffset, gameSpeedValue);
        }
    }
}
