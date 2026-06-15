#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>
#include <thread>
#include <cmath>
#include <dlfcn.h>

#include <Events.h>
#include <engine/RsGlobal.h>
#include <base/Timer.h>

#include "ibass.h"
IBASS* BASS = nullptr;

#include "isautils.h"
ISAUtils* sautils = nullptr;

MYMODCFG(net.rusjj.gtasa.onlineradio, GTA:SA Online Radio IMGUI, 2.0, RusJJ)
NEEDGAME(com.rockstargames.gtasa)

#define MAX_RADIOS 32

uintptr_t pGTASA = 0;
void* hGTASA = NULL;

// ===================== CONFIG =====================
ConfigEntry* pCurrentRadioIndex;
ConfigEntry* pRadioVolume;

const char** pRadioStreams;
const char** pRadioNames;

int nRadiosCount = 0;
int nRadioIndex = 0;

uint32_t pCurrentRadio = 0;
bool bRadioPlaying = false;

// ===================== IMGUI =====================
bool g_showRadioUI = true;

// ===================== HELPERS =====================
void PlayStation(int index)
{
    if(index < 0 || index >= nRadiosCount) return;

    nRadioIndex = index;
    pCurrentRadioIndex->SetInt(index);

    if(pCurrentRadio)
    {
        BASS->ChannelStop(pCurrentRadio);
        BASS->StreamFree(pCurrentRadio);
        pCurrentRadio = 0;
    }

    pCurrentRadio = BASS->StreamCreateURL(
        pRadioStreams[index],
        0,
        BASS_STREAM_BLOCK | BASS_STREAM_STATUS | BASS_STREAM_AUTOFREE | BASS_SAMPLE_FLOAT,
        0
    );

    if(pCurrentRadio)
    {
        BASS->ChannelSetAttribute(
            pCurrentRadio,
            BASS_ATTRIB_VOL,
            0.005f * pRadioVolume->GetInt()
        );

        if(!CTimer::IsPaused())
        {
            BASS->ChannelPlay(pCurrentRadio, false);
            bRadioPlaying = true;
        }
    }
    else
    {
        logger->Error("Failed to open stream: %d", BASS->ErrorGetCode());
    }
}

void NextStation()
{
    int idx = nRadioIndex + 1;
    if(idx >= nRadiosCount) idx = 0;
    PlayStation(idx);
}

void PrevStation()
{
    int idx = nRadioIndex - 1;
    if(idx < 0) idx = nRadiosCount - 1;
    PlayStation(idx);
}

void SetVolume(int vol)
{
    if(pCurrentRadio)
        BASS->ChannelSetAttribute(pCurrentRadio, BASS_ATTRIB_VOL, 0.005f * vol);

    pRadioVolume->SetInt(vol);
    cfg->Save();
}

// ===================== GAME HOOKS =====================
DECL_HOOK(void, PauseGame, void* self)
{
    if(pCurrentRadio)
        BASS->ChannelPause(pCurrentRadio);

    return PauseGame(self);
}

DECL_HOOK(void, ResumeGame, void* self)
{
    if(pCurrentRadio && !CTimer::IsPaused())
        BASS->ChannelPlay(pCurrentRadio, false);

    return ResumeGame(self);
}

// ===================== STOP RADIO =====================
DECL_HOOK(void, StopRadio, uintptr_t self, uintptr_t vehicleInfo, unsigned char flag)
{
    if(pCurrentRadio)
    {
        BASS->ChannelStop(pCurrentRadio);
        BASS->StreamFree(pCurrentRadio);
        pCurrentRadio = 0;
    }

    bRadioPlaying = false;

    return StopRadio(self, vehicleInfo, flag);
}

// ===================== START RADIO =====================
void StartRadioThread()
{
    PlayStation(nRadioIndex);
}

DECL_HOOK(void, StartRadio, uintptr_t self, uintptr_t vehicleInfo)
{
    if(FindPlayerVehicle(-1, false) != 0)
        std::thread(StartRadioThread).detach();
}

