#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>

#include "isautils.h"
ISAUtils* sautils = nullptr;

#define THUMB_ADDRESS(_address)   ((_address) | 1)

MYMODCFG(net.rusjj.gtasa.drawdistance, GTA:SA Draw Distance, 1.1, RusJJ)
NEEDGAME(com.rockstargames.gtasa)
BEGIN_DEPLIST()
    ADD_DEPENDENCY_VER(net.rusjj.aml, 1.0)
END_DEPLIST()

/* Saves */
uintptr_t pGTASA = 0;
unsigned int nStreamingMemoryOverride;
unsigned int nWaterBlocksToRender = 0;
uintptr_t pointerWaterBlocksToRender = 0;
float fRealAspectRatio = 0.0f;
float fAspectRatioScaler = 0.0f;
float flDontGetCfgValueEveryTick = 0.0f;

/* Configs */
ConfigEntry* pNearClipOverride;
ConfigEntry* pDrawDistanceOverride;
ConfigEntry* pStreamingDistanceScale;
ConfigEntry* pHiWaterDistanceScale;

/* Lib Pointers */
void* maincamera; // RwCamera*
int* streamingMemoryAvailable;
void* gMobileMenu;
float* CameraRangeMinX;
float* CameraRangeMaxX;
float* CameraRangeMinY;
float* CameraRangeMaxY;

/* Realloc */
int16_t* pBlocksToBeRenderedOutsideWorldX;
int16_t* pBlocksToBeRenderedOutsideWorldY;

/* Functions Pointers */
typedef float (*RetFloatFn)();

DECL_HOOK(void*, RwCameraSetNearClipPlane, void* self, float a1)
{
    if(self == maincamera && pNearClipOverride->GetFloat() > 0.0f)
    {
        return RwCameraSetNearClipPlane(self, pNearClipOverride->GetFloat());
    }
    return RwCameraSetNearClipPlane(self, a1);
}
DECL_HOOK(void*, RwCameraSetFarClipPlane, void* self, float a1)
{
    if(self == maincamera && pDrawDistanceOverride->GetInt() > 200)
        return RwCameraSetFarClipPlane(self, pDrawDistanceOverride->GetFloat());
    return RwCameraSetFarClipPlane(self, a1);
}

DECL_HOOK(void*, CameraCreate, void* a1, void* a2, int a3)
{
    maincamera = CameraCreate(a1, a2, a3);
    return maincamera;
}

DECL_HOOK(void*, CameraProcess, uintptr_t self)
{
    fAspectRatioScaler = fRealAspectRatio * 0.75 * pStreamingDistanceScale->GetFloat(); // AspectRatio / 4:3
    
    void* ret = CameraProcess(self);
    *(float*)(self + 236) *= fAspectRatioScaler;
    *(float*)(self + 240) *= fAspectRatioScaler;
    return ret;
}

DECL_HOOK(void*, CStreamingUpdate, void* self)
{
    if(*streamingMemoryAvailable < nStreamingMemoryOverride)
        *streamingMemoryAvailable = nStreamingMemoryOverride;
    return CStreamingUpdate(self);
}

// SAUtils
#define DEFAULT_DRAWDISTANCE 800.0f
char szRetDrawDistanceSlider[12];
void RealDrawDistanceChanged(int oldVal, int newVal)
{
    pDrawDistanceOverride->SetInt(newVal);
    cfg->Save();
}
const char* RealDrawDistanceDraw(int nNewValue)
{
    sprintf(szRetDrawDistanceSlider, "x%.2f", (nNewValue / DEFAULT_DRAWDISTANCE));
    return szRetDrawDistanceSlider;
}
void StreamingDistanceChanged(int oldVal, int newVal)
{
    pStreamingDistanceScale->SetFloat(0.01f * newVal);
    cfg->Save();
}
const char* StreamingDistanceDraw(int nNewValue)
{
    sprintf(szRetDrawDistanceSlider, "x%.2f", (nNewValue / 100.0f));
    return szRetDrawDistanceSlider;
}
void HiWaterDistanceChanged(int oldVal, int newVal)
{
    pHiWaterDistanceScale->SetFloat(0.01f * newVal);
    flDontGetCfgValueEveryTick = pHiWaterDistanceScale->GetFloat();
    cfg->Save();
}
const char* HiWaterDistanceDraw(int nNewValue)
{
    sprintf(szRetDrawDistanceSlider, "%d%%", nNewValue);
    return szRetDrawDistanceSlider;
}

