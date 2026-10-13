#include <windows.h>
#include <iostream>
#include <queue>
#include <vector>
#include "MinHook.h"
#include <SDL3/SDL.h>

#pragma comment(lib, "libMinHook.x64.lib")

// --- FORWARD EXPORTS FOR VERSION.DLL ---
#pragma comment(linker, "/export:GetFileVersionInfoA=C:\\Windows\\System32\\version.GetFileVersionInfoA")
#pragma comment(linker, "/export:GetFileVersionInfoByHandle=C:\\Windows\\System32\\version.GetFileVersionInfoByHandle")
#pragma comment(linker, "/export:GetFileVersionInfoExA=C:\\Windows\\System32\\version.GetFileVersionInfoExA")
#pragma comment(linker, "/export:GetFileVersionInfoExW=C:\\Windows\\System32\\version.GetFileVersionInfoExW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeA=C:\\Windows\\System32\\version.GetFileVersionInfoSizeA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExA=C:\\Windows\\System32\\version.GetFileVersionInfoSizeExA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExW=C:\\Windows\\System32\\version.GetFileVersionInfoSizeExW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeW=C:\\Windows\\System32\\version.GetFileVersionInfoSizeW")
#pragma comment(linker, "/export:GetFileVersionInfoW=C:\\Windows\\System32\\version.GetFileVersionInfoW")
#pragma comment(linker, "/export:VerFindFileA=C:\\Windows\\System32\\version.VerFindFileA")
#pragma comment(linker, "/export:VerFindFileW=C:\\Windows\\System32\\version.VerFindFileW")
#pragma comment(linker, "/export:VerInstallFileA=C:\\Windows\\System32\\version.VerInstallFileA")
#pragma comment(linker, "/export:VerInstallFileW=C:\\Windows\\System32\\version.VerInstallFileW")
#pragma comment(linker, "/export:VerLanguageNameA=C:\\Windows\\System32\\version.VerLanguageNameA")
#pragma comment(linker, "/export:VerLanguageNameW=C:\\Windows\\System32\\version.VerLanguageNameW")
#pragma comment(linker, "/export:VerQueryValueA=C:\\Windows\\System32\\version.VerQueryValueA")
#pragma comment(linker, "/export:VerQueryValueW=C:\\Windows\\System32\\version.VerQueryValueW")

typedef bool(SDLCALL* pfn_SDL_PollEvent)(SDL_Event* event);
static pfn_SDL_PollEvent original_SDL_PollEvent = nullptr;

static std::queue<SDL_Event> g_syntheticEvents;


static float g_layoutCenterX = 640.0f; // Center of the standard 1280x720 canvas
static float g_choiceFirstY = 430.0f; // Center coordinate of the first option
static float g_choiceStepY = 50.0f;  // Distance between consecutive option centers
static int   g_choiceMaxSlots = 5;     // Maximum number of choice slots (choice_0 -> choice_4)
static float g_continueBtnY = 675.0f; // Y coordinate of the Continue button at the bottom

struct ButtonPosition {
    float x;
    float y;
};

static std::vector<ButtonPosition> g_allButtons;

void RebuildButtonLayout() {
    g_allButtons.clear();

    // 1. Generate coordinates for the choice slots (choice_0, choice_1, ...)
    for (int i = 0; i < g_choiceMaxSlots; ++i) {
        float y = g_choiceFirstY + (i * g_choiceStepY);
        g_allButtons.push_back({ g_layoutCenterX, y });
    }

    // 2. The Continue button is always at the end of the list
    g_allButtons.push_back({ g_layoutCenterX, g_continueBtnY });

    std::cout << "[GamepadProxy] Button layout rebuilt (" << g_allButtons.size() << " buttons):\n";
    for (size_t i = 0; i < g_allButtons.size(); ++i) {
        if (i < (size_t)g_choiceMaxSlots) {
            std::cout << "  - choice_" << i << ": Y = " << g_allButtons[i].y << "\n";
        }
        else {
            std::cout << "  - continue_btn: Y = " << g_allButtons[i].y << "\n";
        }
    }
}

static int g_selectedIndex = -1;
static SDL_Gamepad* g_gamepad = nullptr;

static float g_windowWidth = 1280.0f;
static float g_windowHeight = 720.0f;

ButtonPosition GetScaledCoord(ButtonPosition pos) {
    float scaleX = g_windowWidth / 1280.0f;
    float scaleY = g_windowHeight / 720.0f;
    return { pos.x * scaleX, pos.y * scaleY };
}

void SynthesizeMouseMove(float x, float y) {
    SDL_Event ev = {};
    ev.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.x = x;
    ev.motion.y = y;
    g_syntheticEvents.push(ev);

    std::cout << "[GamepadProxy] Move Cursor -> (" << x << ", " << y << ") [Window: "
        << (int)g_windowWidth << "x" << (int)g_windowHeight << "]\n";
}

void SynthesizeMouseClick(float x, float y) {
    SDL_Event down = {};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.button = SDL_BUTTON_LEFT;
    down.button.down = true;
    down.button.x = x;
    down.button.y = y;
    g_syntheticEvents.push(down);

    SDL_Event up = down;
    up.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.down = false;
    g_syntheticEvents.push(up);
    std::cout << "[GamepadProxy] Click at -> (" << x << ", " << y << ")\n";
}