// ===================== IMGUI UI =====================
void DrawRadioUI()
{
    if(!g_showRadioUI) return;

    ImGui::Begin("🎵 Online Radio", &g_showRadioUI);

    ImGui::Text("Station: %s", pRadioNames[nRadioIndex]);
    ImGui::Separator();

    // Station list
    if(ImGui::BeginListBox("Stations"))
    {
        for(int i = 0; i < nRadiosCount; i++)
        {
            bool selected = (i == nRadioIndex);
            if(ImGui::Selectable(pRadioNames[i], selected))
            {
                PlayStation(i);
            }
        }
        ImGui::EndListBox();
    }

    ImGui::Separator();

    // Controls
    if(ImGui::Button("⏮ Prev"))
        PrevStation();

    ImGui::SameLine();

    if(ImGui::Button(bRadioPlaying ? "⏸ Stop" : "▶ Play"))
    {
        if(bRadioPlaying)
        {
            if(pCurrentRadio)
                BASS->ChannelPause(pCurrentRadio);

            bRadioPlaying = false;
        }
        else
        {
            PlayStation(nRadioIndex);
        }
    }

    ImGui::SameLine();

    if(ImGui::Button("⏭ Next"))
        NextStation();

    ImGui::Separator();

    // Volume
    int vol = pRadioVolume->GetInt();
    if(ImGui::SliderInt("Volume", &vol, 0, 100))
    {
        SetVolume(vol);
    }

    ImGui::Separator();

    ImGui::Text("Status: %s",
        bRadioPlaying ? "Playing" : "Stopped"
    );

    ImGui::End();
}

// ===================== MOD LOAD =====================
ON_MOD_LOAD()
{
    pGTASA = aml->GetLib("libGTASA.so");
    hGTASA = aml->GetLibHandle("libGTASA.so");

    BASS = (IBASS*)GetInterface("BASS");
    BASS->SetConfig(BASS_CONFIG_NET_TIMEOUT, 5000);

    // config
    pCurrentRadioIndex = cfg->Bind("CurrentRadioIndex", 0);
    pRadioVolume = cfg->Bind("RadioVolume", 80);

    nRadiosCount = cfg->Bind("RadiosCount", 0)->GetInt();
    if(nRadiosCount > MAX_RADIOS) nRadiosCount = MAX_RADIOS;

    if(nRadiosCount <= 0)
    {
        logger->Error("No radios found in config!");
        return;
    }

    pRadioStreams = new const char*[nRadiosCount];
    pRadioNames   = new const char*[nRadiosCount];

    char key[64];

    for(int i = 0; i < nRadiosCount; i++)
    {
        sprintf(key, "Radio_%d", i + 1);

        pRadioStreams[i] = cfg->Bind(key, "", "URLs")->GetString();
        pRadioNames[i]   = cfg->Bind(key, "Untitled", "Names")->GetString();
    }

    nRadioIndex = pCurrentRadioIndex->GetInt();
    if(nRadioIndex < 0) nRadioIndex = 0;

    cfg->Save();

    // hooks
    HOOKPLT(PauseGame,  pGTASA + BYBIT(0x672644, 0x844230));
    HOOKPLT(ResumeGame, pGTASA + BYBIT(0x67056C, 0x840CB0));
    HOOKPLT(StartRadio, pGTASA + BYBIT(0x66F738, 0x83F5C0));
    HOOK(StopRadio, aml->GetSym(hGTASA, "_ZN20CAERadioTrackManager9StopRadioEP21tVehicleAudioSettingsh"));

    // audio settings hook remove radio option
    aml->PlaceB(pGTASA + BYBIT(0x2A4D28 + 1, 0x3638A4),
                pGTASA + BYBIT(0x2A4D3C + 1, 0x3638C0));

    sautils = (ISAUtils*)GetInterface("SAUtils");
    if(sautils)
    {
        sautils->AddSliderItem(
            SetType_Audio,
            "Online Radio Volume",
            pRadioVolume->GetInt(),
            0,
            100,
            SetVolume
        );
    }

    // ===================== IMGUI RENDER =====================
    Events::drawHudEvent.after += []()
    {
        DrawRadioUI();
    };
}
