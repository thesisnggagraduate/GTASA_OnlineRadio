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

MYMODCFG(net.rusjj.gtasa.onlineradio, GTA:SA Online Radio, 1.2, RusJJ)
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

// ================= TIME =================
struct timeval pTimeNow;
time_t lCurrentS;
time_t lCurrentMs;

// ================= CONFIG =================
ConfigEntry* pCurrentRadioIndex;
ConfigEntry* pRadioVolume;

// ================= RADIO =================
uint32_t pCurrentRadio = 0;
const char** pRadioStreams;
const char** pRadioNames;
char nRadiosCount, nRadioIndex;

// ================= FLAGS =================
bool bIsRadioStarted = false;
bool bIsRadioShouldBeRendered = false;
bool bRadioPending = false;

// ================= COLORS =================
GxtChar RadioGXT[256]{0};

CRGBA clrRadioLoading(255, 228, 181, 255);
CRGBA clrRadioPlaying(255, 255, 255, 255);
CRGBA clrRadioIdle(180, 180, 180, 255);

// ================= TIME =================
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

// ================= VOLUME =================
void VolumeChanged(int oldVal, int newVal, void* data)
{
    pRadioVolume->SetInt(newVal);

    if(pCurrentRadio)
    {
        BASS->ChannelSetAttribute(
            pCurrentRadio,
            BASS_ATTRIB_VOL,
            0.01f * newVal
        );
    }

    cfg->Save();
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

    sprintf((char*)RadioGXT, "< Current Radio >~n~%s", pRadioNames[nRadioIndex]);

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
            0.01f * pRadioVolume->GetInt()
        );

        if(!CTimer::IsPaused())
        {
            BASS->ChannelPlay(pCurrentRadio, true);
            bIsRadioStarted = true;
        }
    }
    else
    {
        logger->Error("Failed to open stream: %d", BASS->ErrorGetCode());

        // 🔥 IMPORTANT FIX: DO NOT HIDE UI
        bIsRadioStarted = false;
        bIsRadioShouldBeRendered = true;
    }

    bRadioPending = false;
    lock = false;
}

// ================= START RADIO =================
DECL_HOOK(void, StartRadio, uintptr_t self, uintptr_t vehicleInfo)
{
    if(FindPlayerVehicle(-1, false) != 0)
    {
        bIsRadioShouldBeRendered = true;

        if(!bRadioPending)
        {
            bRadioPending = true;
            std::thread(DoRadio).detach();
        }
    }

    StartRadio(self, vehicleInfo);
}

// ================= STOP RADIO =================
DECL_HOOK(void, StopRadio, uintptr_t self, uintptr_t vehicleInfo, unsigned char flag)
{
    bIsRadioStarted = false;
    bIsRadioShouldBeRendered = false;
    bRadioPending = false;

    if(pCurrentRadio)
    {
        BASS->ChannelStop(pCurrentRadio);
        BASS->StreamFree(pCurrentRadio);
        pCurrentRadio = 0;
    }

    nRadioIndex = -1;

    StopRadio(self, vehicleInfo, flag);
}

// ================= HUD =================
Events::drawHudEvent.after += []()
{
    if(bIsRadioShouldBeRendered)
    {
        float flScale = (float)RsGlobal.maximumHeight / 540.0f;

        CFont::SetScale(flScale);

        // 🎨 COLOR FIX
        if(!pCurrentRadio)
        {
            CFont::SetColor(clrRadioLoading);
        }
        else if(bIsRadioStarted)
        {
            CFont::SetColor(clrRadioPlaying);
        }
        else
        {
            CFont::SetColor(clrRadioIdle);
        }

        CFont::SetFontStyle(FO_FONT_STYLE_HEADING);
        CFont::SetEdge(1);
        CFont::SetOrientation(ALIGN_CENTER);

        CFont::PrintString(
            0.5f * RsGlobal.maximumWidth,
            0.02f * RsGlobal.maximumHeight,
            RadioGXT
        );

        CFont::RenderFontBuffer();
    }
};

// ================= TOUCH =================
Events::touchScreenEvent.after += [](int type, int finger, int x, int y)
{
    if(bIsRadioShouldBeRendered && type == 2 && !bRadioPending)
    {
        float top = RsGlobal.maximumHeight * 0.135f;
        float left = RsGlobal.maximumWidth * 0.33f;
        float right = RsGlobal.maximumWidth * 0.66f;
        float mid = RsGlobal.maximumWidth * 0.5f;

        if(y < top && x > left && x < right)
        {
            if(x > mid)
                ++nRadioIndex;
            else
                --nRadioIndex;

            pCurrentRadioIndex->SetInt(nRadioIndex);
            cfg->Save();

            bRadioPending = true;
            std::thread(DoRadio).detach();
        }
    }
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
    pRadioNames = new const char*[nRadiosCount];

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

    Events::gameProcessEvent += []()
    {
        // keep alive
    };
}
