#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>
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

MYMODCFG(net.rusjj.gtasa.onlineradio, GTA:SA Online Radio FIX FULL, 1.5, RusJJ)
NEEDGAME(com.rockstargames.gtasa)
BEGIN_DEPLIST()
    ADD_DEPENDENCY(net.rusjj.basslib)
END_DEPLIST()

#define MAX_RADIOS 32

#include "ibass.h"
IBASS* BASS = nullptr;

#include "isautils.h"
ISAUtils* sautils = nullptr;

uintptr_t pGTASA = 0;
void* hGTASA = NULL;

// ================= TIME (RESTORED) =================
struct timeval pTimeNow;
time_t lCurrentS;
time_t lCurrentMs;

inline time_t GetCurrentTimeS()
{
    gettimeofday(&pTimeNow, nullptr);
    lCurrentS = pTimeNow.tv_sec;
    return lCurrentS;
}

inline time_t GetCurrentTimeMs()
{
    gettimeofday(&pTimeNow, nullptr);
    lCurrentMs = (1000 * pTimeNow.tv_sec) + (0.001f * pTimeNow.tv_usec);
    return lCurrentMs;
}

// ================= CONFIG =================
ConfigEntry* pCurrentRadioIndex;
ConfigEntry* pRadioVolume;

// ================= RADIO =================
uint32_t pCurrentRadio = 0;
const char** pRadioStreams;
const char** pRadioNames;

char nRadiosCount = 0;
int nRadioIndex = 0;

// ================= FLAGS =================
bool bRadioUI = false;
bool bRadioStarted = false;
bool bRadioPending = false;

// ================= TEXT =================
GxtChar RadioGXT[256]{0};

// ================= COLORS =================
CRGBA clrLoading(255, 228, 181, 255);
CRGBA clrPlaying(255, 255, 255, 255);
CRGBA clrIdle(180, 180, 180, 255);

// ================= VOLUME (RESTORED EXACT) =================
void VolumeChanged(int oldVal, int newVal, void* data)
{
    pRadioVolume->SetInt(newVal);

    if(pCurrentRadio)
    {
        BASS->ChannelSetAttribute(
            pCurrentRadio,
            BASS_ATTRIB_VOL,
            0.005f * newVal
        );
    }

    cfg->Save();
}

// ================= PAUSE =================
DECL_HOOK(bool, PauseGame, void* self)
{
    if(pCurrentRadio)
        BASS->ChannelPause(pCurrentRadio);

    return PauseGame(self);
}

// ================= RESUME =================
DECL_HOOK(bool, ResumeGame, void* self)
{
    if(pCurrentRadio && !CTimer::IsPaused())
        BASS->ChannelPlay(pCurrentRadio, false);

    return ResumeGame(self);
}

// ================= RADIO CORE =================
void DoRadio()
{
    static bool lock = false;
    if(lock) return;
    lock = true;

    nRadioIndex = pCurrentRadioIndex->GetInt();

    if(nRadioIndex < 0) nRadioIndex = nRadiosCount - 1;
    if(nRadioIndex >= nRadiosCount) nRadioIndex = 0;

    if(pCurrentRadio)
    {
        BASS->ChannelStop(pCurrentRadio);
        BASS->StreamFree(pCurrentRadio);
        pCurrentRadio = 0;
    }

    sprintf((char*)RadioGXT, "< Online Radio >~n~%s", pRadioNames[nRadioIndex]);

    auto stream = BASS->StreamCreateURL(
        pRadioStreams[nRadioIndex],
        0,
        BASS_STREAM_BLOCK | BASS_STREAM_STATUS | BASS_STREAM_AUTOFREE | BASS_SAMPLE_FLOAT,
        0
    );

    if(stream)
    {
        pCurrentRadio = stream;

        BASS->ChannelSetAttribute(
            pCurrentRadio,
            BASS_ATTRIB_VOL,
            0.005f * pRadioVolume->GetInt()
        );

        if(!CTimer::IsPaused())
        {
            BASS->ChannelPlay(pCurrentRadio, true);
            bRadioStarted = true;
        }
    }
    else
    {
        logger->Error("Stream failed: %d", BASS->ErrorGetCode());
        bRadioStarted = false;

        // IMPORTANT FIX: UI NEVER DISAPPEARS
        bRadioUI = true;
    }

    bRadioPending = false;
    lock = false;
}

