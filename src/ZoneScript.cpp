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
            LOG_INFO("module", "[pvp_zones] Player %s entered area %u