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
#include "Log.h"
#include <algorithm>
#include <iterator>
#include <map>
#include <time.h>
#include <vector>
#include <random>

struct Config
{
    bool   enabled     = true;
    uint32 kill_goal   = 100;
    uint32 kill_points = 10;

    std::unordered_map<uint32 /* zone */, std::vector<uint32> /* areas */> ids = {{10, {93, 536}}};

    uint32 current_zone = 0;
    uint32 current_area = 0;

    std::string current_zone_name;
    std::string current_area_name;

    bool active = false;

    std::vector<Player*> area_players;
    std::vector<Player*> zone_players;

    std::map<Player*, uint32> points;

    float last_announcement = GameTime::GetGameTime().count();
    float announcement_delay = 300.0f;
    float last_event = 0;
    float event_delay = 10.0f;
    float event_lasts = 1800.0f;
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
        config.event_delay = sConfigMgr->GetOption<float>("pvp_zones.EventDelay", 10.0f);
        config.event_lasts = sConfigMgr->GetOption<float>("pvp_zones.EventLasts", 1800.0f);
        Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Config loaded: enabled=%u, kill_goal=%u, delay=%f",
                                    config.enabled ? 1 : 0, config.kill_goal, config.event_delay);
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
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Player %s entered area %u", player->GetName().c_str(), newArea);
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
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Player %s entered zone %u", player->GetName().c_str(), newZone);
        }
        else if (isPlayerInZone(player))
        {
            ChatHandler(player->GetSession()).SendSysMessage("You have left the Oceanic War cffFFFFFFblood zone!");
            config.zone_players.erase(std::remove(config.zone_players.begin(), config.zone_players.end(), player), config.zone_players.end());
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Player %s left zone %u", player->GetName().c_str(), newZone);
        }
    }

    void OnLogout(Player* player) override
    {
        config.area_players.erase(std::remove(config.area_players.begin(), config.area_players.end(), player), config.area_players.end());
        config.zone_players.erase(std::remove(config.zone_players.begin(), config.zone_players.end(), player), config.zone_players.end());
        config.points.erase(player);
        Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Player %s logged out", player->GetName().c_str());
    }

    void PostLeaderBoard(ChatHandler* handler)
    {
        handler->SendGlobalSysMessage("PvP Zones Leaderboard:");
        if (config.points.empty())
        {
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Leaderboard empty");
            return;
        }

        for (auto& player : config.points)
        {
            handler->PSendSysMessage("%s: %u", player.first->GetName().c_str(), player.second);
        }
        Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Leaderboard posted");
    }

    static void PostAnnouncement(ChatHandler* handler)
    {
        if (!config.active)
        {
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Announcement skipped: inactive");
            return;
        }
        if (config.last_announcement + config.announcement_delay <= GameTime::GetGameTime().count())
        {
            handler->PSendSysMessage("[pvp_zones] Active in: %s - %s", config.current_zone_name.c_str(), config.current_area_name.c_str());
            config.last_announcement = GameTime::GetGameTime().count();
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Announcement posted: %s - %s", config.current_zone_name.c_str(), config.current_area_name.c_str());
        }
    }

    static void CreateEvent(ChatHandler* handler)
    {
        if (config.active)
        {
            handler->PSendSysMessage("[pvp_zones] Event already active");
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "CreateEvent skipped: already active");
            return;
        }

        config.active = true;
        config.last_event = GameTime::GetGameTime().count();
        Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Event starting: time=%f", config.last_event);

        if (config.ids.empty())
        {
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_ERROR, "CreateEvent failed: no zones defined");
            config.active = false;
            return;
        }

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, config.ids.size() - 1);
        auto map_it = std::begin(config.ids);
        std::advance(map_it, dis(gen));

        if (map_it->second.empty())
        {
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_ERROR, "CreateEvent failed: no areas for zone %u", map_it->first);
            config.active = false;
            return;
        }

        std::uniform_int_distribution<> area_dis(0, map_it->second.size() - 1);
        auto area_it = std::begin(map_it->second);
        std::advance(area_it, area_dis(gen));

        config.current_zone = map_it->first;
        config.current_area = *area_it;

        uint8 locale = handler->GetSession() ? handler->GetSessionDbcLocale() : 0;
        if (AreaTableEntry const* entry = sAreaTableStore.LookupEntry(config.current_area))
        {
            config.current_area_name = entry->area_name[locale];
            if (AreaTableEntry const* z_entry = sAreaTableStore.LookupEntry(config.current_zone))
            {
                config.current_zone_name = z_entry->area_name[locale];
            }
        }

        handler->SendGlobalSysMessage(("[pvp_zones] New zone declared: " + config.current_zone_name + " - " + config.current_area_name).c_str());
        Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Event created: zone=%u (%s), area=%u (%s)",
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
        Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Event ended");
    }

    void OnPVPKill(Player* winner, Player* loser) override
    {
        Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "PvP kill: winner=%s, loser=%s, zone=%u, area=%u",
                                    winner->GetName().c_str(), loser->GetName().c_str(), winner->GetZoneId(), winner->GetAreaId());

        if (!config.active || winner->GetZoneId() != config.current_zone)
        {
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Kill ignored: inactive or wrong zone");
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
        winnerHandle.PSendSysMessage("[pvp_zones] You gained %u point(s)", pointsAwarded);
        ChatHandler(loser->GetSession()).PSendSysMessage("[pvp_zones] You lost %u point(s)", pointsAwarded);

        if (config.kill_goal <= 0)
        {
            winnerHandle.SendGlobalSysMessage("[pvp_zones] Event ended: goal reached!");
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
            { "pvp_zones_on",     HandleOnCommand,     SEC_GAMEMASTER, Acore::ChatCommands::Console::No },
            { "pvp_zones_off",    HandleOffCommand,    SEC_GAMEMASTER, Acore::ChatCommands::Console::No },
            { "pvp_zones_create", HandleCreateCommand, SEC_GAMEMASTER, Acore::ChatCommands::Console::No },
            { "pvp_zones_end",    HandleEndCommand,    SEC_GAMEMASTER, Acore::ChatCommands::Console::No },
            { "pvp_zones_debug",  HandleDebugCommand, SEC_GAMEMASTER, Acore::ChatCommands::Console::No }
        };
        return commandTable;
    }

    static bool HandleOnCommand(ChatHandler* handler)
    {
        config.enabled = true;
        handler->PSendSysMessage("PvP Zones Enabled");
        Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Enabled");
        return true;
    }

    static bool HandleOffCommand(ChatHandler* handler)
    {
        config.enabled = false;
        handler->PSendSysMessage("PvP Zones Disabled");
        Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Disabled");
        return true;
    }

    static bool HandleCreateCommand(ChatHandler* handler)
    {
        Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Command: Creating event");
        ZoneLogicScript::CreateEvent(handler);
        return true;
    }

    static bool HandleEndCommand(ChatHandler* handler)
    {
        Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Command: Ending event");
        ZoneLogicScript::EndEvent(handler);
        return true;
    }

    static bool HandleDebugCommand(ChatHandler* /* handler */)
    {
        Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Debug: active=%u, area=%s, zone=%s, last_ann=%f, last_event=%f",
                                    config.active ? 1 : 0, config.current_area_name.c_str(), config.current_zone_name.c_str(),
                                    config.last_announcement, config.last_event);
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
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "OnUpdate skipped: disabled");
            return;
        }

        // Simple static log
        Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "STATIC TEST: hello");
        // Dynamic log with one argument
        Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "TESTING UPDATE: active=%u", config.active ? 1 : 0);

        float currentTime = GameTime::GetGameTime().count();
        if (!config.active && config.last_event + config.event_delay <= currentTime)
        {
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Triggering CreateEvent at %f", currentTime);
            ChatHandler handler(nullptr);
            ZoneLogicScript::CreateEvent(&handler);
        }
        if (config.active && config.last_event + config.event_lasts <= currentTime)
        {
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Triggering EndEvent at %f", currentTime);
            ChatHandler handler(nullptr);
            ZoneLogicScript::EndEvent(&handler);
        }
        if (config.active && config.last_announcement + config.announcement_delay <= currentTime)
        {
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Posting announcement at %f", currentTime);
            ChatHandler handler(nullptr);
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