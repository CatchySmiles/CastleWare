#include "frontend.h"
#include "imgui.h"
#include <cstring>
#include <vector>
#include <string>
#include <cstdio>
#include <cmath>
#include "backend.h"

void RenderInterface() {
    ImGui::SetNextWindowSize({ 420, 0 }, ImGuiCond_FirstUseEver);
    ImGuiWindowFlags winFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;
    if (!ImGui::Begin("CastleWare", nullptr, winFlags)) { ImGui::End(); return; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wpos = ImGui::GetWindowPos();
    ImVec2 wsize = ImGui::GetWindowSize();
    const float topBarH = 36.0f;
    ImU32 topBg = ImGui::GetColorU32(ImGui::GetStyle().Colors[ImGuiCol_TitleBgActive]);
    ImU32 accentCol = ImGui::GetColorU32(ImVec4(0.78f, 0.95f, 0.78f, 1.0f));
    dl->AddRectFilled(wpos, ImVec2(wpos.x + wsize.x, wpos.y + topBarH), topBg, ImGui::GetStyle().WindowRounding);
    dl->AddText(ImVec2(wpos.x + 14, wpos.y + 8), accentCol, "CastleWare");
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + topBarH - 8);

    ImGuiTabBarFlags tabFlags = ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyResizeDown;
    if (ImGui::BeginTabBar("MainTabBar", tabFlags)) {
        if (ImGui::BeginTabItem("Cheats")) {
            if (hProc && modBase) {
                uint32_t cubitsVal = 0, recubesVal = 0;
                uint32_t basePtr = 0;
                if (ReadProcessMemory(hProc, (LPCVOID)(modBase + cubitsBaseOffset), &basePtr, sizeof(basePtr), nullptr) && basePtr) {
                    ReadProcessMemory(hProc, (LPCVOID)(basePtr + cubitsOffset), &cubitsVal, sizeof(cubitsVal), nullptr);
                    ReadProcessMemory(hProc, (LPCVOID)(basePtr + recubesOffset), &recubesVal, sizeof(recubesVal), nullptr);
                }
                else {
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

            if (ImGui::Checkbox("Hide Name (Client Side)", &hideName)) {
                uint8_t v = hideName ? 1u : 0u;
                if (hProc && playerPtr) WriteByteAt(hProc, playerPtr, hideNameOffset, v);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Bind##HideName")) listeningFor = 5;
            ImGui::Text("Bound: %s", KeyToString(bindHideName).c_str());

            ImGui::Separator();
            bool gsChanged = ImGui::Checkbox("Tick Speed", &gameSpeedFreeze);
            if (gsChanged && !gameSpeedFreeze) {
                gameSpeedValue = 100.0f;
                if (hProc) {
                    uint32_t p = 0;
                    if (ReadU32(modBase, gameSpeedPointerBaseOffset, p) && p) WriteFloatAt(hProc, p, gameSpeedPointerInnerOffset, gameSpeedValue);
                }
            }
            ImGui::SameLine();
            ImGui::PushItemWidth(180);
            if (ImGui::SliderFloat("##GameSpeed", &gameSpeedValue, 1.0f, 500.0f, "%.1f")) {
                if (gameSpeedFreeze) {
                    uint32_t p = 0;
                    if (ReadU32(modBase, gameSpeedPointerBaseOffset, p) && p) WriteFloatAt(hProc, p, gameSpeedPointerInnerOffset, gameSpeedValue);
                }
            }
            ImGui::PopItemWidth(); ImGui::SameLine();
            if (ImGui::Button("Reset")) { gameSpeedValue = 100.0f; if (gameSpeedFreeze) { uint32_t p = 0; if (ReadU32(modBase, gameSpeedPointerBaseOffset, p) && p) WriteFloatAt(hProc, p, gameSpeedPointerInnerOffset, gameSpeedValue); } }

            ImGui::Separator();

            if (hProc && playerPtr) {
                float currentX = 0.0f, currentZ = 0.0f, currentY = 0.0f;
                ReadFloatAt(hProc, playerPtr, posWestEastOffsets[0], currentX);
                ReadFloatAt(hProc, playerPtr, posNorthSouthOffsets[0], currentZ);
                ReadFloatAt(hProc, playerPtr, posHeightOffsets[0], currentY);
                if (lastPlayerPtrForPos != playerPtr) { playerPosX = currentX; playerPosZ = currentZ; playerPosY = currentY; lastPlayerPtrForPos = playerPtr; }
                static float flySpeed = 700.0f;
                static bool prevFly = false;
                static float flyBaseX = 0.0f, flyBaseZ = 0.0f, flyBaseY = 0.0f;
                static float flyOffX = 0.0f, flyOffZ = 0.0f, flyOffY = 0.0f;
                static float flyStartupTimer = 0.0f;
                static const float flyStartupDelay = 0.15f; // Otherwise you teleport into the void because it keeps your position between realms
                static uintptr_t lastPlayerPtrForFly = 0;
                ImGui::Checkbox("Fly", &flyEnabled); ImGui::SameLine(); ImGui::InputFloat("Fly Speed", &flySpeed, 0.1f, 1.0f, "%.2f");
                if (ImGui::SmallButton("Bind##Fly")) listeningFor = 6;
                ImGui::SameLine(); ImGui::Text("Bound: %s", KeyToString(bindFly).c_str());
                if (flyEnabled && !prevFly) {
                    float rx = currentX, rz = currentZ, ry = currentY;
                    if (hProc && playerPtr) {
                        ReadFloatAt(hProc, playerPtr, posWestEastOffsets[0], rx);
                        ReadFloatAt(hProc, playerPtr, posNorthSouthOffsets[0], rz);
                        ReadFloatAt(hProc, playerPtr, posHeightOffsets[0], ry);
                    }
                    flyBaseX = rx; flyBaseZ = rz; flyBaseY = ry;
                    flyOffX = flyOffZ = flyOffY = 0.0f;
                    // reset startup timer so we don't immediately write unstable positions
                    flyStartupTimer = 0.0f;
                    // track which player pointer these base coordinates belong to
                    lastPlayerPtrForFly = playerPtr;
                }
                float effX = flyEnabled ? (flyBaseX + flyOffX) : currentX;
                float effZ = flyEnabled ? (flyBaseZ + flyOffZ) : currentZ;
                float effY = flyEnabled ? (flyBaseY + flyOffY) : currentY;
                if (flyEnabled) {
                    float dt = ImGui::GetIO().DeltaTime;
                    // advance startup timer; we wait a short moment to avoid teleporting
                    flyStartupTimer += dt;
                    float step = flySpeed * dt;
                    float ddx = 0.0f, ddz = 0.0f, ddy = 0.0f;
                    if (GetAsyncKeyState('W') & 0x8000) ddz -= step;
                    if (GetAsyncKeyState('S') & 0x8000) ddz += step;
                    if (GetAsyncKeyState('A') & 0x8000) ddx -= step;
                    if (GetAsyncKeyState('D') & 0x8000) ddx += step;
                    if (GetAsyncKeyState(VK_SPACE) & 0x8000) ddy -= step;
                    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) ddy += step;

                    // Normalize horizontal movement so diagonal isn't faster than straight movement
                    float horizLen = std::sqrt(ddx*ddx + ddz*ddz);
                    if (horizLen > 0.0f) {
                        float maxHoriz = step;
                        if (horizLen > maxHoriz) {
                            float s = maxHoriz / horizLen;
                            ddx *= s; ddz *= s;
                        }
                    }

                    // Clamp per-frame increments to avoid runaway values (safety)
                    const float MAX_STEP = 20000.0f; // very large but prevents NaN/infinite growth
                    if (ddx > MAX_STEP) ddx = MAX_STEP; else if (ddx < -MAX_STEP) ddx = -MAX_STEP;
                    if (ddz > MAX_STEP) ddz = MAX_STEP; else if (ddz < -MAX_STEP) ddz = -MAX_STEP;
                    if (ddy > MAX_STEP) ddy = MAX_STEP; else if (ddy < -MAX_STEP) ddy = -MAX_STEP;

                    // If the player pointer changed while flying, resync base to avoid huge jumps
                    if (playerPtr != lastPlayerPtrForFly) {
                        float curX = 0.0f, curZ = 0.0f, curY = 0.0f;
                        if (ReadFloatAt(hProc, playerPtr, posWestEastOffsets[0], curX) &&
                            ReadFloatAt(hProc, playerPtr, posNorthSouthOffsets[0], curZ) &&
                            ReadFloatAt(hProc, playerPtr, posHeightOffsets[0], curY)) {
                            flyBaseX = curX; flyBaseZ = curZ; flyBaseY = curY;
                            flyOffX = flyOffZ = flyOffY = 0.0f;
                            lastPlayerPtrForFly = playerPtr;
                        }
                        flyStartupTimer = 0.0f; // give a fresh delay after resync
                    }

                    if (flyStartupTimer >= flyStartupDelay) {
                        if (ddx != 0.0f || ddz != 0.0f || ddy != 0.0f) {
                            flyOffX += ddx; flyOffZ += ddz; flyOffY += ddy;
                        }
                        float nx = flyBaseX + flyOffX;
                        float nz = flyBaseZ + flyOffZ;
                        float ny = flyBaseY + flyOffY;
                        // sanity checks: avoid writing NaN/inf or extremely large values
                        if (!std::isfinite(nx) || !std::isfinite(nz) || !std::isfinite(ny)) {
                            // abandon this write and resync next frame
                            flyOffX = flyOffZ = flyOffY = 0.0f;
                            flyStartupTimer = 0.0f;
                        } else {
                            bool allOk = true;
                        size_t cnt = sizeof(posWestEastOffsets) / sizeof(posWestEastOffsets[0]);
                            for (size_t j = 0; j < cnt; ++j) {
                                uintptr_t off = posWestEastOffsets[j];
                                if (off) { if (!WriteFloatAt(hProc, playerPtr, off, nx)) { allOk = false; break; } }
                            }
                            if (allOk) {
                                cnt = sizeof(posNorthSouthOffsets) / sizeof(posNorthSouthOffsets[0]);
                                for (size_t j = 0; j < cnt; ++j) {
                                    uintptr_t off = posNorthSouthOffsets[j];
                                    if (off) { if (!WriteFloatAt(hProc, playerPtr, off, nz)) { allOk = false; break; } }
                                }
                            }
                            if (allOk) {
                                cnt = sizeof(posHeightOffsets) / sizeof(posHeightOffsets[0]);
                                for (size_t j = 0; j < cnt; ++j) {
                                    uintptr_t off = posHeightOffsets[j];
                                    if (off) { if (!WriteFloatAt(hProc, playerPtr, off, ny)) { allOk = false; break; } }
                                }
                            }
                            if (allOk) {
                                playerPosX = nx; playerPosZ = nz; playerPosY = ny;
                            } else {
                                // failed to write (process might have changed); resync next frame
                                flyStartupTimer = 0.0f;
                                flyOffX = flyOffZ = flyOffY = 0.0f;
                            }
                        }
                    }
                    else {
                        // During startup delay, keep base synced to current game position to avoid jumps.
                        float curX = 0.0f, curZ = 0.0f, curY = 0.0f;
                        ReadFloatAt(hProc, playerPtr, posWestEastOffsets[0], curX);
                        ReadFloatAt(hProc, playerPtr, posNorthSouthOffsets[0], curZ);
                        ReadFloatAt(hProc, playerPtr, posHeightOffsets[0], curY);
                        flyBaseX = curX; flyBaseZ = curZ; flyBaseY = curY;
                        flyOffX = flyOffZ = flyOffY = 0.0f;
                        playerPosX = curX; playerPosZ = curZ; playerPosY = curY;
                    }
                }
                prevFly = flyEnabled;
            }
            else { ImGui::TextColored({ 0.8f, 0.5f, 0.5f, 1.0f }, "Game not attached"); }

            ImGui::EndTabItem();

        }

        if (ImGui::BeginTabItem("World")) {
            if (hProc && playerPtr) {
                float currentX = 0.0f, currentZ = 0.0f, currentY = 0.0f;
                ReadFloatAt(hProc, playerPtr, posWestEastOffsets[0], currentX);
                ReadFloatAt(hProc, playerPtr, posNorthSouthOffsets[0], currentZ);
                ReadFloatAt(hProc, playerPtr, posHeightOffsets[0], currentY);

                if (lastPlayerPtrForPos != playerPtr) {
                    playerPosX = currentX; playerPosZ = currentZ; playerPosY = currentY;
                    uint8_t hb = 0; if (ReadByteAt(hProc, playerPtr, hideNameOffset, hb)) hideName = (hb != 0);
                    lastPlayerPtrForPos = playerPtr;
                }

                ImGui::AlignTextToFramePadding(); ImGui::Text("X:"); ImGui::SameLine();
                char curXLabel[64]; std::snprintf(curXLabel, sizeof(curXLabel), "%.3f##curX", currentX);
                if (ImGui::SmallButton(curXLabel)) { playerPosX = currentX; }
                ImGui::SameLine(); ImGui::PushItemWidth(140);
                ImGui::InputFloat("##XInput", &playerPosX, 0.1f, 1.0f, "%.3f"); ImGui::PopItemWidth(); ImGui::SameLine();
                if (ImGui::SmallButton("Apply X")) {
                    size_t cnt = sizeof(posWestEastOffsets) / sizeof(posWestEastOffsets[0]);
                    for (size_t i = 0; i < cnt; ++i) { uintptr_t off = posWestEastOffsets[i]; if (off) WriteFloatAt(hProc, playerPtr, off, playerPosX); }
                    ReadFloatAt(hProc, playerPtr, posWestEastOffsets[0], currentX); playerPosX = currentX;
                }

                ImGui::AlignTextToFramePadding(); ImGui::Text("Z:"); ImGui::SameLine();
                char curZLabel[64]; std::snprintf(curZLabel, sizeof(curZLabel), "%.3f##curZ", currentZ);
                if (ImGui::SmallButton(curZLabel)) { playerPosZ = currentZ; }
                ImGui::SameLine(); ImGui::PushItemWidth(140);
                ImGui::InputFloat("##ZInput", &playerPosZ, 0.1f, 1.0f, "%.3f"); ImGui::PopItemWidth(); ImGui::SameLine();
                if (ImGui::SmallButton("Apply Z")) {
                    size_t cnt = sizeof(posNorthSouthOffsets) / sizeof(posNorthSouthOffsets[0]);
                    for (size_t i = 0; i < cnt; ++i) { uintptr_t off = posNorthSouthOffsets[i]; if (off) WriteFloatAt(hProc, playerPtr, off, playerPosZ); }
                    ReadFloatAt(hProc, playerPtr, posNorthSouthOffsets[0], currentZ); playerPosZ = currentZ;
                }

                ImGui::AlignTextToFramePadding(); ImGui::Text("Y:"); ImGui::SameLine();
                char curYLabel[64]; std::snprintf(curYLabel, sizeof(curYLabel), "%.3f##curY", currentY);
                if (ImGui::SmallButton(curYLabel)) { playerPosY = currentY; }
                ImGui::SameLine(); ImGui::PushItemWidth(140);
                ImGui::InputFloat("##YInput", &playerPosY, 0.1f, 1.0f, "%.3f"); ImGui::PopItemWidth(); ImGui::SameLine();
                if (ImGui::SmallButton("Apply Y")) {
                    size_t cnt = sizeof(posHeightOffsets) / sizeof(posHeightOffsets[0]);
                    for (size_t i = 0; i < cnt; ++i) { uintptr_t off = posHeightOffsets[i]; if (off) WriteFloatAt(hProc, playerPtr, off, playerPosY); }
                    ReadFloatAt(hProc, playerPtr, posHeightOffsets[0], currentY); playerPosY = currentY;
                }

                static bool orbitEnabled = false;
                static float orbitRadius = 2.0f;
                static float orbitSpeed = 1.0f;
                static float orbitVertAmp = 0.5f;
                static float orbitAngle = 0.0f;
                static int orbitDir = 1;
                static uintptr_t lastPlayerPtrForOrbit = 0;
                ImGui::Separator();
                ImGui::InputFloat("Radius", &orbitRadius, 0.1f, 1.0f, "%.2f"); ImGui::SameLine();
                ImGui::InputFloat("Speed", &orbitSpeed, 0.1f, 1.0f, "%.2f"); ImGui::SameLine();
                ImGui::InputFloat("VertAmp", &orbitVertAmp, 0.1f, 1.0f, "%.2f");
                ImGui::RadioButton("Add", &orbitDir, 1); ImGui::SameLine(); ImGui::RadioButton("Subtract", &orbitDir, -1);
                if (ImGui::Button(orbitEnabled ? "Stop Orbit" : "Start Orbit")) orbitEnabled = !orbitEnabled;
                if (orbitEnabled) {
                    float dt = ImGui::GetIO().DeltaTime;
                    orbitAngle += orbitSpeed * dt * (orbitDir == 1 ? 1.0f : -1.0f);
                    float nx = currentX + std::cos(orbitAngle) * orbitRadius;
                    float nz = currentZ + std::sin(orbitAngle) * orbitRadius;
                    float ny = currentY + std::sin(orbitAngle * 2.0f) * orbitVertAmp;

                    // resync if player pointer changed while orbiting
                    if (playerPtr != lastPlayerPtrForOrbit) {
                        lastPlayerPtrForOrbit = playerPtr;
                        orbitAngle = 0.0f; // restart angle so orbit isn't offset unpredictably
                    }

                    // sanity checks - only proceed if values are finite
                    if (std::isfinite(nx) && std::isfinite(nz) && std::isfinite(ny)) {
                        bool allOk = true;
                    size_t cnt = sizeof(posWestEastOffsets) / sizeof(posWestEastOffsets[0]);
                    for (size_t j = 0; j < cnt; ++j) {
                        uintptr_t off = posWestEastOffsets[j];
                        if (off) { if (!WriteFloatAt(hProc, playerPtr, off, nx)) { allOk = false; break; } }
                    }
                    if (allOk) {
                        cnt = sizeof(posNorthSouthOffsets) / sizeof(posNorthSouthOffsets[0]);
                        for (size_t j = 0; j < cnt; ++j) {
                            uintptr_t off = posNorthSouthOffsets[j];
                            if (off) { if (!WriteFloatAt(hProc, playerPtr, off, nz)) { allOk = false; break; } }
                        }
                    }
                        if (allOk) {
                            cnt = sizeof(posHeightOffsets) / sizeof(posHeightOffsets[0]);
                            for (size_t j = 0; j < cnt; ++j) {
                                uintptr_t off = posHeightOffsets[j];
                                if (off) { if (!WriteFloatAt(hProc, playerPtr, off, ny)) { allOk = false; break; } }
                            }
                        }
                        if (allOk) {
                            playerPosX = nx; playerPosZ = nz; playerPosY = ny;
                        } else {
                            // if writes failed, stop orbit briefly and resync next frame
                            lastPlayerPtrForOrbit = 0;
                        }
                    }
                }



                // Saved locations UI
                ImGui::Separator();
                struct SavedLocation { std::string name; float x, z, y; };
                static std::vector<SavedLocation> savedLocations;
                static char saveNameBuf[64] = {};
                ImGui::Text("Saved Locations:");
                ImGui::InputText("Name", saveNameBuf, sizeof(saveNameBuf)); ImGui::SameLine();
                if (ImGui::Button("Save")) {
                    if (saveNameBuf[0] != '\0') {
                        savedLocations.push_back({ std::string(saveNameBuf), currentX, currentZ, currentY });
                        saveNameBuf[0] = '\0';
                    }
                }

                for (int i = 0; i < (int)savedLocations.size(); ++i) {
                    auto &loc = savedLocations[i];
                    ImGui::Text("%s: %.3f, %.3f, %.3f", loc.name.c_str(), loc.x, loc.z, loc.y);
                    ImGui::SameLine();
                    char telLabel[32]; std::snprintf(telLabel, sizeof(telLabel), "Teleport##%d", i);
                    if (ImGui::SmallButton(telLabel)) {
                        size_t cnt = sizeof(posWestEastOffsets) / sizeof(posWestEastOffsets[0]);
                        for (size_t j = 0; j < cnt; ++j) { uintptr_t off = posWestEastOffsets[j]; if (off) WriteFloatAt(hProc, playerPtr, off, loc.x); }
                        cnt = sizeof(posNorthSouthOffsets) / sizeof(posNorthSouthOffsets[0]);
                        for (size_t j = 0; j < cnt; ++j) { uintptr_t off = posNorthSouthOffsets[j]; if (off) WriteFloatAt(hProc, playerPtr, off, loc.z); }
                        cnt = sizeof(posHeightOffsets) / sizeof(posHeightOffsets[0]);
                        for (size_t j = 0; j < cnt; ++j) { uintptr_t off = posHeightOffsets[j]; if (off) WriteFloatAt(hProc, playerPtr, off, loc.y); }
                    }
                    ImGui::SameLine();
                    char delLabel[32]; std::snprintf(delLabel, sizeof(delLabel), "Delete##%d", i);
                    if (ImGui::SmallButton(delLabel)) { savedLocations.erase(savedLocations.begin() + i); --i; }
                }
            }
            else { ImGui::TextColored({ 0.8f, 0.5f, 0.5f, 1.0f }, "Game not attached"); }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Accounts")) {
            static char userBuf[128] = {}, passBuf[128] = {};
            ImGui::InputText("Username", userBuf, sizeof(userBuf));
            ImGui::InputText("Password", passBuf, sizeof(passBuf), ImGuiInputTextFlags_Password);
            if (ImGui::Button("Add")) { if (AddAccount(userBuf, passBuf)) { strcpy_s(userBuf, ""); strcpy_s(passBuf, ""); LoadAccounts(); } }
            ImGui::Separator();
            for (int i = 0; i < (int)accounts.size(); ++i) {
                ImGui::PushID(i);
                ImGui::Text("%s", accounts[i].user.c_str()); ImGui::SameLine();
                if (ImGui::SmallButton("Select")) selectedAcc = i;
                ImGui::SameLine(); if (ImGui::SmallButton("Delete")) { accounts.erase(accounts.begin() + i); SaveAccounts(); LoadAccounts(); }
                ImGui::PopID();
            }
            ImGui::Separator();
            if (ImGui::Button("Login")) LoginSelected();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Settings")) {
            ImGui::Checkbox("Show UI", &showMenu); ImGui::SameLine(); ImGui::Text("Bound: %s", KeyToString(bindToggleUI).c_str());
            ImGui::Separator();
            if (ImGui::Button("Save")) SaveSettings();
            ImGui::SameLine(); if (ImGui::Button("Load")) LoadSettings();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    if (listeningFor) {
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize({ 320,0 });
        if (ImGui::Begin("Bind key", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
            const char* names[] = { "", "Noclip", "No Collision", "Inf Jump", "UI Toggle", "Hide Name", "Fly" };
            const char* which = "";
            if (listeningFor >= 0 && listeningFor < (int)(sizeof(names) / sizeof(names[0]))) which = names[listeningFor];
            ImGui::Text("Press key for %s (ESC = cancel)", which);
            ImGui::End();
        }
    }

    ImGui::End();
}