TARGET_THUMB ASM_NAKED void HitWaterBlock_JMP()
{
    __asm(
    ".hidden nWaterBlocksToRender\n"
    ".hidden pointerWaterBlocksToRender\n"
    ".thumb\n"
        "PUSH {R1}\n" // Backup R1
        "LDR R1, =(nWaterBlocksToRender - 100001f - 2*(100002f-100001f))\n" // Copy contents of variable to R0
        "100001:\nADD R1, PC\n100002:\n"
        "LDR R1, [R1]\n"
        
        "CMP R0, R1\n" // Compare R0=nWaterBlocksToRender with R1=m_NumBlocksOutsideWorldToBeRendered
        "POP {R1}\n" // Bring back R1

		"IT GT\n"
		"POPGT {R4,R5,R7,PC}\n"
		"LDR R1, =(0x6777C4 - 0x59869C)\n"

        "PUSH {R0,R1}\n"
        "LDR R0, =(pointerWaterBlocksToRender - 100001f - 2*(100002f-100001f))\n" // Copy contents of variable to R0
        "100001:\nADD R0, PC\n100002:\n"
        "LDR R0, [R0]\n"
        "STR R0, [SP, #4]\n"
        "POP {R0,PC}\n"
    );
} 

void CodeRedirect(uintptr_t address, uintptr_t newAddress, bool isThumb)
{
    if(isThumb)
    {
        char code[12];
        unsigned int sizeOfData = 0;

        if (address % 4 == 0)
        {
            *(uint32_t*)(code + 0) = 0xF000F8DF;
            *(const void**)(code + 4) = (const void*)newAddress;
            sizeOfData = 8;
        }
        else
        {
            *(uint32_t*)(code + 0) = 0xBF00;
            *(uint32_t*)(code + 2) = 0xF000F8DF;
            *(const void**)(code + 6) = (const void*)newAddress;
            sizeOfData = 10;
        }
        aml->Write(address, (uintptr_t)code, sizeOfData);
        return;
    }

	char code[8];
	*(uint32_t*)(code + 0) = 0xE51FF004;
	*(const void**)(code + 4) = (const void*)newAddress;
    aml->Write(address, (uintptr_t)code, sizeof(code));
}

DECL_HOOK(void, emu_SetWater, bool set)
{
    emu_SetWater(set);
    if(set)
    {
        //*CameraRangeMinX *= flDontGetCfgValueEveryTick;
        *CameraRangeMaxX *= flDontGetCfgValueEveryTick;
        //*CameraRangeMinY *= flDontGetCfgValueEveryTick;
        *CameraRangeMaxY *= flDontGetCfgValueEveryTick;
    }
}

