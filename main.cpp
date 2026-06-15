#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>
#include <thread>
#include <sys/time.h>

#include <Events.h>
#include <engine/Font.h>
#include <engine/RsGlobal.h>
#include <base/Timer.h>
#include <entity/PlayerPed.h>

MYMODCFG(net.rusjj.gtasa.onlineradio, GTA:SA Online Radio FIXED, 1.3, RusJJ)
NEEDGAME(com.rockstargames.gtasa)
BEGIN_DEPLIST()
    ADD_DEPENDENCY(net.rusjj.basslib)
END_DEPLIST()

#define MAX_RADIOS 32

#include "ibass.h"
IBASS* BASS = nullptr;

// ================= GLOBALS =================
uintptr_t pGTASA = 0;
void* hGTASA = NULL;

ConfigEntry* pCurrentRadioIndex;
ConfigEntry* pRadioVolume;

const char** pRadioStreams;
const char** pRadioNames;

uint32_t pCurrentRadio = 0;
char nRadiosCount = 0;
int nRadioIndex = 0;

// ===== FIXED FLAGS (IMPORTANT) =====
bool bRadioUI = false;        // ONLY UI
bool bRadioActive = false;    // ONLY AUDIO
bool bRadioPending = false;

GxtChar RadioGXT[256]{0};

// ================= COLORS =================
CRGBA clrIdle(180, 180, 180, 255);
CRGBA clrPlaying(255, 255, 255, 255);
CRGBA clrError(255, 80, 80, 255);

// ================= CLAMP =================
inline void ClampIndex()
{
    if(nRadiosCount <= 0) return;

    if(nRadioIndex < 0)
        nRadioIndex = nRadiosCount - 1;

    if(nRadioIndex >= nRadiosCount)
        nRadioIndex = 0;
}

// ================= RADIO CORE =================
void DoRadio()
{
    static bool lock = false;
    if(lock) return;
    lock = true;

    ClampIndex();

    int index = nRadioIndex;

    if(pCurrentRadio)
    {
        BASS->ChannelStop(pCurrentRadio);
        BASS->StreamFree(pCurrentRadio);
        pCurrentRadio = 0;
    }

    sprintf((char*)RadioGXT, "< Online Radio >~n~%s", pRadioNames[index]);

    auto stream = BASS->StreamCreateURL(
        pRadioStreams[index],
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
            bRadioActive = true;
        }
    }
    else
    {
        logger->Error("Stream failed: %d", BASS->ErrorGetCode());
        bRadioActive = false; // ONLY AUDIO RESET
    }

    bRadioPending = false;
    lock = false;
}

// ================= START RADIO =================
DECL_HOOK(void, StartRadio, uintptr_t self, uintptr_t vehicleInfo)
{
    if(FindPlayerVehicle(-1, false))
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

// ================= STOP RADIO =================
DECL_HOOK(void, StopRadio, uintptr_t self, uintptr_t vehicleInfo, unsigned char flag)
{
    bRadioUI = false;
    bRadioActive = false;
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

// ================= PAUSE / RESUME (FIXED) =================
DECL_HOOK(bool, PauseGame, void* self)
{
    if(pCurrentRadio)
        BASS->ChannelPause(pCurrentRadio);

    return PauseGame(self);
}

DECL_HOOK(bool, ResumeGame, void* self)
{
    if(pCurrentRadio && !CTimer::IsPaused())
        BASS->ChannelPlay(pCurrentRadio, false);

    return ResumeGame(self);
}

// ================= TOUCH INPUT =================
Events::touchScreenEvent.after += [](int type, int finger, int x, int y)
{
    if(!bRadioUI || type != 2 || bRadioPending)
        return;

    float top = RsGlobal.maximumHeight * 0.135f;
    float left = RsGlobal.maximumWidth * 0.33f;
    float right = RsGlobal.maximumWidth * 0.66f;
    float mid = RsGlobal.maximumWidth * 0.5f;

    if(y < top && x > left && x < right)
    {
        if(x > mid)
            nRadioIndex++;
        else
            nRadioIndex--;

        ClampIndex();

        pCurrentRadioIndex->SetInt(nRadioIndex);
        cfg->Save();

        bRadioPending = true;
        std::thread(DoRadio).detach();
    }
};

// ================= HUD =================
Events::drawHudEvent.after += []()
{
    if(!bRadioUI)
        return;

    float scale = (float)RsGlobal.maximumHeight / 540.0f;

    CFont::SetScale(scale);

    // COLOR LOGIC (SAFE)
    if(!pCurrentRadio)
        CFont::SetColor(clrError);
    else if(bRadioActive)
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
    if(nRadiosCount > MAX_RADIOS) nRadiosCount = MAX_RADIOS;

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
    HOOK(StopRadio, aml->GetSym(hGTASA,
        "_ZN20CAERadioTrackManager9StopRadioEP21tVehicleAudioSettingsh"));

    Events::gameProcessEvent += []()
    {
        // keep alive
    };
}
