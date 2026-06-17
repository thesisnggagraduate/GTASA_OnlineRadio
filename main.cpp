#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <sys/time.h>
#include <ctime>
#include <cmath>
#include <dlfcn.h>

#include <Events.h>
#include <engine/Font.h>
#include <engine/RsGlobal.h>
#include <base/Timer.h>
#include <entity/PlayerPed.h>

#include <imgui.h>
#include <imgui.cpp>
#include <backends/imgui_impl_renderware.cpp> 

// Declare external variables, functions, or macros if needed
// Example: extern void SomeExternalFunction();

MYMODCFG(net.johny.gtasa.onlineradio, GTA:SA Online Radio UI, 1.0, Johny)
NEEDGAME(com.rockstargames.gtasa)
BEGIN_DEPLIST()
    ADD_DEPENDENCY(net.rusjj.basslib)
END_DEPLIST()

#define MAX_RADIOS 32

#include "ibass.h"
#include "isautils.h"

IBASS* BASS = nullptr;
ISAUtils* sautils = nullptr;

uintptr_t pGTASA = 0;
void* hGTASA = NULL;

struct timeval pTimeNow;
time_t lCurrentS;
time_t lCurrentMs;

ConfigEntry* pCurrentRadioIndex;
ConfigEntry* pRadioVolume;

uint32_t pCurrentRadio = 0;
const char** pRadioStreams;
const char** pRadioNames;
char nRadiosCount = 0, nRadioIndex = -1;
bool bIsRadioStarted = false;
bool bIsRadioLoading = false;
bool bIsRadioStopped = false;
bool bIsRadioShouldBeRendered = false;
GxtChar RadioGXT[256] { 0 };
CRGBA clrRadioStop(128, 128, 128, 255);
CRGBA clrRadioLoading(255, 228, 181, 255);
CRGBA clrRadioPlaying(255, 255, 255, 255);
CRGBA clrRadioOutline(  0,   0,   0, 255);

// Timing functions
inline time_t GetCurrentTimeS()
{
    gettimeofday(&pTimeNow, nullptr);
    lCurrentS = pTimeNow.tv_sec;
    return lCurrentS;
}
inline time_t GetCurrentTimeMs()
{
    gettimeofday(&pTimeNow, nullptr);
    lCurrentMs = (1000 * pTimeNow.tv_sec) + (pTimeNow.tv_usec / 1000);
    return lCurrentMs;
}

// Declare variables for ImGui toggle
bool showRadioUI = false;

// Declare necessary variables
// Assuming these functions are implemented elsewhere
void PauseGame() { /* Your implementation */ }
void ResumeGame() { /* Your implementation */ }
void StartRadio() { /* Your implementation */ }
void StopRadio() { /* Your implementation */ }

// Declare key code for F1 (platform-specific)
#ifdef _WIN32
#include <windows.h>
#define VK_F1 0x70
#else
// For Linux/Android, define custom key codes or handle differently
#define VK_F1 0 // placeholder
#endif

// Declare variables for radio generation
std::atomic<int> nRadioGen(0);
std::mutex radioMutex;

// Initialize ImGui context
void InitImGuiBackend()
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui_ImplRenderWare_Init(); // Initialize your backend (ensure implementation exists)
}

// Cleanup ImGui
void ShutdownImGui()
{
    ImGui_ImplRenderWare_Shutdown();
    ImGui::DestroyContext();
}

// Function to render ImGui overlay
void RenderImGuiOverlay()
{
    if (!showRadioUI) return;

    ImGui::Begin("Radio Controls");
    if (ImGui::Button("Play/Pause"))
    {
        if (pCurrentRadio)
        {
            static bool isPaused = false;
            if (isPaused)
            {
                BASS->ChannelPlay(pCurrentRadio, false);
                isPaused = false;
            }
            else
            {
                BASS->ChannelPause(pCurrentRadio);
                isPaused = true;
            }
        }
    }

    static int volume = pRadioVolume ? pRadioVolume->GetInt() : 80;
    if (ImGui::SliderInt("Volume", &volume, 0, 100))
    {
        if (pRadioVolume) pRadioVolume->SetInt(volume);
        if (pCurrentRadio)
            BASS->ChannelSetAttribute(pCurrentRadio, BASS_ATTRIB_VOL, 0.005f * volume);
    }

    if (ImGui::Button("Stop"))
    {
        nRadioGen.fetch_add(1);
        {
            std::lock_guard<std::mutex> lk(radioMutex);
            if (pCurrentRadio)
            {
                BASS->ChannelStop(pCurrentRadio);
                BASS->StreamFree(pCurrentRadio);
                pCurrentRadio = 0;
            }
            bIsRadioStarted = false;
            bIsRadioLoading = false;
            bIsRadioStopped = true;
            bIsRadioShouldBeRendered = false;
        }
    }
    ImGui::End();
}

