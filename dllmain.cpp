#include <Windows.h>
#include <string>
#include <iostream>
#include <fstream>
#include <thread>

#include "Memory.h"

#include "Interface.h"
#include "globals.hpp"
#include "min_hook.h"

#include "hooks/DrawModelExecute.h"
#include "hooks/CreateMove.h"
#include "hooks/FrameStageNotify.h"
#include "hooks/RenderView.h"
#include "hooks/Present.h"
#include "hooks/PaintTraverse.h"
#include "hooks/RunStringEx.h"
#include "hooks/ProcessGMODServerToClient.h"
#include "hooks/RunCommand.h"
#include "hooks/Paint.h"


#include "hacks/ConVarSpoofing.h"
#include "engine/inetmessage.h"

void Main()
{
    ZeroMemory(Settings::ScriptInput, sizeof(Settings::ScriptInput));
    srand(time(nullptr));
#ifdef _DEBUG
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONIN$", "r", stdin);
    SetConsoleTitle(L"GMod SDK - WIP - Coded by t.me/Gaztoof");
#endif
    ConColorMsg = (MsgFn)GetProcAddress(GetModuleHandleW(L"tier0.dll"), ConColorMsgDec);

    ConPrint("Successfully injected!", Color(0, 255, 0));
    ConfigSystem::HandleConfig("Default", ConfigSystem::configHandle::Load);

    // TODO: Implement SendNetMsg fakelag instead of bSendpacket (old method)
    // Globals::bSendpacket = (bool*)(GetRealFromRelative((char*)findPattern("engine", CL_MovePattern, "CL_MOVE"), 0x1, 5) + BSendPacketOffset);
    // DWORD originalProtection;
    // VirtualProtect(Globals::bSendpacket, sizeof(bool), PAGE_EXECUTE_READWRITE, &originalProtection);

    Globals::predictionRandomSeed = (unsigned int*)(GetRealFromRelative((char*)findPattern("client", PredictionSeedPattern, "predictionRandomSeed") + 0x3, 0x2, 6));
    Globals::hostName = (char*)(GetRealFromRelative((char*)findPattern("client", HostNamePattern, "HostName"), 0x3, 7));
    MoveHelper = (GetRealFromRelative((char*)findPattern("client", MoveHelperPattern, "MoveHelper"), 0x3, 7)); // https://i.imgur.com/p3C93PT.png

    EngineClient = (CEngineClient*)GetInterface("engine.dll", "VEngineClient015");
    LuaShared = (CLuaShared*)GetInterface("lua_shared.dll", "LUASHARED003");
    ClientEntityList = (CClientEntityList*)GetInterface("client.dll", "VClientEntityList003");
    CHLclient = (CHLClient*)GetInterface("client.dll", "VClient017");
    MaterialSystem = (CMaterialSystem*)GetInterface("materialsystem.dll", "VMaterialSystem080");
    InputSystem = (CInputSystem*)GetInterface("inputsystem.dll", "InputSystemVersion001");
    CVar = (CCvar*)GetInterface("vstdlib.dll", "VEngineCvar007");
    ModelRender = (CModelRender*)GetInterface("engine.dll", "VEngineModel016");
    RenderView = (CVRenderView*)GetInterface("engine.dll", "VEngineRenderView014");
    EngineTrace = (IEngineTrace*)GetInterface("engine.dll", "EngineTraceClient003");
    IVDebugOverlay = (CIVDebugOverlay*)GetInterface("engine.dll", "VDebugOverlay003");
    GameEventManager = (CGameEventManager*)GetInterface("engine.dll", "GAMEEVENTSMANAGER002");
    MatSystemSurface = (CMatSystemSurface*)GetInterface("vguimatsurface.dll", "VGUI_Surface030");
    PanelWrapper = (VPanelWrapper*)GetInterface("vgui2.dll", "VGUI_Panel009");
    PhysicsSurfaceProps = (CPhysicsSurfaceProps*)GetInterface("vphysics.dll", "VPhysicsSurfaceProps001");
    Prediction = (CPrediction*)GetInterface("client.dll", "VClientPrediction001");
    GameMovement = (CGameMovement*)GetInterface("client.dll", "GameMovement001");
    EngineVGui = (void*)GetInterface("engine.dll", "VEngineVGui001"); // Eventually implement that?
    ModelInfo = (CModelInfo*)GetInterface("engine.dll", "VModelInfoClient006");

    // x64: thats directly the vtable pointer // CEngineClient::IsPaused points to clientstate https://i.imgur.com/4aWvQbs.png
    ClientState = GetRealFromRelative((*(char***)(EngineClient))[84], CClientStateOffset, CClientStateSize, false);

    // Try GetRealFromRelative for GlobalVars (like zxcmodule with getAbsAddr)
    GlobalVars = (CGlobalVarsBase*)GetRealFromRelative((char*)((uintptr_t*)CHLclient)[0], GlobalVarsOffset, 7);

    // Find ClientMode (g_pClientMode) using pattern scan
    char* clientModePattern = (char*)findPattern("client.dll", "\x48\x8B\x0D????\x48\x8B\xD8\x48\x85\xC0", "ClientMode");
    if (!clientModePattern)
        clientModePattern = (char*)findPattern("client.dll", "\x48\x8B\x05????\x48\x8B\xC8\x48\x85\xC0", "ClientMode");

    if (clientModePattern)
    {
        int offset = *(int*)(clientModePattern + 3);
        ClientMode = *reinterpret_cast<ClientModeShared**>(clientModePattern + 7 + offset);
    }
    else
    {
        // Alternative: try to get ClientMode from Init hook on first call
        // For now, set to nullptr and try again in FrameStageNotify
        ConPrint("ClientMode pattern not found, will try alternative method", Color(255, 255, 0));
    }

    // TODO: Fix GetVMT - offsets may be wrong for game version
    // Input = GetVMT<CInput>((uintptr_t)CHLclient, 20, InputOffset);
    // ViewRender = GetVMT<CViewRender>((uintptr_t)CHLclient, 2, ViewRenderOffset);
    // UniformRandomStream = GetVMT<CUniformRandomStream>((uintptr_t)GetProcAddress(GetModuleHandleA("vstdlib.dll"), "RandomSeed"), RandomSeedOffset);

    // Hook CreateMove if ClientMode was found
    if (ClientMode)
    {
        oCreateMove = VMTHook<_CreateMove>((PVOID**)ClientMode, (PVOID)hkCreateMove, 21);
        if (oCreateMove)
            ConPrint("CreateMove hook installed successfully!", Color(0, 255, 0));
        else
            ConPrint("CreateMove hook installation failed!", Color(255, 0, 0));
    }
    else
    {
        ConPrint("ClientMode not found - CreateMove hook not installed", Color(255, 0, 0));
    }

    localPlayer = (C_BasePlayer*)ClientEntityList->GetClientEntity(EngineClient->GetLocalPlayer());

    if(Lua = LuaShared->GetLuaInterface((unsigned char)LuaInterfaceType::LUA_CLIENT))
        oRunStringEx = VMTHook< _RunStringEx>((PVOID**)Lua, (PVOID)hkRunStringEx, 111);
    oCreateLuaInterfaceFn = VMTHook<_CreateLuaInterfaceFn>((PVOID**)LuaShared, (PVOID)hkCreateLuaInterfaceFn, 4);
    oCloseLuaInterfaceFn = VMTHook<_CloseLuaInterfaceFn>((PVOID**)LuaShared, (PVOID)hkCloseInterfaceLuaFn, 5);

    // TODO: Uncomment these after fixing GetVMT offsets
    oFrameStageNotify = VMTHook< _FrameStageNotify>((PVOID**)CHLclient, hkFrameStageNotify, 35);
    if (ViewRender)
        oRenderView = VMTHook<_RenderView>((PVOID**)ViewRender, (PVOID)hkRenderView, 26);
    oPaintTraverse = VMTHook< _PaintTraverse>((PVOID**)PanelWrapper, (PVOID)hkPaintTraverse, 41);
    oDrawModelExecute = VMTHook< _DrawModelExecute>((PVOID**)ModelRender, (PVOID)hkDrawModelExecute, 20);
    oProcessGMOD_ServerToClient = VMTHook< _ProcessGMOD_ServerToClient>((PVOID**)ClientState, (PVOID)hkProcessGMOD_ServerToClient, 64);
    // oRunCommand = VMTHook<_RunCommand>((PVOID**)Prediction, (PVOID)hkRunCommand, 19);
    oPaint = VMTHook<_Paint>((PVOID**)EngineVGui, (PVOID)hkPaint, 13);

    present = (char*)findPattern(PresentModule, PresentPattern, "Present");

    Globals::damageEvent = new DamageEvent();
    Globals::deathEvent = new DeathEvent();
    GameEventManager->AddListener((IGameEventListener2*)Globals::damageEvent, "player_hurt", false);
    GameEventManager->AddListener((IGameEventListener2*)Globals::deathEvent, "entity_killed", false);

    // This can be easily detected(for instance, perphead will ban you for it), so disabled unless you need it and researched enough the server you're on.
    if (false)
    {
        ConVar* cvar = CVar->FindVar("mat_fullbright");
        cvar->RemoveFlags(FCVAR_CHEAT);

        //This'll let you change your name ingame freely
        cvar = CVar->FindVar("name");
        cvar->RemoveFlags(FCVAR_SERVER_CAN_EXECUTE);
        cvar->DisableCallback();
    }


    //GlobalVars->maxClients
    //GlobalVars + 0x14 = 1 will let u do anything lua related
    // Use MinHook for Present (similar to nixware)
    if (present)
    {
        min_hook.create_hook((LPVOID)present, (PVOID)hkPresent, (LPVOID*)&oPresent);
        min_hook.enable_hook((LPVOID)present);
    }

    //EngineClient->ClientCmd_Unrestricted("play \"items/suitchargeok1.wav\"");
        //Sleep(2200);
    Sleep(1000);
    MatSystemSurface->PlaySound("HL1/fvox/bell.wav");
    Sleep(1100);
    MatSystemSurface->PlaySound("HL1/fvox/activated.wav");
    Globals::openMenu = true;
}

BOOL APIENTRY DllMain(HMODULE hModule, uintptr_t ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
        std::thread(Main).detach();
    return TRUE;
}