// ================= START =================
DECL_HOOK(void, StartRadio, uintptr_t self, uintptr_t vehicleInfo)
{
    if(FindPlayerVehicle(-1, false) != 0)
    {
        bRadioUI = true;
        nRadioIndex = pCurrentRadioIndex->GetInt();

        if(!bRadioPending)
        {
            bRadioPending = true;
            std::thread(DoRadio).detach();
        }
    }

    StartRadio(self, vehicleInfo);
}

// ================= STOP =================
DECL_HOOK(void, StopRadio, uintptr_t self, uintptr_t vehicleInfo, unsigned char flag)
{
    bRadioUI = false;
    bRadioStarted = false;
    bRadioPending = false;

    if(pCurrentRadio)
    {
        BASS->ChannelStop(pCurrentRadio);
        BASS->StreamFree(pCurrentRadio);
        pCurrentRadio = 0;
    }

    nRadioIndex = 0;

    StopRadio(self, vehicleInfo, flag);
}

// ================= MAIN LOOP (FIXED EVENT USAGE ONLY) =================
Events::gameProcessEvent += []()
{
    if(!bRadioUI) return;

    // ================= INPUT =================
    CPad* pad = CPad::GetPad(0);

    if(!bRadioPending)
    {
        if(pad->NewState.LeftShoulder1)
        {
            nRadioIndex--;
            pCurrentRadioIndex->SetInt(nRadioIndex);
            cfg->Save();

            bRadioPending = true;
            std::thread(DoRadio).detach();
        }

        if(pad->NewState.RightShoulder1)
        {
            nRadioIndex++;
            pCurrentRadioIndex->SetInt(nRadioIndex);
            cfg->Save();

            bRadioPending = true;
            std::thread(DoRadio).detach();
        }
    }

    // ================= HUD =================
    float scale = (float)RsGlobal.maximumHeight / 540.0f;

    CFont::SetScale(scale);

    if(!pCurrentRadio)
        CFont::SetColor(clrLoading);
    else if(bRadioStarted)
        CFont::SetColor(clrPlaying);
    else
        CFont::SetColor(clrIdle);

    CFont::SetFontStyle(FO_FONT_STYLE_HEADING);
    CFont::SetEdge(1);
    CFont::SetOrientation(ALIGN_CENTER);

    CFont::PrintString(
        0.5f * RsGlobal.maximumWidth,
        0.02f * RsGlobal.maximumHeight,
        RadioGXT
    );

    CFont::RenderFontBuffer();
};

// ================= INIT =================
ON_MOD_LOAD()
{
    pGTASA = aml->GetLib("libGTASA.so");
    hGTASA = aml->GetLibHandle("libGTASA.so");

    BASS = (IBASS*)GetInterface("BASS");

    pCurrentRadioIndex = cfg->Bind("CurrentRadioIndex", 0);
    pRadioVolume = cfg->Bind("RadioVolume", 80);

    nRadiosCount = cfg->Bind("RadiosCount", 0)->GetInt();

    if(nRadiosCount > MAX_RADIOS)
        nRadiosCount = MAX_RADIOS;

    if(nRadiosCount <= 0)
    {
        logger->Error("No radios found!");
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

    cfg->Save();

    HOOKPLT(StartRadio, pGTASA + BYBIT(0x66F738, 0x83F5C0));

    HOOK(StopRadio,
        aml->GetSym(hGTASA,
        "_ZN20CAERadioTrackManager9StopRadioEP21tVehicleAudioSettingsh")
    );
}