extern "C" void OnModLoad()
{
    logger->SetTag("GTASA Draw Distance");
    pGTASA = aml->GetLib("libGTASA.so");

    pNearClipOverride = cfg->Bind("NearClip", "0.1");
    pDrawDistanceOverride = cfg->Bind("DrawDistance", "800.0");
    nStreamingMemoryOverride = cfg->Bind("PreferredStreamingMemMB", "1024")->GetInt() * 1024 * 1024;
    nWaterBlocksToRender = (unsigned int)cfg->Bind("WaterBlocksToRender", "384")->GetInt();
    pStreamingDistanceScale = cfg->Bind("StreamingDistanceScale", "1.0");

    fRealAspectRatio = ((RetFloatFn)(pGTASA + 0x18E984))();
    if(fRealAspectRatio < 1.0f) fRealAspectRatio = 1.0f / fRealAspectRatio;
    streamingMemoryAvailable = (int*)(pGTASA + 0x685FA0);

    gMobileMenu = (void*)(pGTASA + 0x6E006C);

    if(nWaterBlocksToRender > 1)
    {
        if(nWaterBlocksToRender > 70) // Default value is 0-69 (70) so don`t override if not necessary
        {
            pBlocksToBeRenderedOutsideWorldX = new int16_t[nWaterBlocksToRender];
            aml->Write(pGTASA + 0x6777C4, (uintptr_t)&pBlocksToBeRenderedOutsideWorldX, sizeof(void*));

            pBlocksToBeRenderedOutsideWorldY = new int16_t[nWaterBlocksToRender];
            aml->Write(pGTASA + 0xA1C05C, (uintptr_t)&pBlocksToBeRenderedOutsideWorldY, sizeof(void*));
        }
        --nWaterBlocksToRender;
        pointerWaterBlocksToRender = (pGTASA + 0x598694) | 1;
        CodeRedirect(pGTASA + 0x59868C, (uintptr_t)&HitWaterBlock_JMP, true); // From FLA
    }

    HOOKPLT(RwCameraSetNearClipPlane, pGTASA + 0x670C9C);
    HOOKPLT(RwCameraSetFarClipPlane, pGTASA + 0x6740CC);

    HOOKPLT(CameraCreate, pGTASA + 0x675174);
    HOOKPLT(CameraProcess, pGTASA + 0x6717BC);

    HOOKPLT(CStreamingUpdate, pGTASA + 0x673898);

    if(cfg->Bind("MoreOftenPopulationUpdate", "1")->GetBool())
    {
        aml->Unprot(pGTASA + 0x3F40C0, sizeof(char));
        *(char*)(pGTASA + 0x3F40C0) = 2;
    }
    
    // Do not delete vehicles behind the player camera
    bool bRemoveCarsBehind = cfg->Bind("DontRemoveVehicleBehindCamera", "1")->GetBool();
    if(bRemoveCarsBehind)
    {
        aml->PlaceJMP(pGTASA + 0x2EC660, pGTASA + 0x2EC6D6);
    }
    
    // Do not delete peds behind the player camera
    if(cfg->Bind("DontRemovePedBehindCamera", "1")->GetBool())
    {
        aml->PlaceJMP(pGTASA + 0x4CE4EA, pGTASA + 0x4CE55C);
    }

    if(cfg->Bind("SpawnVehiclesInFrontOfPlayer", "1")->GetBool())
    {
        if(bRemoveCarsBehind) // They are already spawning!
        {
            //aml->Unprot(pGTASA + 0x2E866F, sizeof(char));
            //*(char*)(pGTASA + 0x2E866F) = 0xDB;
        }
        else
        {
            aml->PlaceJMP(pGTASA + 0x2E864E, pGTASA + 0x2E8670);
        }
    }
    else
    {
        if(bRemoveCarsBehind) // Huh?
        {
            aml->Unprot(pGTASA + 0x2E866F, sizeof(char));
            *(char*)(pGTASA + 0x2E866F) = 0xDB;
        }
    }

    /* Unstable + Useless */
    //pHiWaterDistanceScale = cfg->Bind("HighWaterDistanceScale", 1.0f);
    //flDontGetCfgValueEveryTick = pHiWaterDistanceScale->GetFloat();
    //CameraRangeMinX = (float*)(pGTASA + 0xA1DC8C);
    //CameraRangeMaxX = (float*)(pGTASA + 0xA1DC90);
    //CameraRangeMinY = (float*)(pGTASA + 0xA1DC94);
    //CameraRangeMaxY = (float*)(pGTASA + 0xA1DC98);
    //HOOKPLT(emu_SetWater, pGTASA + 0x673530);

    sautils = (ISAUtils*)GetInterface("SAUtils");
    if(sautils != nullptr)
    {
        sautils->AddSliderItem(Display, "Real Draw Distance", pDrawDistanceOverride->GetInt(), 200, 4000, RealDrawDistanceChanged, RealDrawDistanceDraw);
        sautils->AddSliderItem(Display, "Streaming Distance Scale", 100 * pStreamingDistanceScale->GetFloat(), 100 * 0.25f, 100 * 5.0f, StreamingDistanceChanged, StreamingDistanceDraw);
        //sautils->AddSliderItem(Display, "Detailed Water Draw Distance", 100 * pHiWaterDistanceScale->GetFloat(), 0, 100 * 5.0f, HiWaterDistanceChanged, HiWaterDistanceDraw);
    }
}