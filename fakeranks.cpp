#include <stdio.h>
#include <algorithm>
#include <iterator>
#include "fakeranks.h"
#include "utils/module.h"
#include "schemasystem/schemasystem.h"
#include "khook_helpers.h"
#include "cs2_sdk/entity/cbaseplayerpawn.h"
#include "cs2_sdk/entity/ccsplayercontroller.h"
#include "cs2_sdk/entity/cbaseplayercontroller.h"
#include <networksystem/inetworkserializer.h>
#include <networksystem/inetworkmessages.h>
#include <inetchannel.h>
#include "protobuf/generated/cstrike15_usermessages.pb.h"

class GameSessionConfiguration_t
{
};

KHOOK_VIRTUAL(GameFrame, &IServerGameDLL::GameFrame, g_pSource2Server, nullptr, Hook_GameFrame_Post);
KHOOK_VIRTUAL(StartupServer, &INetworkServerService::StartupServer, g_pNetworkServerService, nullptr, Hook_StartupServer_Post);

FakeRank_RevealAll g_FakeRanks;
PLUGIN_EXPOSE(FakeRank_RevealAll, g_FakeRanks);

CGlobalVars* g_pGlobals = nullptr;
CGameEntitySystem* g_pEntitySystem = nullptr;
IGameEventSystem* g_pGameEventSystem = nullptr;

uint64_t g_iOldButtons[ABSOLUTE_PLAYER_LIMIT]{};
static INetworkMessageInternal* g_pRankRevealMessage = nullptr;
static bool g_bPaused = false;

static void ResetButtons()
{
    std::fill(std::begin(g_iOldButtons), std::end(g_iOldButtons), 0);
}

CGlobalVars* GetGameGlobals()
{
    INetworkGameServer* srv = g_pNetworkServerService->GetIGameServer();

    if (!srv) return nullptr;

    return g_pNetworkServerService->GetIGameServer()->GetGlobals();
}

CGameEntitySystem* GameEntitySystem()
{
#ifdef WIN32
    static int offset = 88;
#else
    static int offset = 80;
#endif
    return *reinterpret_cast<CGameEntitySystem**>((uintptr_t)(g_pGameResourceServiceServer) + offset);
}

std::vector<CKHookBase*>& GetKHookList()
{
    static std::vector<CKHookBase*> s_vecSigHooks;
    return s_vecSigHooks;
}

bool FakeRank_RevealAll::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
    PLUGIN_SAVEVARS();

    GET_V_IFACE_ANY(GetServerFactory, g_pSource2Server, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkMessages, INetworkMessages, NETWORKMESSAGES_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pGameEventSystem, IGameEventSystem, GAMEEVENTSYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pGameResourceServiceServer, IGameResourceService, GAMERESOURCESERVICESERVER_INTERFACE_VERSION);

    g_pRankRevealMessage = g_pNetworkMessages->FindNetworkMessagePartial("CCSUsrMsg_ServerRankRevealAll");
    if (!g_pRankRevealMessage)
    {
        snprintf(error, maxlen, "CCSUsrMsg_ServerRankRevealAll is unavailable in this CS2 build");
        return false;
    }

    g_SMAPI->AddListener(this, this);

    // Constructing KHook::Virtual does not attach it to an engine instance.
    for (auto* hook : GetKHookList()) hook->Add();
    ResetButtons();
    g_bPaused = false;

    ConVar_Register(FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL);

    if (late)
    {
        g_pEntitySystem = GameEntitySystem();
        g_pGlobals = GetGameGlobals();
    }

    return true;
}

bool FakeRank_RevealAll::Unload(char* error, size_t maxlen)
{
    for (auto* hook : GetKHookList()) hook->Remove();
    ConVar_Unregister();
    g_pEntitySystem = nullptr;
    g_pGlobals = nullptr;
    g_pRankRevealMessage = nullptr;
    ResetButtons();
    return true;
}

KHook::Return<void> Hook_StartupServer_Post(INetworkServerService* pThis, const GameSessionConfiguration_t& config, ISource2WorldSession*, const char*)
{
    g_pEntitySystem = GameEntitySystem();
    g_pGlobals = GetGameGlobals();
    ResetButtons();
    return { KHook::Action::Ignore };
}

KHook::Return<void> Hook_GameFrame_Post(IServerGameDLL* pThis, bool simulating, bool bFirstTick, bool bLastTick)
{
    if (g_bPaused || !g_pEntitySystem || !g_pGlobals || !g_pRankRevealMessage) return { KHook::Action::Ignore };

    int maxClients = std::clamp(g_pGlobals->maxClients, 0, ABSOLUTE_PLAYER_LIMIT);
    CRecipientFilter filter;

    for (int i = 0; i < maxClients; i++)
    {
        CCSPlayerController* pPlayerController = (CCSPlayerController*)g_pEntitySystem->GetEntityInstance((CEntityIndex)(i + 1));

        if (!pPlayerController || !pPlayerController->IsConnected() || !pPlayerController->m_hPawn() || !pPlayerController->m_hPawn()->m_pMovementServices())
        {
            g_iOldButtons[i] = 0;
            continue;
        }

        uint64_t iButtons = pPlayerController->m_hPawn()->m_pMovementServices()->m_nButtons().m_pButtonStates()[0];
        if ((iButtons & PlayerButtons_t::Scoreboard) && !(g_iOldButtons[i] & PlayerButtons_t::Scoreboard))
        {
            filter.AddRecipient(CPlayerSlot(i));
        }
        g_iOldButtons[i] = iButtons;
    }

    if (filter.GetRecipientCount() > 0)
    {
        CNetMessage* msg = g_pRankRevealMessage->AllocateMessage();
        if (!msg) return { KHook::Action::Ignore };
        g_pGameEventSystem->PostEventAbstract(0, false, &filter, g_pRankRevealMessage, msg, 0);
        delete msg;
    }
    return { KHook::Action::Ignore };
}

void FakeRank_RevealAll::AllPluginsLoaded() {}

void FakeRank_RevealAll::OnLevelInit(
    char const* pMapName, char const* pMapEntities, char const* pOldLevel, char const* pLandmarkName, bool loadGame, bool background)
{
}

void FakeRank_RevealAll::OnLevelShutdown()
{
    g_pEntitySystem = nullptr;
    g_pGlobals = nullptr;
    ResetButtons();
}

bool FakeRank_RevealAll::Pause(char* error, size_t maxlen) { g_bPaused = true; ResetButtons(); return true; }

bool FakeRank_RevealAll::Unpause(char* error, size_t maxlen) { g_bPaused = false; ResetButtons(); return true; }

const char* FakeRank_RevealAll::GetLicense() { return "GPLv3"; }

const char* FakeRank_RevealAll::GetVersion() { return "1.1.4"; }

const char* FakeRank_RevealAll::GetDate() { return __DATE__; }

const char* FakeRank_RevealAll::GetLogTag() { return "FakeRanks"; }

const char* FakeRank_RevealAll::GetAuthor() { return "Cruze"; }

const char* FakeRank_RevealAll::GetDescription() { return "Reveals all fake ranks"; }

const char* FakeRank_RevealAll::GetName() { return "FakeRanks - Reveal All"; }

const char* FakeRank_RevealAll::GetURL() { return "https://github.com/koiie111/FakeRanks-RevealAll"; }
