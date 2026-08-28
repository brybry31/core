/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "PlayerBotMgr.h"
#include "PlayerBotAI.h"
#include "Player.h"
#include "Log.h"
#include "SocialMgr.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "MapManager.h"
#include "MoveSpline.h"
#include "Utilities/Random.h"
#include "BotChatQueue.h"
#include "GridNotifiers.h"
#include "CellImpl.h"
#include "Group.h"
#include "Chat.h"
#include "ObjectMgr.h"
#include <ctime>

void PlayerBotAI::HandleBotChatPacket(WorldPacket const* packet)
{
    if (!me || packet->GetOpcode() != SMSG_MESSAGECHAT)
        return;

    WorldPacket copy(*packet);
    uint8 msgType;
    uint32 lang;
    ObjectGuid senderGuid;
    uint32 msgLen;
    std::string msg;
    copy >> msgType;
    copy >> lang;

    if (msgType != CHAT_MSG_SAY && msgType != CHAT_MSG_PARTY && msgType != CHAT_MSG_YELL)
        return;

    copy >> senderGuid;
    copy >> senderGuid;
    copy >> msgLen;
    copy >> msg;

    if (senderGuid == me->GetObjectGuid() || lang == LANG_ADDON)
        return;

    std::string senderName = "unknown";
    bool senderIsBot = false;
    Player* pTalker = ObjectAccessor::FindPlayer(senderGuid);
    if (pTalker)
    {
        senderName = pTalker->GetName();
        senderIsBot = pTalker->GetSession() && pTalker->GetSession()->GetBot() != nullptr;
    }

    if (senderIsBot)
        return;

    m_chatHistory.push_back(senderName + ": " + msg);
    while (m_chatHistory.size() > 10)
        m_chatHistory.pop_front();

    uint32 nowSec = (uint32)time(nullptr);
    bool namedMe = msg.find(me->GetName()) != std::string::npos;
    uint32 chance = namedMe ? 95 : (msgType == CHAT_MSG_PARTY ? 60 : 35);

    if (nowSec - m_lastChatReplyTime < 8)
        return;
    if ((uint32)(rand() % 100) >= chance)
        return;
    if (!sBotChatQueue.TryClaim(senderGuid, msg, nowSec))
        return;

    m_lastChatReplyTime = nowSec;

    std::string prompt = "You are ";
    prompt += me->GetName();
    prompt += ", a level " + std::to_string(me->GetLevel()) + " ";
    prompt += GetRaceName(me->GetRace());
    prompt += " ";
    prompt += GetClassName(me->GetClass());
    prompt += " in World of Warcraft, vanilla patch 1.12. The year is before the Burning Crusade; never mention anything from later expansions. You are talking to a player named ";
    prompt += senderName;
    if (pTalker)
    {
        prompt += ", a level " + std::to_string(pTalker->GetLevel());
        prompt += " ";
        prompt += GetRaceName(pTalker->GetRace());
        prompt += " ";
        prompt += GetClassName(pTalker->GetClass());
    }
    prompt += ".";
    prompt += BuildPersona(me);
    prompt += BuildSituation(me);

    if (m_chatHistory.size() > 1)
    {
        prompt += " Recent conversation:";
        for (auto const& line : m_chatHistory)
            prompt += "\n" + line;
        prompt += "\n";
    }
    prompt += " Reply in under 15 words, lowercase, casual, like a real player typing quickly. Do not repeat yourself. No quotation marks. They say: " + msg;

    sBotChatQueue.Enqueue(me->GetObjectGuid(), prompt, msgType);
}

std::string BuildSituation(Player* me)
{
    std::string s;

    if (char const* zone = GetZoneName(me->GetZoneId()))
    {
        s += " You are currently in ";
        s += zone;
        s += ".";
    }
    else
        printf("[ZONE] unmapped zone id %u\n", me->GetZoneId());
        
    uint32 hpPct = me->GetMaxHealth() ? (uint32)((me->GetHealth() * 100) / me->GetMaxHealth()) : 100;
    if (me->IsInCombat())
        s += " You are currently in combat and fighting.";
    else if (hpPct < 40)
        s += " You are badly wounded and resting.";
    else if (hpPct < 80)
        s += " You are a bit beaten up but fine.";

    std::list<Player*> nearby;
    MaNGOS::AnyPlayerInObjectRangeCheck check(me, 40.0f);
    MaNGOS::PlayerListSearcher<MaNGOS::AnyPlayerInObjectRangeCheck> searcher(nearby, check);
    Cell::VisitWorldObjects(me, searcher, 40.0f);

    uint32 others = 0;
    for (Player* p : nearby)
        if (p != me)
            ++others;

    if (others == 0)
        s += " Nobody else is around.";
    else if (others == 1)
        s += " There is one other person nearby.";
    else
        s += " There are " + std::to_string(others) + " other people nearby.";

    return s;
}

