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
#include "Corpse.h"
#include "LootMgr.h"
#include "GameObject.h"
#include <algorithm>
#include <iterator>
#include <map>
#include <time.h>
#include <vector>
#include <random>
#include <string>

struct Config
{
    bool   enabled     = true;
    uint32 kill_goal   = 100;
    uint32 kill_points = 10;
    uint32 loot_item_id = 49426; // Emblem of Frost
    uint32 loot_item_count = 1;
    uint32 chest_id = 179697; // Chest GameObject ID
    uint32 chest_despawn = 120; // Chest despawn time in seconds

    std::unordered_map<uint32 /* zone */, std::vector<uint32> /* areas */> ids = {{267, {272}}}; // Tarren Mill

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
        config.loot_item_id = sConfigMgr->GetOption<uint32>("pvp_zones.LootItemId", 49426);
        config.loot_item_count = sConfigMgr->GetOption<uint32>("pvp_zones.LootItemCount", 1);
        config.chest_id = sConfigMgr->GetOption<uint32>("pvp_zones.ChestID", 179697);
        config.chest_despawn = sConfigMgr->GetOption<uint32>("pvp_zones.ChestTimer", 120);
        std::string msg = "Config loaded: enabled=" + std::to_string(config.enabled ? 1 : 0) +
                          ", kill_goal=" + std::to_string(config.kill_goal) +
                          ", delay=" + std::to_string(config.event_delay) +
                          ", loot_item=" + std::to_string(config.loot_item_id) +
                          ", loot_count=" + std::to_string(config.loot_item_count) +
                          ", chest_id=" + std::to_string(config.chest_id) +
                          ", chest_despawn=" + std::to_string(config.chest_despawn);
        Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, msg.c_str());
    }
};

class ZoneLogicScript : public PlayerScript, WorldScript
{
public:
    ZoneLogicScript() : PlayerScript("pvp_zones_PlayerScript"), WorldScript("pvp_zones_WorldScript") {}

