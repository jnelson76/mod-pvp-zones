#include "Chat.h"
#include "Common.h"
#include "Config.h"
#include "DBCStores.h"
#include "DBCStructure.h"
#include "Define.h"
#include "GameTime.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Log.h" // For logging
#include <algorithm>
#include <iterator>
#include <map>
#include <time.h>
#include <vector>
#include <random>

// Hardcoded until GetOption vector is implemented
struct Config
{
    bool   enabled     = true;
    uint32 kill_goal   = 100;  // Default value
    uint32 kill_points = 10;   // Default value

    std::unordered_map<uint32 /* zone */, std::vector<uint32> /* areas */> ids = {{10, {93, 536}}};

    /* non .conf stuff */
    uint32 current_zone = 0;
    uint32 current_area = 0;

    std::string current_zone_name;
    std::string current_area_name;

    bool active = false;

    std::vector<Player*> area_players;
    std::vector<Player*> zone_players;

    std::map<Player*, uint32> points;

    float last_announcement = GameTime::GetGameTime().count();
    float announcement_delay = 300.0f; // 5 minutes default
    float last_event = 0;
    float event_delay = 3600.0f;       // 1 hour default
    float event_lasts = 1800.0f;       // 30 minutes default
};

Config config;

class ZoneConfig : public WorldScript
{
public:
    ZoneConfig() : WorldScript("pvp_zones_Config") {}

    void OnStartup() override
    {
        config.enabled = sConfigMgr->GetOption<bool>("pvp_zones.Enable", true);
        config.kill_goal = sConfigMgr->GetOption<uint32>("pvp_zones.KillGoal", 100);
        config.announcement_delay = sConfigMgr->GetOption<float>("pvp_zones.AnnouncementDelay", 300.0f);
        config.kill_points = sConfigMgr->GetOption<uint32>("pvp_zones.KillPoints", 10);
        config.event_delay = sConfigMgr->GetOption<float>("pvp_zones.EventDelay", 3600.0f);
        config.event_lasts = sConfigMgr->GetOption<float>("pvp_zones.EventLasts", 1800.0f);
        LOG_INFO("module", "[pvp_zones] Config loaded: enabled=%u, kill_goal=%u, announcement_delay=%f, event_delay=%f",
                 config.enabled, config.kill_goal, config.announcement_delay, config.event_delay);
    }
};

class ZoneLogicScript : public PlayerScript, WorldScript
{
public:
    ZoneLogicScript() : PlayerScript("pvp_zones_PlayerScript"), WorldScript("pvp_zones_WorldScript") {}

    void OnUpdateArea(Player* player, uint32 /* oldArea */, uint32 newArea) override
    {
        if (config.current_area == newArea)
        {
            ChatHandler(player->GetSession()).SendSysMessage("You have entered the Oceanic War cffFFFFFFblood zone!");
            config.area_players.push_back(player);
            LOG_INFO("module", "[pvp_zones] Player %s entered area %u", player->GetName().c_str(), newArea);
        }
        else
        {
            config.area_players.erase(std::remove(config.area_players.begin(), config.area_players.end(), player), config.area_players.end());
        }
    }

    static bool isPlayerInZone(Player* player)
    {
        return std::find(config.zone_players.begin(), config.zone_players.end(), player) != config.zone_players.end();
    }

    void OnPlayerPVPFlagChange(Player* player, bool state) override
    {
        if (isPlayerInZone(player) && !state)
        {
            player->SetPvP(true);
        }
    }

    void OnUpdateZone(Player* player, uint32 newZone, uint32 /* new area */) override
    {
        if (config.current_zone == newZone)
        {
            if (isPlayerInZone(player))
            {
                return;
            }
            ChatHandler(player->GetSession()).SendSysMessage("You have entered the Oceanic War cffFFFFFFblood zone!");
            config.zone_players.push_back(player);
            player->UpdatePvP(true, true);
            LOG_INFO("module", "[pvp_zones] Player %s entered zone %u", player->GetName().c_str(), newZone);
        }
        else if (isPlayerInZone(player))
        {
            ChatHandler(player->GetSession()).SendSysMessage("You have left the Oceanic War cffFFFFFFblood zone!");
            config.zone_players.erase(std::remove(config.zone_players.begin(), config.zone_players.end(), player), config.zone_players.end());
            LOG_INFO("module", "[pvp_zones] Player %s left zone %u", player->GetName().c_str(), newZone);
        }
    }