std::string BuildPersona(Player* me)
{
    char const* temperaments[] = {
        "gruff and impatient",
        "cheerful and chatty",
        "quiet and blunt",
        "sarcastic and dry",
        "nervous and overly polite",
        "cocky and boastful",
        "weary and world-tired",
        "friendly but easily distracted"
    };

    char const* quirks[] = {
        "You complain about your gear a lot.",
        "You are always broke and mention gold often.",
        "You think you are underrated at your class.",
        "You are obsessed with finding a good grinding spot.",
        "You bring up an old wipe you still resent.",
        "You are saving up for a mount and mention it.",
        "You dislike crowded cities.",
        "You are convinced the drop rates are rigged."
    };

    char const* styles[] = {
        "You type in short fragments and abbreviate a lot.",
        "You rarely use punctuation.",
        "You use vanilla wow slang like lf1m, wtb, oom, ty, np.",
        "You sometimes trail off mid sentence."
    };

    uint32 h = me->GetObjectGuid().GetCounter();
    std::string s = " Your personality is ";
    s += temperaments[h % 8];
    s += ". ";
    s += quirks[(h / 8) % 8];
    s += " ";
    s += styles[(h / 64) % 4];
    return s;
}

char const* GetClassName(uint8 cls)
{
    switch (cls)
    {
        case CLASS_WARRIOR: return "warrior";
        case CLASS_PALADIN: return "paladin";
        case CLASS_HUNTER: return "hunter";
        case CLASS_ROGUE: return "rogue";
        case CLASS_PRIEST: return "priest";
        case CLASS_SHAMAN: return "shaman";
        case CLASS_MAGE: return "mage";
        case CLASS_WARLOCK: return "warlock";
        case CLASS_DRUID: return "druid";
        default: return "adventurer";
    }
}


char const* GetRaceName(uint8 race)
{
    switch (race)
    {
        case RACE_HUMAN: return "human";
        case RACE_ORC: return "orc";
        case RACE_DWARF: return "dwarf";
        case RACE_NIGHTELF: return "night elf";
        case RACE_UNDEAD: return "undead forsaken";
        case RACE_TAUREN: return "tauren";
        case RACE_GNOME: return "gnome";
        case RACE_TROLL: return "troll";
        default: return "adventurer";
    }
}

char const* GetZoneName(uint32 zoneId)
{
    switch (zoneId)
    {
        // Eastern Kingdoms
        case 1:    return "Dun Morogh";
        case 3:    return "Badlands";
        case 4:    return "Blasted Lands";
        case 8:    return "Swamp of Sorrows";
        case 10:   return "Duskwood";
        case 11:   return "Wetlands";
        case 12:   return "Elwynn Forest";
        case 25:   return "Blackrock Mountain";
        case 28:   return "Western Plaguelands";
        case 33:   return "Stranglethorn Vale";
        case 36:   return "Alterac Mountains";
        case 38:   return "Loch Modan";
        case 40:   return "Westfall";
        case 41:   return "Deadwind Pass";
        case 44:   return "Redridge Mountains";
        case 45:   return "Arathi Highlands";
        case 46:   return "Burning Steppes";
        case 47:   return "The Hinterlands";
        case 51:   return "Searing Gorge";
        case 85:   return "Tirisfal Glades";
        case 87:   return "Gilneas";
        case 130:  return "Silverpine Forest";
        case 139:  return "Eastern Plaguelands";
        case 141:  return "Teldrassil";
        case 148:  return "Darkshore";
        case 267:  return "Hillsbrad Foothills";
        case 331:  return "Ashenvale";
        case 357:  return "Feralas";
        case 361:  return "Felwood";
        case 400:  return "Thousand Needles";
        case 405:  return "Desolace";
        case 406:  return "Stonetalon Mountains";
        case 440:  return "Tanaris";
        case 490:  return "Un'Goro Crater";
        case 493:  return "Moonglade";
        case 618:  return "Winterspring";
        case 1377: return "Silithus";
        case 1519: return "Stormwind City";
        case 1537: return "Ironforge";
        case 1497: return "Undercity";
        case 1637: return "Orgrimmar";
        case 1638: return "Thunder Bluff";
        case 1657: return "Darnassus";
        case 215:  return "Mulgore";
        case 14:   return "Durotar";
        case 17:   return "The Barrens";
        case 15:   return "Dustwallow Marsh";
        case 16:   return "Azshara";
        case 2597: return "Alterac Valley";
        case 3277: return "Warsong Gulch";
        case 3358: return "Arathi Basin";
        default:   return nullptr;
    }
}