void PollGamepadInput() {
    SDL_UpdateGamepads();

    if (!g_gamepad) {
        int count = 0;
        SDL_JoystickID* joysticks = SDL_GetGamepads(&count);
        if (count > 0 && joysticks) {
            g_gamepad = SDL_OpenGamepad(joysticks[0]);
            if (g_gamepad) {
                std::cout << "[GamepadProxy] Gamepad connected: "
                    << SDL_GetGamepadName(g_gamepad) << "\n";
            }
        }
    }
    if (!g_gamepad) return;

    static bool prevDpadUp = false;
    static bool prevDpadDown = false;
    static bool prevBtnA = false;
    static bool prevBtnB = false;
    static bool prevBtnStart = false;

    bool curDpadUp = SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
    bool curDpadDown = SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    bool curBtnA = SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
    bool curBtnB = SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_EAST);
    bool curBtnStart = SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_START);

    int totalButtons = (int)g_allButtons.size();

    // D-Pad Down:
    if (curDpadDown && !prevDpadDown) {
        if (g_selectedIndex < 0) {
            g_selectedIndex = 0;
        }
        else {
            g_selectedIndex = (g_selectedIndex + 1) % totalButtons;
        }
        auto target = GetScaledCoord(g_allButtons[g_selectedIndex]);
        SynthesizeMouseMove(target.x, target.y);
    }

    // D-Pad Up:
    if (curDpadUp && !prevDpadUp) {
        if (g_selectedIndex < 0) {
            g_selectedIndex = totalButtons - 1;
        }
        else {
            g_selectedIndex = (g_selectedIndex - 1 + totalButtons) % totalButtons;
        }
        auto target = GetScaledCoord(g_allButtons[g_selectedIndex]);
        SynthesizeMouseMove(target.x, target.y);
    }

    // B button: Move focus to continue_btn
    if (curBtnB && !prevBtnB) {
        g_selectedIndex = -1;
        auto target = GetScaledCoord(g_allButtons.back());
        SynthesizeMouseMove(target.x, target.y);
        std::cout << "[GamepadProxy] Focus reset to continue_btn.\n";
    }

    // A button: Confirm the selection
    if (curBtnA && !prevBtnA) {
        if (g_selectedIndex >= 0 && g_selectedIndex < totalButtons) {
            auto target = GetScaledCoord(g_allButtons[g_selectedIndex]);
            SynthesizeMouseClick(target.x, target.y);
            g_selectedIndex = -1;
        }
        else {
            auto target = GetScaledCoord(g_allButtons.back());
            SynthesizeMouseClick(target.x, target.y);
        }
    }

    // Start button: Open/close the pause menu
    if (curBtnStart && !prevBtnStart) {
        SDL_Event escDown = {};
        escDown.type = SDL_EVENT_KEY_DOWN;
        escDown.key.key = SDLK_ESCAPE;
        escDown.key.down = true;
        g_syntheticEvents.push(escDown);

        SDL_Event escUp = escDown;
        escUp.type = SDL_EVENT_KEY_UP;
        escUp.key.down = false;
        g_syntheticEvents.push(escUp);
    }

    prevDpadUp = curDpadUp;
    prevDpadDown = curDpadDown;
    prevBtnA = curBtnA;
    prevBtnB = curBtnB;
    prevBtnStart = curBtnStart;
}

bool SDLCALL Detour_SDL_PollEvent(SDL_Event* event) {
    PollGamepadInput();

    if (!g_syntheticEvents.empty()) {
        if (event) {
            *event = g_syntheticEvents.front();
        }
        g_syntheticEvents.pop();
        return true;
    }

    if (original_SDL_PollEvent) {
        bool res = original_SDL_PollEvent(event);
        if (res && event) {
            if (event->type == SDL_EVENT_WINDOW_RESIZED || event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                g_windowWidth = (float)event->window.data1;
                g_windowHeight = (float)event->window.data2;
                std::cout << "[GamepadProxy] Window Size Changed -> "
                    << (int)g_windowWidth << "x" << (int)g_windowHeight << "\n";
            }
        }
        return res;
    }
    return false;
}

DWORD WINAPI InitHookThread(LPVOID) {
    std::cout << "[GamepadProxy] Injected into process successfully!\n";

    RebuildButtonLayout();

    HMODULE hSDL = nullptr;
    while (!hSDL) {
        hSDL = GetModuleHandleA("SDL3.dll");
        Sleep(50);
    }

    SDL_InitSubSystem(SDL_INIT_GAMEPAD);

    if (MH_Initialize() == MH_OK) {
        void* pPollEvent = (void*)GetProcAddress(hSDL, "SDL_PollEvent");
        if (pPollEvent) {
            if (MH_CreateHook(pPollEvent, &Detour_SDL_PollEvent, reinterpret_cast<LPVOID*>(&original_SDL_PollEvent)) == MH_OK) {
                MH_EnableHook(pPollEvent);
                std::cout << "[GamepadProxy] SDL_PollEvent HOOKED SUCCESSFULLY!\n";
            }
        }
    }

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, InitHookThread, nullptr, 0, nullptr);
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        MH_Uninitialize();
    }
    return TRUE;
}