    void OnLogout(Player* player) override
    {
        config.area_players.erase(std::remove(config.area_players.begin(), config.area_players.end(), player), config.area_players.end());
        config.zone_players.erase(std::remove(config.zone_players.begin(), config.zone_players.end(), player), config.zone_players.end());
        config.points.erase(player);
        LOG_INFO("module", "[pvp_zones] Player %s logged out, removed from tracking", player->GetName().c_str());
    }

    void PostLeaderBoard(ChatHandler* handler)
    {
        handler->SendGlobalSysMessage("PvP Zones Leaderboard:");
        if (config.points.empty())
        {
            LOG_INFO("module", "[pvp_zones] Leaderboard empty");
            return;
        }

        for (auto& player : config.points)
        {
            handler->PSendSysMessage("%s: %u", player.first->GetName().c_str(), player.second);
        }
        LOG_INFO("module", "[pvp_zones] Leaderboard posted");
    }

    static void PostAnnouncement(ChatHandler* handler)
    {
        if (!config.active)
        {
            LOG_INFO("module", "[pvp_zones] Announcement skipped: event inactive");
            return;
        }
        if (config.last_announcement + config.announcement_delay < GameTime::GetGameTime().count())
        {
            handler->PSendSysMessage("[pvp_zones] Is currently active in: %s - %s", config.current_zone_name.c_str(), config.current_area_name.c_str());
            config.last_announcement = GameTime::GetGameTime().count();
            LOG_INFO("module", "[pvp_zones] Announcement posted: %s - %s", config.current_zone_name.c_str(), config.current_area_name.c_str());
        }
    }

    static void CreateEvent(ChatHandler* handler)
    {
        if (config.active)
        {
            handler->PSendSysMessage("[pvp_zones] Event already active");
            LOG_INFO("module", "[pvp_zones] CreateEvent skipped: already active");
            return;
        }

        config.active = true;
        config.last_event = GameTime::GetGameTime().count();
        LOG_INFO("module", "[pvp_zones] Event starting: last_event=%f", config.last_event);

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, config.ids.size() - 1);
        auto map_it = std::begin(config.ids);
        std::advance(map_it, dis(gen));

        std::uniform_int_distribution<> area_dis(0, map_it->second.size() - 1);
        auto area_it = std::begin(map_it->second);
        std::advance(area_it, area_dis(gen));

        config.current_zone = map_it->first;
        config.current_area = *area_it;

        if (AreaTableEntry const* entry = sAreaTableStore.LookupEntry(config.current_area))
        {
            config.current_area_name = entry->area_name[handler->GetSessionDbcLocale()];
            if (AreaTableEntry const* z_entry = sAreaTableStore.LookupEntry(config.current_zone))
            {
                config.current_zone_name = z_entry->area_name[handler->GetSessionDbcLocale()];
            }
        }

        handler->SendGlobalSysMessage(("[pvp_zones] A new zone has been declared: " + config.current_zone_name + " - " + config.current_area_name).c_str());
        LOG_INFO("module", "[pvp_zones] Event created: zone=%u (%s), area=%u (%s)",
                 config.current_zone, config.current_zone_name.c_str(), config.current_area, config.current_area_name.c_str());

        auto players = ObjectAccessor::GetPlayers();
        for (auto& player : players)
        {
            if (player.second->GetZoneId() == config.current_zone)
            {
                player.second->SetPvP(true);
                ChatHandler(player.second->GetSession()).SendSysMessage("You have entered the Oceanic War cffFFFFFFblood zone!");
                config.zone_players.push_back(player.second);
            }
            if (player.second->GetAreaId() == config.current_area)
            {
                config.area_players.push_back(player.second);
            }
        }
    }

    static void EndEvent(ChatHandler* handler)
    {
        config.active = false;
        handler->SendGlobalSysMessage("[pvp_zones] The event has ended.");
        config.points.clear();
        config.area_players.clear();
        config.zone_players.clear();
        LOG_INFO("module", "[pvp_zones] Event ended, state reset");
    }

    void OnPVPKill(Player* winner, Player* loser) override
    {
        LOG_INFO("module", "[pvp_zones] PvP kill: winner=%s, loser=%s, zone=%u, area=%u",
                 winner->GetName().c_str(), loser->GetName().c_str(), winner->GetZoneId(), winner->GetAreaId());

        if (!config.active || winner->GetZoneId() != config.current_zone)
        {
            LOG_INFO("module", "[pvp_zones] Kill ignored: event inactive or wrong zone");
            return;
        }

        uint32 pointsAwarded = config.kill_points;
        if (winner->GetAreaId() == config.current_area)
        {
            pointsAwarded *= 2;
        }

        config.points[winner] = config.points[winner] + pointsAwarded;
        if (config.points[loser] >= pointsAwarded)
        {
            config.points[loser] -= pointsAwarded;
        }
        else
        {
            config.points[loser] = 0;
        }

        config.kill_goal--;
        ChatHandler winnerHandle(winner->GetSession());
        winnerHandle.PSendSysMessage("[pvp_zones] You have gained %u PvP point(s)", pointsAwarded);
        ChatHandler(loser->GetSession()).PSendSysMessage("[pvp_zones] You have lost %u PvP point(s)", pointsAwarded);

        if (config.kill_goal <= 0)
        {
            winnerHandle.SendGlobalSysMessage("[pvp_zones] The goal has been reached!");
            EndEvent(&winnerHandle);
        }

        if (config.kill_goal % 5 == 0)
        {
            PostLeaderBoard(&winnerHandle);
        }
    }
};