bool PlayerBotAI::OnSessionLoaded(PlayerBotEntry* entry, WorldSession* sess)
{
    sess->LoginPlayer(entry->playerGUID);
    return true;
}

void PlayerBotAI::UpdateAI(uint32 const diff)
{
    UpdateBotChat();

    if (me->IsBeingTeleportedNear())
    {
        WorldPackets::Movement::MoveTeleportAck packet;
        packet.guid = me->GetObjectGuid();
#if SUPPORTED_CLIENT_BUILD > CLIENT_BUILD_1_9_4
        packet.movementCounter = 0;
#endif
        packet.time = 0;
        me->GetSession()->HandleMoveTeleportAckOpcode(packet);
    }
    if (me->IsBeingTeleportedFar())
        me->GetSession()->HandleMoveWorldportAck();
}

void PlayerBotAI::Remove()
{
    if (me)
    {
        if (me->AI() == this)
            me->SetAI(nullptr);
        me = nullptr;
    }
}

void PlayerBotFleeingAI::OnPlayerLogin()
{
    me->GetMotionMaster()->MoveFleeing(me);
    me->SetCheatGod(true);
}

// MageOrgrimmarAttackerAI event
enum
{
    SPELL_FROST_NOVA = 122,
    SPELL_FIREBOLT = 133,
    AURA_REGEN_MANA = 430,
};


bool PlayerBotAI::SpawnNewPlayer(WorldSession* sess, uint8 class_, uint32 race_, uint32 mapId, uint32 instanceId, float x, float y, float z, float o, Player* pClone)
{
    ASSERT(botEntry);
    std::string name = sObjectMgr.GenerateFreePlayerName();
    normalizePlayerName(name);

    uint8 gender;
    uint8 skin;
    uint8 face;
    uint8 hairStyle;
    uint8 hairColor;
    uint8 facialHair;

    if (pClone)
    {
        gender = pClone->GetByteValue(UNIT_FIELD_BYTES_0, UNIT_BYTES_0_OFFSET_GENDER);
        skin = pClone->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_SKIN_ID);
        face = pClone->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_FACE_ID);
        hairStyle = pClone->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_HAIR_STYLE_ID);
        hairColor = pClone->GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_HAIR_COLOR_ID);
        facialHair = pClone->GetByteValue(PLAYER_BYTES_2, PLAYER_BYTES_2_OFFSET_FACIAL_STYLE);
    }
    else
    {
        gender = urand(0, 1);
        Player::SelectRandomAppearance(race_, gender, hairStyle, hairColor, face, facialHair, skin);
    }

    Player* newChar = new Player(sess);
    uint32 guid = botEntry->playerGUID;
    if (!newChar->Create(guid, name, race_, class_, gender, skin, face, hairStyle, hairColor, facialHair))
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "PlayerBotAI::SpawnNewPlayer: Unable to create a player!");
        delete newChar;
        return false;
    }
    newChar->SetLocationMapId(mapId);
    newChar->SetLocationInstanceId(instanceId);
    newChar->SetAutoInstanceSwitch(false);
    newChar->GetMotionMaster()->Initialize();
    // Set instance
    if (instanceId && mapId > 1) // Not a continent
    {
        DungeonPersistentState* state = (DungeonPersistentState*)sMapPersistentStateMgr
                .AddPersistentState(sMapStorage.LookupEntry<MapEntry>(mapId), instanceId, time(nullptr) + 3600, false, true);
        newChar->BindToInstance(state, true, true);
    }
    // Generate position
    Map* map = sMapMgr.FindMap(mapId, instanceId);
    if (!map)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "PlayerBotAI::SpawnNewPlayer: Map (%u, %u) not found!", mapId, instanceId);
        delete newChar;
        return false;
    }
    newChar->Relocate(x, y, z, o);
    newChar->SetMap(map);
    newChar->SaveRecallPosition();
    newChar->CreatePacketBroadcaster();
    MasterPlayer* mPlayer = new MasterPlayer(sess);
    mPlayer->LoadPlayer(newChar);
    mPlayer->SetSocial(sSocialMgr.LoadFromDB(nullptr, newChar->GetObjectGuid()));
    if (!newChar->GetMap()->Add(newChar))
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "PlayerBotAI::SpawnNewPlayer: Unable to add player to map!");
        delete newChar;
        return false;
    }
    sObjectMgr.InsertPlayerInCache(newChar);
    sess->SetPlayer(newChar);
    sess->SetMasterPlayer(mPlayer);
    sObjectAccessor.AddObject(newChar);
    newChar->SetCanModifyStats(true);
    newChar->UpdateAllStats();
    return true;
}
bool MageOrgrimmarAttackerAI::OnSessionLoaded(PlayerBotEntry* entry, WorldSession* sess)
{
    return SpawnNewPlayer(sess, CLASS_MAGE, RACE_GNOME, 1, 0, 1017.0f, -4450, 12, 0.65f);
}

