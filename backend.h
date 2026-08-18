#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "json.hpp"
#include "offsets.h"

using json = nlohmann::json;

struct KeyBind { int key = 0; bool ctrl = false, shift = false, alt = false; };
struct Account { std::string user, pass; };

static const uint32_t PSZ_DEFAULT = 1065353216u;
static const uint32_t PSZ_TINY = 985353216u;
static const uint32_t PSZ_NOCOLL = 0u;
static const uint32_t JUMP_INF = 6400u;

extern HANDLE hProc;
extern DWORD pid;
extern uintptr_t modBase;
extern uintptr_t playerPtr;

extern bool noclip;
extern bool noCollision;
extern bool infJump;
extern bool showMenu;
extern bool hideName;

extern KeyBind bindNoclip;
extern KeyBind bindNoColl;
extern KeyBind bindInfJump;
extern KeyBind bindToggleUI;
extern KeyBind bindHideName;
extern KeyBind bindFly;
extern int listeningFor;

extern bool flyEnabled;

extern float playerPosX;
extern float playerPosY;
extern float playerPosZ;
extern uintptr_t lastPlayerPtrForPos;

extern std::vector<Account> accounts;
extern int selectedAcc;
extern std::string statusMsg;

extern bool gameSpeedFreeze;
extern float gameSpeedValue;

DWORD GetProcId(const std::wstring& name);
uintptr_t GetModuleBase(DWORD pid, const std::wstring& modName);
bool Attach();
void Detach();
void TryAttachOrRefresh();

bool ReadU32(uintptr_t base, uintptr_t off, uint32_t& v);
bool WriteU32(uintptr_t base, uintptr_t off, uint32_t v);

std::string KeyToString(const KeyBind& b);
bool IsPressed(const KeyBind& b);
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

void ApplyCheats();
