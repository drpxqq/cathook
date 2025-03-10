
/*
 * HTrigger.cpp
 *
 *  Created on: Oct 5, 2016
 *      Author: nullifiedcat
 */

#include <hacks/Trigger.hpp>
#include "common.hpp"
#include <PlayerTools.hpp>
#include <settings/Bool.hpp>
#include "Backtrack.hpp"

namespace hacks::triggerbot
{
static settings::Boolean enable{ "trigger.enable", "false" };
static settings::Int hitbox_mode{ "trigger.hitbox-mode", "0" };
static settings::Int accuracy{ "trigger.accuracy", "1" };
static settings::Boolean wait_for_charge{ "trigger.wait-for-charge", "false" };
static settings::Boolean zoomed_only{ "trigger.zoomed-only", "true" };
static settings::Float delay{ "trigger.delay", "0" };

static settings::Button trigger_key{ "trigger.key.button", "<null>" };
static settings::Int trigger_key_mode{ "trigger.key.mode", "1" };
// FIXME move these into targeting
static settings::Boolean ignore_cloak{ "trigger.target.ignore-cloaked-spies", "true" };
static settings::Boolean ignore_vaccinator{ "trigger.target.ignore-vaccinator", "true" };
static settings::Boolean buildings_sentry{ "trigger.target.buildings-sentry", "true" };
static settings::Boolean buildings_other{ "trigger.target.buildings-other", "true" };
static settings::Boolean stickybot{ "trigger.target.stickybombs", "false" };
static settings::Boolean teammates{ "trigger.target.teammates", "false" };
static settings::Float max_range{ "trigger.target.max-range", "4096" };

// Vars for usersettings

float target_time = 0.0f;

int last_hb_traced = 0;
Vector forward;

// Func to find value of how far to target ents
inline float EffectiveTargetingRange()
{
    if (GetWeaponMode() == weapon_melee)
        return re::C_TFWeaponBaseMelee::GetSwingRange(RAW_ENT(LOCAL_W));
    // Pyros only have so much until their flames hit
    else if (LOCAL_W->m_iClassID() == CL_CLASS(CTFFlameThrower))
        return 300.0f;
    // If user has set a max range, then use their setting,
    if (max_range)
        return *max_range;
    // else use a pre-set range
    else
        return 8012.0f;
}

// The main function of the triggerbot
static void CreateMove()
{
    float backup_time = target_time;
    target_time       = 0;

    // Check if trigerbot is enabled, weapon is valid and if player can aim
    if (!enable || CE_BAD(LOCAL_W) || !ShouldShoot())
        return;

    // Reset our last hitbox traced
    last_hb_traced = -1;

    // Get an ent in front of the player
    CachedEntity *ent = nullptr;

    bool state_good = false;

    bool shouldBacktrack = backtrack::backtrackEnabled() && !hacks::backtrack::hasData();

    if (shouldBacktrack)
    {
        float target_range = EffectiveTargetingRange();
        for (const auto &ent_data : backtrack::bt_data)
        {
            if (state_good)
                break;
            for (auto &tick_data : ent_data)
            {
                if (!tick_data.in_range)
                    continue;
                backtrack::MoveToTick(tick_data);
                ent = FindEntInSight(target_range);
                // Restore the data
                backtrack::RestoreEntity(tick_data.entidx);
                if (ent)
                {
                    state_good = IsTargetStateGood(ent, tick_data);
                    if (state_good)
                        break;
                    else
                        backtrack::RestoreEntity(tick_data.entidx);
                }
            }
        }
    }
    else
    {
        ent = FindEntInSight(EffectiveTargetingRange());
    }

    // Check if dormant or null to prevent crashes
    if (CE_BAD(ent))
        return;

    if (!shouldBacktrack)
        state_good = IsTargetStateGood(ent, std::nullopt);

    // Determine whether the triggerbot should shoot, then act accordingly
    if (state_good)
    {
        target_time = backup_time;
        if (delay)
        {
            if (target_time > g_GlobalVars->curtime)
                target_time = 0.0f;

            if (!target_time)
                target_time = g_GlobalVars->curtime;
            else
            {
                if (g_GlobalVars->curtime - float(delay) >= target_time)
                {
                    current_user_cmd->buttons |= IN_ATTACK;
                    *bSendPackets = true;
                }
            }
        }
        else
        {
            current_user_cmd->buttons |= IN_ATTACK;
            *bSendPackets = true;
        }
    }
}

// The first check to see if the player should shoot in the first place
bool ShouldShoot()
{
    // Check for +use
    if (current_user_cmd->buttons & IN_USE)
        return false;

    // Check if using action slot item
    if (g_pLocalPlayer->using_action_slot_item)
        return false;

    // Check the aimkey to determine if it should shoot
    if (!UpdateAimkey())
        return false;

    // Check if Carrying A building
    if (CE_BYTE(LOCAL_E, netvar.m_bCarryingObject))
        return false;

    // Check if deadringer out
    if (CE_BYTE(LOCAL_E, netvar.m_bFeignDeathReady))
        return false;

    // If zoomed only is on, check if zoomed
    if (*zoomed_only && g_pLocalPlayer->holding_sniper_rifle && !g_pLocalPlayer->bZoomed && !(current_user_cmd->buttons & IN_ATTACK))
        return false;

    // Check if player is bonked
    if (HasCondition<TFCond_Bonked>(LOCAL_E))
        return false;

    // Check if player is taunting
    if (HasCondition<TFCond_Taunting>(LOCAL_E))
        return false;

    // Check if player is cloaked
    if (IsPlayerInvisible(LOCAL_E))
        return false;

    if (IsAmbassador(LOCAL_W) && !AmbassadorCanHeadshot())
        return false;

    switch (GetWeaponMode())
    {
    case weapon_hitscan:
    case weapon_melee:
        break;
    // Check if player is using a projectile based weapon
    case weapon_projectile:
    // Check if player doesn't have a weapon usable by aimbot
    default:
        return false;
    }

    // Check if player is zooming, not already attacking, and cannot headshot
    if (g_pLocalPlayer->bZoomed && !(current_user_cmd->buttons & (IN_ATTACK | IN_ATTACK2)) && !CanHeadshot())
        return false;

    return true;
}

// A second check to determine whether a target is good enough to be aimed at
bool IsTargetStateGood(CachedEntity *entity, std::optional<backtrack::BacktrackData> bt_data)
{
    if (bt_data)
        backtrack::MoveToTick(*bt_data);
    // Check for Players
    if (entity->m_Type() == ENTITY_PLAYER)
    {
        // Check if target is The local player
        if (entity == LOCAL_E)
            return false;
        // Don't aim at dead player
        if (!entity->m_bAlivePlayer())
            return false;
        // Don't aim at teammates
        if (!entity->m_bEnemy() && !teammates)
            return false;

        // Global checks
        if (!player_tools::shouldTarget(entity))
            return false;

        // If settings allow waiting for charge, and current charge can't kill target, don't aim
        if (*wait_for_charge && g_pLocalPlayer->holding_sniper_rifle)
        {
            float bdmg = CE_FLOAT(LOCAL_W, netvar.flChargedDamage);
            if (g_GlobalVars->curtime - g_pLocalPlayer->flZoomBegin <= 1.0f)
                bdmg = 50.0f;
            //                if ((bdmg * 3) < (HasDarwins(entity)
            //                                      ? (entity->m_iHealth() *
            //                                      1.15)
            //                                      : entity->m_iHealth()))
            if (bdmg * 3 < entity->m_iHealth())
                return false;
        }
        // Don't target invulnerable players, ex: uber, bonk
        if (IsPlayerInvulnerable(entity))
            return false;
        // If settings allow, don't target cloaked players
        if (ignore_cloak && IsPlayerInvisible(entity))
            return false;
        // If settings allow, don't target vaccinated players
        if (ignore_vaccinator && IsPlayerResistantToCurrentWeapon(entity))
            return false;

        // Head hitbox detection
        if (HeadPreferable(entity))
        {
            if (last_hb_traced != hitbox_t::head)
                return false;
        }

        // If usersettings tell us to use accuracy improvements and the cached hitbox isn't null, then we check if it hits here
        if (*accuracy)
        {
            // Get a cached hitbox for the one traced
            hitbox_cache::CachedHitbox *hb = entity->hitboxes.GetHitbox(last_hb_traced);
            // Check for null
            if (hb)
            {
                // Get the min and max for the hitbox
                Vector minz(fminf(hb->min.x, hb->max.x), fminf(hb->min.y, hb->max.y), fminf(hb->min.z, hb->max.z));
                Vector maxz(fmaxf(hb->min.x, hb->max.x), fmaxf(hb->min.y, hb->max.y), fmaxf(hb->min.z, hb->max.z));

                // Shrink the hitbox here
                Vector size = maxz - minz;
                Vector smod = size * 0.05f * *accuracy;

                // Save the changes to the vectors
                minz += smod;
                maxz -= smod;

                // Trace and test if it hits the smaller hitbox, if it fails we return false
                Vector hit;
                if (!CheckLineBox(minz, maxz, g_pLocalPlayer->v_Eye, forward, hit))
                    return false;
            }
        }
        // Target passed the tests so return true
        return true;
    }
    // Check for buildings
    else if (entity->m_Type() == ENTITY_BUILDING)
    {
        // Check if building aimbot is enabled
        if (!(buildings_other || buildings_sentry))
            return false;
        // Check if enemy building
        if (!entity->m_bEnemy())
            return false;

        // If needed, Check if building type is allowed
        if (!(buildings_other && buildings_sentry))
        {
            // Check if target is a sentrygun
            if (entity->m_iClassID() == CL_CLASS(CObjectSentrygun))
            {
                // If sentries are not allowed, don't target
                if (!buildings_sentry)
                    return false;
            }
            else
            {
                // If target is not a sentry, check if other buildings are
                // allowed
                if (!buildings_other)
                    return false;
            }
        }

        // Target passed the tests so return true
        return true;

        // Check for stickybombs
    }
    else if (entity->m_iClassID() == CL_CLASS(CTFGrenadePipebombProjectile))
    {
        // Check if sticky aimbot is enabled
        if (!stickybot)
            return false;

        // Check if thrower is a teammate
        if (!entity->m_bEnemy())
            return false;

        // Check if target is a pipe bomb
        if (CE_INT(entity, netvar.iPipeType) != 1)
            return false;

        // Target passed the tests so return true
        return true;
    }
    else
    {
        // If target is not player, building or sticky, return false
        return false;
    }
}

// A function to return a potential entity in front of the player
CachedEntity *FindEntInSight(float range, bool no_players)
{
    // We don't want to hit ourselves, so we set as ignored
    trace_t trace;
    trace::filter_default.SetSelf(RAW_ENT(LOCAL_E));

    // Get Forward vector
    forward = GetForwardVector(range, LOCAL_E);

    // Set up the trace starting with the origin of the local players eyes attempting to hit the end vector we determined
    Ray_t ray;
    ray.Init(g_pLocalPlayer->v_Eye, forward);

    // Ray trace
    g_ITrace->TraceRay(ray, 0x4200400B, &trace::filter_default, &trace);

    // Return an ent if that is what we hit
    if (trace.DidHit() && trace.m_pEnt && (IClientEntity *) trace.m_pEnt != g_IEntityList->GetClientEntity(0))
    {
        last_hb_traced    = trace.hitbox;
        CachedEntity *ent = ENTITY(((IClientEntity *) trace.m_pEnt)->entindex());
        // Player check
        if (!no_players || ent->m_Type() != ENTITY_PLAYER)
            return ent;
    }

    // Since we didn't hit and entity, the vis check failed so return 0
    return nullptr;
}

// A function to find whether the head should be used for a target
bool HeadPreferable(CachedEntity *target)
{

    // Switch based on the priority type we need
    switch (*hitbox_mode)
    {
    case 0:
    { // AUTO-HEAD priority
        // Var to keep if we can bodyshot
        bool headonly = false;
        // If user is using a sniper rifle, Set headonly to whether we can
        // headshot or not,
        if (g_pLocalPlayer->holding_sniper_rifle)
        {
            headonly = CanHeadshot();
            // If player is using an ambassador, set headonly to true
        }
        else if (IsAmbassador(LOCAL_W))
        {
            headonly = true;
        }
        // Bodyshot handling
        if (g_pLocalPlayer->holding_sniper_rifle)
        {
            // Some keeper vars
            float cdmg, bdmg;
            // Grab netvar for current charge damage
            cdmg = CE_FLOAT(LOCAL_W, netvar.flChargedDamage);
            // Set our baseline bodyshot damage
            bdmg = 50;

            // Vaccinator damage correction
            if (HasCondition<TFCond_UberBulletResist>(target))
            {
                // Vac charge protects against 75% of damage
                bdmg = (bdmg * .25) - 1;
                cdmg = (cdmg * .25) - 1;
            }
            else if (HasCondition<TFCond_SmallBulletResist>(target))
            {
                // Passive bullet resist protects against 10% of damage
                bdmg = (bdmg * .90) - 1;
                cdmg = (cdmg * .90) - 1;
            }
            // Invis damage correction
            if (IsPlayerInvisible(target))
            {
                // Invis spies get protection from 10% of damage
                bdmg = (bdmg * .80) - 1;
                cdmg = (cdmg * .80) - 1;
            }
            // If can headshot and if bodyshot kill from charge damage, or
            // if crit boosted, and they have 150 health, or if player isn't
            // zoomed, or if the enemy has less than 40, due to darwins, and
            // only if they have less than 150 health will it try to bodyshot
            if (CanHeadshot() && (cdmg >= target->m_iHealth() || IsPlayerCritBoosted(LOCAL_E) || !g_pLocalPlayer->bZoomed || target->m_iHealth() <= bdmg) && target->m_iHealth() <= 150)
            {
                // We dont need to hit the head as a bodyshot will kill
                headonly = false;
            }
        }

        // Return our var of if we need to headshot
        return headonly;
    }
    case 1:
    { // AUTO-CLOSEST priority
        // We don't need the head so just use anything
        return false;
    }
    case 2:
    { // Head only
        // User wants the head only
        return true;
    }
    }
    // We don't know what the user wants so just use anything
    return false;
}

// A function that determines whether aimkey allows aiming
bool UpdateAimkey()
{
    static bool trigger_key_flip  = false;
    static bool pressed_last_tick = false;
    bool allow_trigger_key        = true;
    // Check if aimkey is used
    if (trigger_key && trigger_key_mode)
    {
        // Grab whether the aimkey is depressed
        bool key_down = trigger_key.isKeyDown();
        // Switch based on the user set aimkey mode
        switch ((int) trigger_key_mode)
        {
        // Only while key is depressed, enable
        case 1:
            if (!key_down)
                allow_trigger_key = false;
            break;
        // Only while key is not depressed, enable
        case 2:
            if (key_down)
                allow_trigger_key = false;
            break;
        // Aimkey acts like a toggle switch
        case 3:
            if (!pressed_last_tick && key_down)
                trigger_key_flip = !trigger_key_flip;
            if (!trigger_key_flip)
                allow_trigger_key = false;
        }
        pressed_last_tick = key_down;
    }
    // Return whether the aimkey allows aiming
    return allow_trigger_key;
}

// Helper functions to trace for hitboxes

// TEMPORARY CODE.
// TODO
bool GetIntersection(float fDst1, float fDst2, Vector P1, Vector P2, Vector &Hit)
{
    if ((fDst1 * fDst2) >= 0.0f)
        return false;
    if (fDst1 == fDst2)
        return false;
    Hit = P1 + (P2 - P1) * (-fDst1 / (fDst2 - fDst1));
    return true;
}

bool InBox(Vector Hit, Vector B1, Vector B2, int Axis)
{
    if (Axis == 1 && Hit.z > B1.z && Hit.z < B2.z && Hit.y > B1.y && Hit.y < B2.y)
        return true;
    if (Axis == 2 && Hit.z > B1.z && Hit.z < B2.z && Hit.x > B1.x && Hit.x < B2.x)
        return true;
    if (Axis == 3 && Hit.x > B1.x && Hit.x < B2.x && Hit.y > B1.y && Hit.y < B2.y)
        return true;
    return false;
}

bool CheckLineBox(Vector B1, Vector B2, Vector L1, Vector L2, Vector &Hit)
{
    if (L2.x < B1.x && L1.x < B1.x)
        return false;
    if (L2.x > B2.x && L1.x > B2.x)
        return false;
    if (L2.y < B1.y && L1.y < B1.y)
        return false;
    if (L2.y > B2.y && L1.y > B2.y)
        return false;
    if (L2.z < B1.z && L1.z < B1.z)
        return false;
    if (L2.z > B2.z && L1.z > B2.z)
        return false;
    if (L1.x > B1.x && L1.x < B2.x && L1.y > B1.y && L1.y < B2.y && L1.z > B1.z && L1.z < B2.z)
    {
        Hit = L1;
        return true;
    }
    if ((GetIntersection(L1.x - B1.x, L2.x - B1.x, L1, L2, Hit) && InBox(Hit, B1, B2, 1)) || (GetIntersection(L1.y - B1.y, L2.y - B1.y, L1, L2, Hit) && InBox(Hit, B1, B2, 2)) || (GetIntersection(L1.z - B1.z, L2.z - B1.z, L1, L2, Hit) && InBox(Hit, B1, B2, 3)) || (GetIntersection(L1.x - B2.x, L2.x - B2.x, L1, L2, Hit) && InBox(Hit, B1, B2, 1)) || (GetIntersection(L1.y - B2.y, L2.y - B2.y, L1, L2, Hit) && InBox(Hit, B1, B2, 2)) || (GetIntersection(L1.z - B2.z, L2.z - B2.z, L1, L2, Hit) && InBox(Hit, B1, B2, 3)))
        return true;

    return false;
}

void Draw()
{
}

static InitRoutine EC(
    []()
    {
        EC::Register(EC::CreateMove, CreateMove, "triggerbot", EC::average);
        EC::Register(EC::CreateMoveWarp, CreateMove, "triggerbot_w", EC::average);
    });
} // namespace hacks::triggerbot