void MageOrgrimmarAttackerAI::UpdateAI(uint32 const diff)
{
    PlayerBotAI::UpdateAI(diff);
    if (me->GetLevel() != 60)
        me->GiveLevel(60);
    // DEATH
    if (!me->IsAlive())
    {
        sPlayerBotMgr.DeleteBot(me->GetGUIDLow());
        /*
        if (me->GetDeathState() < CORPSE)
            return;
        if (me->GetDeathState() == CORPSE && me->GetDeathTimer() && me->GetDeathTimer() < (6 * MINUTE * IN_MILLISECONDS - 30000))
        {
            me->SetHealth(1);
            me->RepopAtGraveyard();
        }
        else if (me->GetDeathState() == CORPSE && !me->GetDeathTimer())
        {
            me->ResurrectPlayer(0.5f);
            me->SpawnCorpseBones();
        }
        */
        return;
    }
    // COMBAT AI
    if (me->IsNonMeleeSpellCasted(false) || (me->HasAura(AURA_REGEN_MANA) && me->GetPower(POWER_MANA) != me->GetMaxPower(POWER_MANA)))
        return;
    float range = me->IsInCombat() ? 30.0f : frand(15, 30);
    Unit* target = me->SelectNearestTarget(range);
    if (target && !me->IsWithinLOSInMap(target))
        target = nullptr;
    // OOM ?
    if (me->GetPower(POWER_MANA) < 40 && target && me->IsInCombat())
    {
        if (me->Attack(target, true))
            me->GetMotionMaster()->MoveChase(target);
        return;
    }
    // Stop chase if has mana
    if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
        me->GetMotionMaster()->MovementExpired();
    bool nearTarget = target && target->CanReachWithMeleeAutoAttack(me);
    if (me->IsSpellReady(sSpellMgr.GetSpellEntry(SPELL_FROST_NOVA)) && me->GetPower(POWER_MANA) > 50)
        if (nearTarget)
            me->CastSpell(me, SPELL_FROST_NOVA, false);
    if (nearTarget && target->HasUnitState(UNIT_STATE_CAN_NOT_MOVE))
    {
        // already runing
        if (!me->movespline->Finalized())
            return;
        // Try to kit
        float x, y, z;
        me->GetPosition(x, y, z);
        float d = me->GetDistance(target);
        d += me->GetObjectBoundingRadius();
        d += target->GetObjectBoundingRadius();
        x += (x - target->GetPositionX()) * 5.0f / d;
        y += (y - target->GetPositionY()) * 5.0f / d;
        me->UpdateGroundPositionZ(x, y, z);
        me->GetMotionMaster()->MovePoint(0, x, y, z, MOVE_PATHFINDING | MOVE_EXCLUDE_STEEP_SLOPES);
        return;
    }

    if (target && me->GetPower(POWER_MANA) > 50)
    {
        uint32 spellId = SPELL_FIREBOLT;
        me->SetFacingToObject(target);
        if (!me->movespline->Finalized())
            me->StopMoving();

        /*float z = me->GetPositionZ();
        me->UpdateGroundPositionZ(me->GetPositionX(), me->GetPositionY(), z);
        me->Relocate(me->GetPositionX(), me->GetPositionY(), z);
        me->m_movementInfo.moveFlags = 0;
        me->SendHeartBeat();*/

        me->CastSpell(target, spellId, false);
        return;
    }
    // OUT OF COMBAT REGEN
    if (!me->IsInCombat() && me->GetPower(POWER_MANA) < 150)
    {
        if (!me->movespline->Finalized())
            me->StopMoving();
        me->CastSpell(target, AURA_REGEN_MANA, false);
        return;
    }
    // MOVEMENT AI

    // Target pos (where to go)
    float x = 0;
    float y = 0;
    float z = 0;

    float r = 10;
    if (me->movespline->Finalized())
    {
        if (me->GetPositionX() < 1000.0f)
        {
            x = 1176;
            y = -4404;
        }
        else if (me->GetPositionX() + 10 < 1176.0f)
        {
            x = 1176;
            y = -4404;
        }
        else if (me->GetPositionX() + 10 < 1357.0f)
        {
            switch (urand(0, 2))
            {
                case 0:
                    x = 1357;
                    y = -4376;
                    break;
                case 1:
                    x = 1354;
                    y = -4412;
                    break;
                case 2:
                    x = 1346;
                    y = -4339;
                    break;
            }
        }
        else if (me->GetPositionX() + 10 < 1421.0f)
        {
            // Porte orgri
            x = 1427;
            y = -4362;
            z = 25.0f;
            r = 4;
        }
        else
        {
            switch (urand(0, 2))
            {
                case 0:
                    x = 1516;
                    y = -4410;
                    z = 17.0f;
                    r = 4;
                    break;
                case 1:
                    x = 1538;
                    y = -4347;
                    z = 18;
                    r = 3;
                    break;
                case 2:
                    x = 1617;
                    y = -4426;
                    z = 12;
                    r = 4;
                    break;
            }
        }
        if (!z)
        {
            z = me->GetPositionZ();
            me->UpdateGroundPositionZ(x, y, z);
        }
        r = 20;
        if (!me->GetMap()->GetWalkRandomPosition(nullptr, x, y, z, r))
            return;
    }
    else
    {
        return;
        if (urand(0, 20) == 0) // random move
        {
            me->GetPosition(x, y, z);
            r = frand(0, 2);
            float angle = me->GetOrientation() + frand(-M_PI_F / 2, M_PI_F / 2);
            x += r * cos(angle);
            y += r * sin(angle);
            if (!me->GetMap()->GetWalkHitPosition(nullptr, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), x, y, z))
                return;
        }
        else
            return;
    }
    me->GetMotionMaster()->MovePoint(0, x, y, z, MOVE_PATHFINDING | MOVE_EXCLUDE_STEEP_SLOPES);
}

