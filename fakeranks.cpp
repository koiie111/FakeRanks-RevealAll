#include <stdio.h>
#include <algorithm>
#include <iterator>
#include <cstdlib>
#include <cstring>
#include "fakeranks.h"
#include "utils/module.h"
#include "schemasystem/schemasystem.h"
#include "khook_helpers.h"
#include "utils/deferred_lookup.h"
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
static DeferredLookup<INetworkMessageInternal> g_RankRevealMessage;
static bool g_bWarnedMissingMessage = false;
static unsigned long long g_nFrames = 0, g_nScoreboardPresses = 0, g_nRevealRecipients = 0;
static bool g_bPaused = false;

static void ResetButtons()
{
    std::fill(std::begin(g_iOldButtons), std::end(g_iOldButtons), 0);
}

static void ResetMessage()
{
    g_RankRevealMessage.Reset();
    g_bWarnedMissingMessage = false;
}

static INetworkMessageInternal* ResolveRankRevealMessage()
{
    auto* message = g_RankRevealMessage.Resolve(Plat_FloatTime(), []() -> INetworkMessageInternal* {
        // ID lookup avoids depending on the registry's name prefix.
        auto* found = g_pNetworkMessages->FindNetworkMessageById(CS_UM_ServerRankRevealAll);
        if (!found) found = g_pNetworkMessages->FindNetworkMessagePartial("ServerRankRevealAll");
        if (found)
        {
            const char* name = found->GetUnscopedName();
            // Do not send a different message if Valve ever reuses ID 350.
            if (!name || !std::strstr(name, "ServerRankRevealAll")) return nullptr;
            Msg("[FakeRanks] Rank reveal message ready: %s (lookup attempts: %u)\n", name, g_RankRevealMessage.Attempts());
        }
        return found;
    });
    if (!message && !g_bWarnedMissingMessage && g_RankRevealMessage.Attempts() >= 10)
    {
        Warning("[FakeRanks] Rank reveal message not registered yet (ID 350/name lookup). Retrying; run fakeranks_status for diagnostics.\n");
        g_bWarnedMissingMessage = true;
    }
    return message;
}

static bool SendRankReveal(CRecipientFilter& filter)
{
    auto* descriptor = ResolveRankRevealMessage();
    if (!descriptor || filter.GetRecipientCount() == 0) return false;
    CNetMessage* message = descriptor->AllocateMessage();
    if (!message) return false;
    g_pGameEventSystem->PostEventAbstract(0, false, &filter, descriptor, message, 0);
    delete message;
    g_nRevealRecipients += filter.GetRecipientCount();
    return true;
}

CON_COMMAND_F(fakeranks_status, "Print FakeRanks runtime diagnostics (server console only)", FCVAR_GAMEDLL)
{
    if (context.GetPlayerSlot().Get() != -1) return;
    Msg("[FakeRanks] version=1.1.6 paused=%d globals=%d entities=%d message=%s attempts=%u frames=%llu tab_presses=%llu sent_recipients=%llu\n",
        g_bPaused, g_pGlobals != nullptr, g_pEntitySystem != nullptr,
        g_RankRevealMessage.Get() ? "ready" : "waiting", g_RankRevealMessage.Attempts(),
        g_nFrames, g_nScoreboardPresses, g_nRevealRecipients);
}

CON_COMMAND_F(fakeranks_reveal, "Send rank reveal to a connected player: fakeranks_reveal <slot 0-based> (server console only)", FCVAR_GAMEDLL)
{
    if (context.GetPlayerSlot().Get() != -1) return;
    if (args.ArgC() != 2 || !g_pGlobals || !g_pEntitySystem)
    {
        Msg("[FakeRanks] Usage on a running map: fakeranks_reveal <slot 0-based>\n");
        return;
    }
    char* end = nullptr;
    long slot = std::strtol(args[1], &end, 10);
    if (end == args[1] || *end || slot < 0 || slot >= std::min(g_pGlobals->maxClients, ABSOLUTE_PLAYER_LIMIT))
    {
        Msg("[FakeRanks] Invalid player slot.\n");
        return;
    }
    auto* controller = static_cast<CCSPlayerController*>(g_pEntitySystem->GetEntityInstance(CEntityIndex(slot + 1)));
    if (!controller || !controller->IsConnected())
    {
        Msg("[FakeRanks] No connected player in this slot.\n");
        return;
    }
    CRecipientFilter filter;
    filter.AddRecipient(CPlayerSlot(slot));
    Msg("[FakeRanks] Manual reveal for slot %ld: %s\n", slot, SendRankReveal(filter) ? "posted" : "message unavailable");
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
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);

    // User messages may only be registered after Load(). Resolve from GameFrame.
    ResetMessage();
    g_nFrames = g_nScoreboardPresses = g_nRevealRecipients = 0;

    g_SMAPI->AddListener(this, this);

    // Constructing KHook::Virtual does not attach it to an engine instance.
    for (auto* hook : GetKHookList()) hook->Add();
    ResetButtons();
    g_bPaused = false;

    ConVar_Register(FCVAR_RELEASE | FCVAR_GAMEDLL);

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
    ResetMessage();
    ResetButtons();
    return true;
}

KHook::Return<void> Hook_StartupServer_Post(INetworkServerService* pThis, const GameSessionConfiguration_t& config, ISource2WorldSession*, const char*)
{
    g_pEntitySystem = GameEntitySystem();
    g_pGlobals = GetGameGlobals();
    ResetButtons();
    ResetMessage();
    return { KHook::Action::Ignore };
}

KHook::Return<void> Hook_GameFrame_Post(IServerGameDLL* pThis, bool simulating, bool bFirstTick, bool bLastTick)
{
    ++g_nFrames;
    if (g_bPaused || !g_pEntitySystem || !g_pGlobals) return { KHook::Action::Ignore };
    // Preserve upstream's polling cadence: inspect TAB every twelfth tick.
    if (g_pGlobals->tickcount % 12 != 0) return { KHook::Action::Ignore };
    const bool wasReady = g_RankRevealMessage.Get() != nullptr;
    const bool messageReady = ResolveRankRevealMessage() != nullptr;
    // A held scoreboard should be revealed when the registry becomes ready.
    if (messageReady && !wasReady) ResetButtons();

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
            ++g_nScoreboardPresses;
            if (messageReady) filter.AddRecipient(CPlayerSlot(i));
        }
        g_iOldButtons[i] = iButtons;
    }

    if (filter.GetRecipientCount() > 0)
    {
        SendRankReveal(filter);
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
    ResetMessage();
}

bool FakeRank_RevealAll::Pause(char* error, size_t maxlen) { g_bPaused = true; ResetButtons(); return true; }

bool FakeRank_RevealAll::Unpause(char* error, size_t maxlen) { g_bPaused = false; ResetButtons(); return true; }

const char* FakeRank_RevealAll::GetLicense() { return "GPLv3"; }

const char* FakeRank_RevealAll::GetVersion() { return "1.1.6"; }

const char* FakeRank_RevealAll::GetDate() { return __DATE__; }

const char* FakeRank_RevealAll::GetLogTag() { return "FakeRanks"; }

const char* FakeRank_RevealAll::GetAuthor() { return "Cruze"; }

const char* FakeRank_RevealAll::GetDescription() { return "Reveals all fake ranks"; }

const char* FakeRank_RevealAll::GetName() { return "FakeRanks - Reveal All"; }

const char* FakeRank_RevealAll::GetURL() { return "https://github.com/koiie111/FakeRanks-RevealAll"; }