class ZoneCommands : public CommandScript
{
public:
    ZoneCommands() : CommandScript("pvp_zones_Commands") {}

    Acore::ChatCommands::ChatCommandTable GetCommands() const override
    {
        static Acore::ChatCommands::ChatCommandTable commandTable =
        {
            { "pvp_zones_on",     HandleOnCommand,     SEC_GAMEMASTER, Console::No },
            { "pvp_zones_off",    HandleOffCommand,    SEC_GAMEMASTER, Console::No },
            { "pvp_zones_create", HandleCreateCommand, SEC_GAMEMASTER, Console::No },
            { "pvp_zones_end",    HandleEndCommand,    SEC_GAMEMASTER, Console::No },
            { "pvp_zones_debug",  HandleDebugCommand,  SEC_GAMEMASTER, Console::No }
        };
        return commandTable;
    }

    static bool HandleOnCommand(ChatHandler* handler)
    {
        config.enabled = true;
        handler->PSendSysMessage("PvP Zones Enabled");
        LOG_INFO("module", "[pvp_zones] Command: Enabled PvP Zones");
        return true;
    }

    static bool HandleOffCommand(ChatHandler* handler)
    {
        config.enabled = false;
        handler->PSendSysMessage("PvP Zones Disabled");
        LOG_INFO("module", "[pvp_zones] Command: Disabled PvP Zones");
        return true;
    }

    static bool HandleCreateCommand(ChatHandler* handler)
    {
        LOG_INFO("module", "[pvp_zones] Command: Creating event");
        ZoneLogicScript::CreateEvent(handler);
        return true;
    }

    static bool HandleEndCommand(ChatHandler* handler)
    {
        LOG_INFO("module", "[pvp_zones] Command: Ending event");
        ZoneLogicScript::EndEvent(handler);
        return true;
    }

    static bool HandleDebugCommand(ChatHandler* /* handler */)
    {
        LOG_INFO("module", "[pvp_zones] Debug: active=%u, area_name=%s, zone_name=%s, last_announcement=%f, last_event=%f, next_announcement=%fs",
                 config.active, config.current_area_name.c_str(), config.current_zone_name.c_str(),
                 config.last_announcement, config.last_event,
                 (config.last_announcement + config.announcement_delay) - GameTime::GetGameTime().count());
        return true;
    }
};

class ZoneWorld : public WorldScript
{
public:
    ZoneWorld() : WorldScript("pvp_zones_World") {}

    void OnUpdate(uint32 /* p_time */) override
    {
        if (!config.enabled)
        {
            LOG_INFO("module", "[pvp_zones] OnUpdate skipped: module disabled");
            return;
        }

        LOG_INFO("module", "[pvp_zones] OnUpdate running: active=%u, current_zone=%u, current_area=%u",
                 config.active, config.current_zone, config.current_area);

        float currentTime = GameTime::GetGameTime().count();
        if (!config.active && config.last_event + config.event_delay < currentTime)
        {
            LOG_INFO("module", "[pvp_zones] Triggering CreateEvent");
            ChatHandler handler(sWorld->GetDefaultChatHandler());
            ZoneLogicScript::CreateEvent(&handler);
        }
        if (config.active && config.last_event + config.event_lasts < currentTime)
        {
            LOG_INFO("module", "[pvp_zones] Triggering EndEvent");
            ChatHandler handler(sWorld->GetDefaultChatHandler());
            ZoneLogicScript::EndEvent(&handler);
        }
        if (config.last_announcement + config.announcement_delay <= currentTime)
        {
            LOG_INFO("module", "[pvp_zones] Posting announcement");
            ChatHandler handler(sWorld->GetDefaultChatHandler());
            ZoneLogicScript::PostAnnouncement(&handler);
        }
    }
};

void Addpvp_zonesScripts()
{
    new ZoneWorld();
    new ZoneConfig();
    new ZoneLogicScript();
    new ZoneCommands();
}