    void OnUpdateArea(Player* player, uint32 /*oldArea*/, uint32 newArea) override
    {
        if (config.current_area == newArea)
        {
            ChatHandler(player->GetSession()).SendSysMessage("You have entered the Oceanic War cffFFFFFFblood zone!");
            config.area_players.push_back(player);
            std::string msg = "Player " + player->GetName() + " entered area " + std::to_string(newArea);
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, msg.c_str());
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

    void OnUpdateZone(Player* player, uint32 newZone, uint32 /*newArea*/) override
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
            std::string msg = "Player " + player->GetName() + " entered zone " + std::to_string(newZone);
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, msg.c_str());
        }
        else if (isPlayerInZone(player))
        {
            ChatHandler(player->GetSession()).SendSysMessage("You have left the Oceanic War cffFFFFFFblood zone!");
            config.zone_players.erase(std::remove(config.zone_players.begin(), config.zone_players.end(), player), config.zone_players.end());
            std::string msg = "Player " + player->GetName() + " left zone " + std::to_string(newZone);
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, msg.c_str());
        }
    }

    void OnLogout(Player* player) override
    {
        config.area_players.erase(std::remove(config.area_players.begin(), config.area_players.end(), player), config.area_players.end());
        config.zone_players.erase(std::remove(config.zone_players.begin(), config.zone_players.end(), player), config.zone_players.end());
        config.points.erase(player);
        std::string msg = "Player " + player->GetName() + " logged out";
        Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, msg.c_str());
    }

    void OnPVPKill(Player* winner, Player* loser) override
    {
        std::string killMsg = "PvP kill: winner=" + winner->GetName() + ", loser=" + loser->GetName() + ", zone=" + std::to_string(winner->GetZoneId()) + ", area=" + std::to_string(winner->GetAreaId());
        Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, killMsg.c_str());

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

        // Spawn chest at loser's position
        if (GameObject* chest = winner->SummonGameObject(config.chest_id, loser->GetPositionX(), loser->GetPositionY(), loser->GetPositionZ(), loser->GetOrientation(), 0.0f, 0.0f, 0.0f, 0.0f, config.chest_despawn, false))
        {
            winner->AddGameObject(chest);
            chest->SetOwnerGUID(ObjectGuid::Empty); // Allow anyone to loot

            // Add Emblem of Frost
            LootStoreItem emblemLoot(config.loot_item_id, false, 100.0f, false, 1, 0, config.loot_item_count, config.loot_item_count);
            chest->loot.AddItem(emblemLoot);
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Emblem of Frost added to chest: " + std::to_string(config.loot_item_id));

            // Select and add random gear
            std::vector<uint32> equippedItems;
            for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            {
                if (Item* item = loser->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                {
                    equippedItems.push_back(item->GetEntry());
                }
            }

            uint32 randomGearId = 0;
            if (!equippedItems.empty())
            {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> dis(0, equippedItems.size() - 1);
                randomGearId = equippedItems[dis(gen)];
            }
            else
            {
                randomGearId = 25; // Fallback to Copper Ore
            }

            LootStoreItem gearLoot(randomGearId, false, 100.0f, false, 1, 0, 1, 1);
            chest->loot.AddItem(gearLoot);
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Gear item added to chest: Item " + std::to_string(randomGearId));

            // Debug chest loot state
            std::string lootContents = "Chest loot contents: ";
            for (const auto& item : chest->loot.items)
            {
                lootContents += std::to_string(item.itemid) + " (count: " + std::to_string(item.count) + ") ";
            }
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, lootContents.c_str());
        }
        else
        {
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_ERROR, "Failed to spawn chest");
        }

        ChatHandler(winner->GetSession()).PSendSysMessage("[pvp_zones] You gained %u point(s) and loot! Check the chest!", pointsAwarded);
        ChatHandler(loser->GetSession()).PSendSysMessage("[pvp_zones] You lost %u point(s) and a piece of gear!", pointsAwarded);

        config.kill_goal--;
        if (config.kill_goal <= 0)
        {
            ChatHandler handler(winner->GetSession());
            handler.SendGlobalSysMessage("[pvp_zones] Event ended: goal reached!");
            EndEvent(&handler);
        }

        if (config.kill_goal % 5 == 0)
        {
            ChatHandler handler(winner->GetSession());
            PostLeaderBoard(&handler);
        }
    }

    void OnPlayerReleasedGhost(Player* /*player*/) override
    {
        // No longer needed for loot setup
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
        std::string startMsg = "Event starting: time=" + std::to_string(config.last_event);
        Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, startMsg.c_str());

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
            std::string errMsg = "CreateEvent failed: no areas for zone " + std::to_string(map_it->first);
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, errMsg.c_str());
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
        std::string createMsg = "Event created: zone=" + std::to_string(config.current_zone) + " (" + config.current_zone_name + "), area=" + std::to_string(config.current_area) + " (" + config.current_area_name + ")";
        Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, createMsg.c_str());

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
            std::string msg = "Announcement posted: " + config.current_zone_name + " - " + config.current_area_name;
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, msg.c_str());
        }
    }

private:
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
            std::string msg = player.first->GetName() + ": " + std::to_string(player.second);
            handler->PSendSysMessage(msg.c_str());
        }
        Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "Leaderboard posted");
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

    static bool HandleDebugCommand(ChatHandler* /*handler*/)
    {
        std::string debugMsg = "Debug: active=" + std::to_string(config.active ? 1 : 0) +
                               ", area=" + config.current_area_name +
                               ", zone=" + config.current_zone_name +
                               ", last_ann=" + std::to_string(config.last_announcement) +
                               ", last_event=" + std::to_string(config.last_event);
        Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, debugMsg.c_str());
        return true;
    }
};

class ZoneWorld : public WorldScript
{
public:
    ZoneWorld() : WorldScript("pvp_zones_World") {}

    void OnUpdate(uint32 /*p_time*/) override
    {
        if (!config.enabled)
        {
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, "OnUpdate skipped: disabled");
            return;
        }

        float currentTime = GameTime::GetGameTime().count();

        if (!config.active && config.last_event + config.event_delay <= currentTime)
        {
            std::string createMsg = "Triggering CreateEvent at " + std::to_string(currentTime);
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, createMsg.c_str());
            ChatHandler handler(nullptr);
            ZoneLogicScript::CreateEvent(&handler);
        }
        if (config.active && config.last_event + config.event_lasts <= currentTime)
        {
            std::string endMsg = "Triggering EndEvent at " + std::to_string(currentTime);
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, endMsg.c_str());
            ChatHandler handler(nullptr);
            ZoneLogicScript::EndEvent(&handler);
        }
        if (config.active && config.last_announcement + config.announcement_delay <= currentTime)
        {
            std::string annMsg = "Posting announcement at " + std::to_string(currentTime);
            Log::instance()->outMessage("module", LogLevel::LOG_LEVEL_INFO, annMsg.c_str());
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