// Hook functions
// You should implement hooks like this
// Example:
void Hook_PauseGame()
{
    // Your hook code here
}
void Hook_ResumeGame()
{
    // Your hook code here
}
void Hook_StartRadio()
{
    // Your hook code here
}
void Hook_StopRadio()
{
    // Your hook code here
}

// Load your mod
void OnModLoad()
{
    // Initialize variables
    pGTASA = aml->GetLib("libGTASA.so");
    hGTASA = aml->GetLibHandle("libGTASA.so");

    BASS = (IBASS*)GetInterface("BASS");
    BASS->SetConfig(BASS_CONFIG_NET_TIMEOUT, 5000);

    // Bind configuration
    pCurrentRadioIndex = cfg->Bind("CurrentRadioIndex", 0);
    pRadioVolume = cfg->Bind("RadioVolume", 80);
    nRadiosCount = cfg->Bind("RadiosCount", 0)->GetInt();

    if (nRadiosCount > 0)
    {
        if (nRadiosCount > MAX_RADIOS) nRadiosCount = MAX_RADIOS;
        if (pCurrentRadioIndex->GetInt() < 0) pCurrentRadioIndex->SetInt(0);
        if (pCurrentRadioIndex->GetInt() > MAX_RADIOS) pCurrentRadioIndex->SetInt(MAX_RADIOS);

        nRadioIndex = -1;
        pRadioStreams = new const char*[nRadiosCount];
        pRadioNames = new const char*[nRadiosCount];

        char szTemp[16];
        for (int i = 0; i < nRadiosCount; ++i)
        {
            sprintf(szTemp, "Radio_%d", i + 1);
            pRadioStreams[i] = cfg->Bind(szTemp, "", "URLs")->GetString();
            pRadioNames[i] = cfg->Bind(szTemp, "Untitled Radio", "Names")->GetString();
        }

        if (pRadioVolume->GetInt() > 100) pRadioVolume->SetInt(100);
        else if (pRadioVolume->GetInt() < 0) pRadioVolume->SetInt(0);
        cfg->Save();
    }
    else
    {
        logger->Error("There is no radios in the config! Mod is not loaded.");
        return;
    }

    // Setup hooks
    HOOKPLT(PauseGame,          pGTASA + BYBIT(0x672644, 0x844230));
    HOOKPLT(ResumeGame,         pGTASA + BYBIT(0x67056C, 0x840CB0));
    HOOKPLT(StartRadio,         pGTASA + BYBIT(0x66F738, 0x83F5C0));
    HOOK(StopRadio,             aml->GetSym(hGTASA, "_ZN20CAERadioTrackManager9StopRadioEP21tVehicleAudioSettingsh"));

    // Remove radio from settings (if needed)
    aml->PlaceB(pGTASA + BYBIT(0x2A4D28 + 0x1, 0x3638A4), pGTASA + BYBIT(0x2A4D3C + 0x1, 0x3638C0));

    // Initialize ImGui
    InitImGuiBackend();

    // Register draw callback
    Events::drawHudEvent.after += []()
    {
        // Toggle UI with F1 key
        if (/* platform-specific key check */)
        {
            // Example for Windows
            #ifdef _WIN32
            if (GetAsyncKeyState(VK_F1) & 0x8000)
            {
                showRadioUI = !showRadioUI;
            }
            #endif
            // For Android/Linux, implement your key detection
        }

        // Start ImGui frame
        ImGui_ImplRenderWare_NewFrame();
        ImGui::NewFrame();

        // Render overlay if toggled
        RenderImGuiOverlay();

        // Render ImGui
        ImGui::Render();
        ImGui_ImplRenderWare_RenderDrawData(ImGui::GetDrawData());
    };
}

// Cleanup
void OnModUnload()
{
    ShutdownImGui();
}