void PopulateAreaBotAI::BeforeAddToMap(Player* player)
{
    if (player->GetInstanceId() || player->GetTeam() != _team)
        return;
    if (player->GetMapId() != _map || !player->IsWithinDist3d(_x, _y, _z, _radius * 2))
    {
        float x = _x;
        float y = _y;
        float z = _z;
        Map* map = sMapMgr.CreateMap(_map, player);
        while (!map->GetWalkRandomPosition(nullptr, x, y, z, _radius));
        player->Relocate(x, y, z);
        player->SetLocationMapId(_map);
    }
}

void PopulateAreaBotAI::OnPlayerLogin()
{
    if (urand(0, 1))
        me->GetMotionMaster()->MoveConfused();
}

PlayerBotAI* CreatePlayerBotAI(std::string ainame)
{
    if (ainame == "MageOrgrimmarAttackerAI")
        return new MageOrgrimmarAttackerAI();
    if (ainame == "IronforgePopulationAI")
        return new PopulateAreaBotAI(0, -4928.5f, -946.6f, 501.6f, ALLIANCE, 100.0f);
    if (ainame == "StormwindPopulationAI")
        return new PopulateAreaBotAI(0, -8829.5f, 625.6f, 93.9f, ALLIANCE, 50.0f);
    if (ainame == "OrgrimmarPopulationAI")
        return new PopulateAreaBotAI(1, 1568, -4405.87f, 8.13f, HORDE, 150.0f);
    if (ainame == "PlayerBotFleeingAI")
        return new PlayerBotFleeingAI();
    return new PlayerBotAI();
}

void PlayerBotAI::UpdateBotChat()
{
    if (!me || !me->IsInWorld())
        return;

    BotChatReply chatReply;
    while (sBotChatQueue.PopReply(chatReply))
    {
        if (Player* pBot = ObjectAccessor::FindPlayer(chatReply.botGuid))
        {
            if (chatReply.chatType == CHAT_MSG_PARTY && pBot->GetGroup())
            {
                WorldPacket data;
                ChatHandler::BuildChatPacket(data, CHAT_MSG_PARTY, chatReply.text.c_str(), LANG_UNIVERSAL, 0, pBot->GetObjectGuid());
                pBot->GetGroup()->BroadcastPacket(&data, false);
            }
            else
                pBot->Say(chatReply.text.c_str(), LANG_UNIVERSAL);

            m_chatHistory.push_back(std::string(pBot->GetName()) + ": " + chatReply.text);
            while (m_chatHistory.size() > 10)
                m_chatHistory.pop_front();
        }
    }
}