/**
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
 *
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
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

// Doing what a step says.
//
// WHAT IS HERE AND WHAT IS NOT, and the line between them is deliberate.
//
// The 26 verbs EventAI had and the DB scripts did not -- phases, threat,
// instance data, the movement switches -- are implemented here, natively.
// They have to be: there is no old body to borrow, EventAI's implementation of
// them being a switch inside its interpreter rather than anything callable.
//
// The 47 the DB scripts had still go to ScriptAction, and will until there is
// something that would catch a mistake in rewriting them. The differential
// test compares TIMING -- same step, same order, same tick -- and would not
// notice a rewritten body casting the wrong spell. Rewriting forty-seven
// effects with no check on their effects is how a migration acquires a bug
// nobody can date.
//
// A verb returns true when the sequence should stop there.

#include "MaiPerform.h"

#include "MaiTargeting.h"

#include "Cell.h"
#include "CellImpl.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "InstanceData.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Spell.h"
#include "Unit.h"

namespace mai
{
    Unit* Doing::SourceUnit() const
    {
        return source ? source->ToUnit() : nullptr;
    }

    Creature* Doing::SourceCreature() const
    {
        return source ? source->ToCreature() : nullptr;
    }

    Unit* Doing::TargetUnit() const
    {
        return target ? target->ToUnit() : nullptr;
    }

    namespace
    {
        /// An operand the step gave, or @a fallback when it did not. The
        /// distinction matters: `inc_phase by=0` and `inc_phase` are not the
        /// same instruction, and only one of them is a mistake.
        uint32 Given(Step const& step, std::size_t slot, uint32 fallback = 0)
        {
            return step.Has(slot) ? step.operands[slot].u : fallback;
        }

        float GivenF(Step const& step, std::size_t slot, float fallback = 0.0f)
        {
            return step.Has(slot) ? step.operands[slot].f : fallback;
        }

        /**
         * The AI to tell, when the step is still acting as its own creature.
         *
         * There is exactly one AI a verb can reach -- the one whose rule this
         * step belongs to -- and a step that was redirected at a buddy or told
         * to act AS somebody else is no longer about that creature. Telling it
         * anyway would stop the boss moving because his add was told to.
         *
         * `ruleOwner` is the source as it stood before any of the four flags
         * or either selector moved it, so the two being the same object IS the
         * question "is this still about me".
         */
        Driver* DriverFor(Doing const& doing)
        {
            return (doing.driver && doing.source &&
                    doing.source == doing.ruleOwner)
                       ? doing.driver
                       : nullptr;
        }

        // ---- phases ---------------------------------------------------------

        bool SetPhase(Doing& doing, Step const& step)
        {
            if (doing.actor)
            {
                doing.actor->phases.current = Given(step, 0) & 31u;
            }
            return false;
        }

        bool IncPhase(Doing& doing, Step const& step)
        {
            if (!doing.actor)
            {
                return false;
            }

            // Signed, and clamped rather than wrapped. EventAI let a phase
            // walk off either end and the result was a creature in a phase no
            // rule mentions -- which looks exactly like a creature that has
            // stopped working, and is impossible to tell from one.
            int32 const by = step.Has(0) ? step.operands[0].i : 1;
            int32 const now = int32(doing.actor->phases.current) + by;
            doing.actor->phases.current = uint32(now < 0 ? 0
                                                         : (now > 31 ? 31 : now));
            return false;
        }

        bool RandomPhase(Doing& doing, Step const& step)
        {
            if (!doing.actor)
            {
                return false;
            }

            uint32 choices[3];
            uint32 count = 0;
            for (std::size_t slot = 0; slot < 3; ++slot)
            {
                if (step.Has(slot))
                {
                    choices[count++] = step.operands[slot].u;
                }
            }

            if (count)
            {
                doing.actor->phases.current = choices[urand(0, count - 1)] & 31u;
            }
            return false;
        }

        bool RandomPhaseRange(Doing& doing, Step const& step)
        {
            if (!doing.actor)
            {
                return false;
            }

            uint32 const least = Given(step, 0);
            uint32 const most = Given(step, 1);

            // An inverted range is the row's mistake, not a reason to pick
            // one end of it silently: the original swapped them, and so does
            // this, because a phase is still a phase either way round.
            uint32 const low = least < most ? least : most;
            uint32 const high = least < most ? most : least;

            doing.actor->phases.current = urand(low, high) & 31u;
            return false;
        }

        // ---- one of three ---------------------------------------------------

        /// The slots that were actually given, which is what "one of three"
        /// has to choose between. A row with two sounds must not pick the
        /// third an eighth of the time and play silence.
        uint32 OneOfThree(Step const& step, bool& any)
        {
            uint32 choices[3];
            uint32 count = 0;
            for (std::size_t slot = 0; slot < 3; ++slot)
            {
                if (step.Has(slot))
                {
                    choices[count++] = step.operands[slot].u;
                }
            }

            any = count != 0;
            return any ? choices[urand(0, count - 1)] : 0;
        }

        bool RandomSound(Doing& doing, Step const& step)
        {
            bool any = false;
            uint32 const sound = OneOfThree(step, any);

            if (any && doing.source)
            {
                doing.source->PlayDirectSound(sound);
            }
            return false;
        }

        bool RandomEmote(Doing& doing, Step const& step)
        {
            bool any = false;
            uint32 const emote = OneOfThree(step, any);

            if (any)
            {
                if (Unit* self = doing.SourceUnit())
                {
                    self->HandleEmote(emote);
                }
            }
            return false;
        }

        /**
         * Casting, the way an AI casts.
         *
         * THE FIRST BORROWED BODY TO BE REPLACED, and it earns it: 10,512 of
         * the 27,561 converted steps are this verb, and every one of them came
         * from an EventAI row that called DoCastSpellIfCan. The DB-script body
         * they were falling through to calls Unit::CastSpell outright, and the
         * two differ in ways that show up as a boss behaving oddly rather than
         * as anything failing:
         *
         *   * DoCastSpellIfCan REFUSES while the creature is already casting
         *     something non-triggered. The raw call does not, so a creature
         *     with three timers coming due in one scan tries three casts and
         *     interrupts itself twice.
         *   * it runs CanCastSpell first -- range, line of sight, silence,
         *     immunity -- and the raw call leaves all of that to the spell
         *     system, which fails later and differently.
         *   * it needs no target to refuse gracefully; the borrowed body logs
         *     a database error every time one is missing.
         *
         * The flags operand is the CAST_* vocabulary, unchanged, because that
         * is what EventAI's own column held. `command_additional` on the step
         * means triggered, which is how the DB scripts spelled the same thing.
         */
        bool CastSpell(Doing& doing, Step const& step, bool& handled)
        {
            Creature* self = doing.SourceCreature();
            Unit* victim = doing.TargetUnit();

            uint32 flags = Given(step, 1);
            if (step.buddy.flags & CommandAdditional)
            {
                flags |= CAST_TRIGGERED;
            }

            // Cast BY the source and credited TO the rule's owner, when the
            // step says so. They are the same unit unless a buddy, a summon or
            // a selector moved the acting away from the deciding.
            ObjectGuid credited;
            if (Given(step, 2) && doing.ruleOwner)
            {
                credited = doing.ruleOwner->GetObjectGuid();
            }

            // Only a creature with an AI casts the AI way -- DoCastSpellIfCan
            // asks whether it MAY, which is the whole reason MAI took this
            // verb over. A sequence the world started may have a game object
            // or a player as its source, and those fall through to the
            // borrowed body exactly as before.
            //
            // With ONE exception, and it is the reason this check moved below
            // the credit: the borrowed body cannot pass an original caster. A
            // step that asked for one and whose source is a plain unit --
            // "the player you just killed casts the mark on himself, and it is
            // YOURS" -- would silently lose the attribution, which is the
            // whole of what the step was for.
            if (!self || !self->AI())
            {
                Unit* caster = doing.SourceUnit();
                if (!caster || credited.IsEmpty())
                {
                    handled = false;
                    return false;
                }

                caster->CastSpell(victim ? victim : caster, Given(step, 0),
                                  (flags & CAST_TRIGGERED) != 0, nullptr,
                                  nullptr, credited);
                return false;
            }

            CanCastResult const result =
                self->AI()->DoCastSpellIfCan(victim ? victim : self,
                                             Given(step, 0), flags, credited);

            // Refused, not failed. Already casting, out of range, silenced,
            // the target immune -- all of them mean "not now" rather than
            // "never", and a rule with a retry wants to know.
            if (result == CAST_OK)
            {
                return false;
            }

            if (doing.refused)
            {
                *doing.refused = true;
            }

            // And when the step says so, nothing after it happens. Every
            // ability in ScriptDev that announces itself is written
            //
            //     if (DoCastSpellIfCan(...) == CAST_OK) { DoScriptText(...); }
            //
            // because a boss who says his line and then does nothing is worse
            // than a silent one: the raid is told to move out of something
            // that is not there.
            return Given(step, 3) != 0;
        }

        /**
         * Arrive at the target, instantly.
         *
         * A near-teleport rather than a movement: the target's own position,
         * the caster's own facing. That is what Shazzrah's Gate does, and what
         * the ScriptDev script wrote by hand under a comment reading
         * TODO REMOVE HACK -- because the spell's dummy effect had nowhere to
         * live. It has somewhere now: the effect reaches MAI as a sequence
         * keyed on the spell, and this is the one verb that sequence needed.
         */
        bool TeleportToTarget(Doing& doing, Step const& step)
        {
            (void)step;

            Unit* self = doing.SourceUnit();
            Unit* victim = doing.TargetUnit();

            if (!self || !victim || self == victim)
            {
                sLog.outErrorDb("MAI: teleport_to_target needs a source and a "
                                "different target");
                return false;
            }

            self->NearTeleportTo(victim->Where().X(), victim->Where().Y(),
                                 victim->Where().Z(), self->Where().Facing());
            return false;
        }

        /**
         * Tell ONE creature something.
         *
         * throw_ai_event shouts to everyone in a radius; this speaks to whoever
         * the step's buddy search picked. Garr telling a single add to detonate
         * is not the same instruction as telling every add within thirty yards
         * to detonate, and only one of those is a fight.
         *
         * The value travels with it, so the receiving rule can be written
         * against it: "explode with THIS spell" is a number the sender chooses
         * rather than something the receiver has to infer from who asked.
         */
        bool SendAiEvent(Doing& doing, Step const& step)
        {
            Creature* self = doing.SourceCreature();
            Creature* receiver = doing.target ? doing.target->ToCreature()
                                              : nullptr;

            if (!self || !self->AI() || !receiver)
            {
                sLog.outErrorDb("MAI: send_ai_event needs a creature to send "
                                "and a creature to send to");
                return false;
            }

            self->AI()->SendAIEvent(AIEventType(Given(step, 0)), receiver,
                                    receiver, Given(step, 1));
            return false;
        }

        /**
         * Immune to something, or no longer.
         *
         * Jandice Barov's illusions are made immune to magic damage the moment
         * they are summoned, and that is the whole trick of the fight: they
         * cannot be AoE'd down, so they have to be found. Without it she is a
         * different boss, which is why she was the one refusal this round that
         * was NOT one flag away.
         */
        bool SetImmunity(Doing& doing, Step const& step)
        {
            Unit* self = doing.SourceUnit();
            if (!self)
            {
                sLog.outErrorDb("MAI: set_immunity has no unit to apply to");
                return false;
            }

            // `apply` defaults to true: a step that mentions an immunity and
            // says nothing else means to grant it.
            bool const apply = !step.Has(2) || Given(step, 2) != 0;

            self->ApplySpellImmune(0, Given(step, 0), Given(step, 1), apply);
            return false;
        }

        /**
         * Keep meaning this one.
         *
         * The other half of `select=11`. A focus, a mark and a chain are all
         * "pick one, then keep meaning that one", and until now MAI could pick
         * and could not keep: every step chose again from scratch, so three
         * beats of the same ability would land on three different players.
         */
        bool RememberTarget(Doing& doing, Step const& step)
        {
            (void)step;

            if (!doing.actor)
            {
                return false;
            }

            doing.actor->remembered = doing.target ? doing.target->GetObjectGuid()
                                                   : ObjectGuid();
            return false;
        }

        /**
         * Summon at the target's feet.
         *
         * The missing member of the summon family, exactly as
         * teleport_to_target was of the movement one. `temp_summon_creature`
         * takes a written position, and the borrowed body reads it straight
         * out of the row -- so summoning where somebody is standing was not
         * expressible at all, only summoning where somebody stood when the
         * script was written.
         */
        /**
         * Summon at a written position, scattered.
         *
         * NATIVE ONLY WHEN `scatter` IS WRITTEN, and that is deliberate
         * caution rather than indecision: 27,561 converted steps go through
         * the borrowed body, which passes two flags this cannot see -- a run
         * flag off the row's data_flags and a fourth argument off its first
         * text id. Reproducing those from a Step that does not carry them
         * would be a guess. A step that asks for scatter is a step written
         * since, and it means exactly this.
         *
         * RandomGroundPointNear is what every script uses that spawns three of
         * something and does not want them standing inside each other.
         */
        bool TempSummonCreature(Doing& doing, Step const& step, bool& handled)
        {
            if (!step.Has(2))
            {
                handled = false;
                return false;
            }

            WorldObject* self = doing.source;
            if (!self)
            {
                sLog.outErrorDb("MAI: temp_summon_creature needs somebody to "
                                "summon");
                return false;
            }

            Geometry::Vector3 const centre(GivenF(step, 3), GivenF(step, 4),
                                           GivenF(step, 5));
            Geometry::Vector3 const spot =
                RandomGroundPointNear(*self, centre, GivenF(step, 2));

            uint32 const despawn = Given(step, 1);
            self->SummonCreature(Given(step, 0), spot.x, spot.y, spot.z,
                                 GivenF(step, 6),
                                 despawn ? TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN
                                         : TEMPSPAWN_DEAD_DESPAWN,
                                 despawn);
            return false;
        }

        /// Walk to a written position, scattered. Native under the same
        /// condition and for the same reason as the summon above.
        bool MoveTo(Doing& doing, Step const& step, bool& handled)
        {
            if (!step.Has(1))
            {
                handled = false;
                return false;
            }

            Unit* self = doing.SourceUnit();
            if (!self)
            {
                sLog.outErrorDb("MAI: move_to needs somebody to move");
                return false;
            }

            Geometry::Vector3 const centre(GivenF(step, 2), GivenF(step, 3),
                                           GivenF(step, 4));
            Geometry::Vector3 const spot =
                RandomGroundPointNear(*self, centre, GivenF(step, 1));

            self->GetMotionMaster()->MovePoint(0, spot.x, spot.y, spot.z);
            return false;
        }

        bool SummonAtTarget(Doing& doing, Step const& step)
        {
            Unit* self = doing.SourceUnit();
            WorldObject* where = doing.target;

            if (!self || !where)
            {
                sLog.outErrorDb("MAI: summon_at_target needs somebody to summon "
                                "and somebody to summon at");
                return false;
            }

            uint32 const despawn = Given(step, 1);

            // TempSpawnType, when the script names one. The default is what
            // every converted row already means: timed when there is a delay,
            // dead-despawn when there is not.
            TempSpawnType const mode =
                step.Has(2) ? TempSpawnType(Given(step, 2))
                            : (despawn ? TEMPSPAWN_TIMED_DESPAWN
                                       : TEMPSPAWN_DEAD_DESPAWN);

            // An OFFSET, not a place. The whole point of this verb is that the
            // place is not known until it runs. `o` is the exception and is
            // absolute -- there is no orientation to offset from.
            float const x = where->Where().X() + GivenF(step, 5);
            float const y = where->Where().Y() + GivenF(step, 6);
            float const z = where->Where().Z() + GivenF(step, 7);

            // Which way it looks. One means towards whoever summoned it -- a
            // bearing FROM where it appears, which is what
            // `pGo->Where().BearingTo(pPlayer->Where())` said. Two means the
            // same way as the thing it appeared at. Absent means the `o` of
            // the position facet, zero by default.
            float facing = GivenF(step, 8);
            switch (Given(step, 4))
            {
                case 1: facing = where->Where().BearingTo(self->Where()); break;
                case 2: facing = where->Where().Facing(); break;
                default: break;
            }

            Creature* made =
                self->SummonCreature(Given(step, 0), x, y, z, facing, mode,
                                     despawn);

            if (made && Given(step, 3) && made->AI())
            {
                made->AI()->AttackStart(self);
            }
            return false;
        }

        /**
         * Follow, which is not chase.
         *
         * MoveChase keeps a fighting distance and turns to face; MoveFollow
         * walks at a fixed bearing behind somebody, which is what a charmed
         * critter does. The four numbers are RANGES because the scripts that
         * want this want a crowd that does not stack: ten enthralled rats each
         * pick their own distance and angle, and look like ten rats.
         */
        bool Follow(Doing& doing, Step const& step)
        {
            Creature* self = doing.SourceCreature();
            Unit* who = doing.TargetUnit();

            if (!self || !who)
            {
                sLog.outErrorDb("MAI: follow needs a creature and somebody to "
                                "follow");
                return false;
            }

            float const nearest = GivenF(step, 0, PET_FOLLOW_DIST);
            float const farthest = GivenF(step, 1, nearest);
            float const leastAngle = GivenF(step, 2, PET_FOLLOW_ANGLE);
            float const mostAngle = GivenF(step, 3, leastAngle);

            self->GetMotionMaster()->MoveFollow(
                who,
                farthest > nearest ? frand(nearest, farthest) : nearest,
                mostAngle > leastAngle ? frand(leastAngle, mostAngle)
                                       : leastAngle);
            return false;
        }

        /**
         * The source kills the target, and the CREDIT is what this is for.
         *
         * `die` is the other half: the target simply dies, with no killer and
         * so no experience, no loot and no quest tick. A script that means one
         * and writes the other is the difference between a quest that
         * completes and one that does not, which is why they are two verbs and
         * not one with a flag.
         */
        bool KillTarget(Doing& doing, Step const&)
        {
            Unit* self = doing.SourceUnit();
            Unit* victim = doing.TargetUnit();

            if (!self || !victim || !victim->IsAlive())
            {
                return false;
            }

            self->DealDamage(victim, victim->GetMaxHealth(), nullptr,
                             DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, nullptr,
                             false);
            return false;
        }

        /**
         * Take a gameobject that is standing there ready, and use it up.
         *
         * The check and the taking are one verb on purpose: every script that
         * does this reads "if it is there and nobody has it, it is mine", and
         * splitting it in two is two searches and a window between them.
         *
         * A non-zero respawn time means somebody already took it and it is
         * waiting to come back -- the single check that keeps two players from
         * looting one node.
         */
        bool ConsumeGo(Doing& doing, Step const& step)
        {
            GameObject* found = nullptr;

            if (step.Has(1))
            {
                // The nearest one of that entry, by MaNGOS's own searcher --
                // the same one the buddy search uses, so "nearest" means the
                // same thing everywhere in MAI.
                if (doing.target)
                {
                    float const radius = GivenF(step, 1);
                    MaNGOS::NearestGameObjectEntryInObjectRangeCheck check(
                        *doing.target, Given(step, 0), radius);
                    MaNGOS::GameObjectLastSearcher<
                        MaNGOS::NearestGameObjectEntryInObjectRangeCheck>
                            search(found, check);
                    Cell::VisitGridObjects(doing.target, search, radius);
                }
            }
            else if (doing.target)
            {
                found = doing.target->ToGameObject();
                if (found && found->GetEntry() != Given(step, 0))
                {
                    found = nullptr;
                }
            }

            if (!found || found->GetRespawnTime() != 0)
            {
                return true;
            }

            found->SetLootState(GO_JUST_DEACTIVATED);
            return false;
        }

        // ---- the conditions a sequence stops on -----------------------------

        bool RequireVictim(Doing& doing, Step const& step)
        {
            Unit const* who = doing.TargetUnit();
            bool const wanted = !step.Has(0) || Given(step, 0) != 0;

            return !who || (who->getVictim() != nullptr) != wanted;
        }

        bool RequireHealth(Doing& doing, Step const& step)
        {
            Unit const* who = doing.TargetUnit();
            if (!who || !who->GetMaxHealth())
            {
                return true;
            }

            uint64 const scaled = uint64(who->GetHealth()) * 100u;

            if (step.Has(0) && scaled > uint64(who->GetMaxHealth()) * Given(step, 0))
            {
                return true;
            }

            return step.Has(1) &&
                   scaled < uint64(who->GetMaxHealth()) * Given(step, 1);
        }

        /**
         * Whether one of those is standing about nearby.
         *
         * Its OWN search rather than the step's buddy, and that is the point:
         * a buddy that is not there SKIPS the step, so a buddy can never
         * answer "is there one?". This can, which is what lets a script say
         * "attack the myrmidon, or the siren if there is no myrmidon".
         */
        bool RequireCreature(Doing& doing, Step const& step)
        {
            bool const wanted = !step.Has(2) || Given(step, 2) != 0;

            if (!doing.target)
            {
                return wanted;
            }

            // Three-valued, and GetClosestCreatureWithEntry is why: it takes
            // onlyAlive and onlyDead as two booleans, and the Larkorwi and
            // Murkdeep triggers pass FALSE to both -- "is there one at all,
            // alive or a corpse". Absent means alive only.
            uint32 const state = step.Has(3) ? Given(step, 3) : 1;
            bool const onlyAlive = state == 1;
            bool const onlyDead = state == 0;

            Creature* found = nullptr;
            float const radius = GivenF(step, 1);
            MaNGOS::NearestCreatureEntryWithLiveStateInObjectRangeCheck check(
                *doing.target, Given(step, 0), onlyAlive, onlyDead, radius);
            MaNGOS::CreatureLastSearcher<
                MaNGOS::NearestCreatureEntryWithLiveStateInObjectRangeCheck>
                    search(found, check);
            Cell::VisitGridObjects(doing.target, search, radius);

            return (found != nullptr) != wanted;
        }

        /**
         * Everything a script asks about a player before it does anything.
         *
         * One verb rather than five because that is the shape of the question
         * rather than a saving: ScriptDev asks these in clusters and almost
         * never singly --
         *
         *     if (pPlayer->IsAlive() && !pPlayer->isGameMaster() && ...)
         *
         * Each is checked only when the step wrote it, so a step names the two
         * it cares about and says nothing about the rest.
         */
        bool RequirePlayer(Doing& doing, Step const& step)
        {
            Player const* who = doing.owner.IsEmpty()
                                    ? nullptr
                                    : sObjectMgr.GetPlayer(doing.owner);
            if (!who)
            {
                return true;
            }

            if (step.Has(0) && who->IsAlive() != (Given(step, 0) != 0))
            {
                return true;
            }

            if (step.Has(1) && who->IsInCombat() != (Given(step, 1) != 0))
            {
                return true;
            }

            if (step.Has(2) && who->isGameMaster() != (Given(step, 2) != 0))
            {
                return true;
            }

            if (step.Has(3) && who->IsTaxiFlying() != (Given(step, 3) != 0))
            {
                return true;
            }

            // An ENTRY rather than a flag: Children's Week asks which orphan
            // is following you, not whether one is.
            if (step.Has(4))
            {
                Pet const* pet = who->GetMiniPet();
                if (!pet || pet->GetEntry() != Given(step, 4))
                {
                    return true;
                }
            }

            return false;
        }

        /**
         * Whether the step's source is something a script put here.
         *
         * The difference between the three Greymist Coastrunners a quest put
         * on the beach and the ones that live there: only the summoned ones
         * run to the water, and only their deaths are counted.
         */
        bool RequireSummoned(Doing& doing, Step const& step)
        {
            Creature const* self = doing.SourceCreature();
            bool const wanted = !step.Has(0) || Given(step, 0) != 0;

            return !self || self->IsTemporarySummon() != wanted;
        }

        /**
         * Open or close whichever door the step is acting on.
         *
         * Found by the buddy search rather than named by guid, which is the
         * difference from `open_door` and the reason both exist: a waterfall
         * that parts when somebody walks up to it is one of thirty-five yards,
         * not one guid.
         */
        bool UseDoor(Doing& doing, Step const& step)
        {
            GameObject* door = doing.source ? doing.source->ToGameObject()
                                            : nullptr;
            if (!door && doing.target)
            {
                door = doing.target->ToGameObject();
            }

            // Only if it is ready. An object already in use is mid-animation,
            // and using it again is what makes a door stutter.
            if (door && door->getLootState() == GO_READY)
            {
                door->UseDoorOrButton(Given(step, 0));
            }
            return false;
        }

        /**
         * Saying no.
         *
         * REFUSING is not doing, and it is the whole of what an ItemScript
         * was for: every one of them checked something and then blocked the
         * item's own spell. The verb ends the sequence and sets the flag the
         * inline run reports back to the seam as a cancel.
         *
         * EQUIP_ERR_NONE -- zero -- is a real value here rather than an
         * absence: sending it is what takes the item off the player's cursor,
         * which is why three of the four scripts sent it before saying why.
         */
        bool RefuseUse(Doing& doing, Step const& step)
        {
            if (doing.cancel)
            {
                *doing.cancel = true;
            }

            Player* who = doing.owner.IsEmpty()
                              ? nullptr
                              : sObjectMgr.GetPlayer(doing.owner);
            if (!who)
            {
                return true;
            }

            Item* item = doing.item.IsEmpty() ? nullptr
                                              : who->GetItemByGuid(doing.item);

            if (step.Has(0))
            {
                who->SendEquipError(InventoryResult(Given(step, 0)), item,
                                    nullptr);
            }

            if (step.Has(1))
            {
                // Which spell the failure is about. The item's own first spell
                // is what it always is, so a step that does not say means
                // that.
                uint32 spellId = Given(step, 2);
                if (!spellId && item && item->GetProto())
                {
                    spellId = item->GetProto()->Spells[0].SpellId;
                }

                if (SpellEntry const* spell =
                        sSpellStore.LookupEntry(spellId))
                {
                    Spell::SendCastResult(who, spell, 1,
                                          SpellCastResult(Given(step, 1)));
                }
            }

            return true;
        }

        bool RequireStandState(Doing& doing, Step const& step)
        {
            Unit const* who = doing.TargetUnit();
            return !who || who->getStandState() != Given(step, 0);
        }

        // ---- the branch -----------------------------------------------------

        /**
         * A branch, and WHERE it runs.
         *
         * A branch started from a rule stays on the creature that started it.
         * Handed to the engine instead it becomes one of the MAP's frames,
         * which have no actor and no rule behind them -- so every `set_state`,
         * `set_phase`, `set_timer` and `select=` in the branch quietly does
         * nothing. Not a load error and not a log line: the steps run, and the
         * half of them that were about the creature have no creature.
         *
         * The engine still gets the ones a rule did not start -- a sequence
         * the world started has no AI to run a branch on -- and it always gets
         * an INLINE one, where the caller is waiting on an answer that a
         * queued frame could not give.
         */
        bool StartBranchOn(Doing& doing, uint32 kind, uint32 id)
        {
            ObjectGuid const source =
                doing.source ? doing.source->GetObjectGuid() : ObjectGuid();
            ObjectGuid const target =
                doing.target ? doing.target->GetObjectGuid() : ObjectGuid();

            if (doing.driver && !doing.cancel &&
                doing.driver->StartBranch(kind, id, source, target))
            {
                return true;
            }

            return StartSequence(doing.map, kind, id, doing.source,
                                 doing.target, doing.owner, doing.item,
                                 doing.cancel);
        }

        bool StartScript(Doing& doing, Step const& step)
        {
            StartBranchOn(doing, step.Has(0) ? Given(step, 0) : KindBranch,
                          Given(step, 1));
            return false;
        }

        /**
         * "I produced this; look no further."
         *
         * The same channel `refuse_use` uses and the opposite meaning: one
         * says the thing must not happen, the other that it already has. An
         * area trigger is where it matters -- a claimed one skips the quest
         * credit the trigger would otherwise give, the tavern rest, the
         * battleground handling and the teleport.
         */
        bool Claim(Doing& doing, Step const&)
        {
            if (doing.cancel)
            {
                *doing.cancel = true;
            }
            return false;
        }

        /**
         * Exactly one of up to three, uniformly.
         *
         * Repeating an id is how a script says two-in-three: `a=7 b=7 c=8` is
         * the shape every `urand(0, 2)` in ScriptDev has, written down instead
         * of implied.
         */
        bool RandomScript(Doing& doing, Step const& step)
        {
            // The range form: one of `count` branches numbered from `first`.
            // Thirteen Ethereum prisoners are two numbers rather than thirteen
            // columns.
            if (step.Has(3) && step.Has(4) && Given(step, 4))
            {
                StartBranchOn(doing, KindBranch,
                              Given(step, 3) + urand(0, Given(step, 4) - 1));
                return false;
            }

            uint32 pick[3];
            uint32 count = 0;

            for (std::size_t slot = 0; slot < 3; ++slot)
            {
                if (step.Has(slot))
                {
                    pick[count++] = Given(step, slot);
                }
            }

            if (!count)
            {
                return false;
            }

            StartBranchOn(doing, KindBranch, pick[urand(0, count - 1)]);
            return false;
        }

        /**
         * A creature's own m_AuraFlags, which is not an update field and so
         * cannot be reached by set_unit_flag.
         */
        bool SetAuraFlags(Doing& doing, Step const& step)
        {
            Creature* self = doing.SourceCreature();
            if (!self)
            {
                return false;
            }

            if (!step.Has(1) || Given(step, 1) != 0)
            {
                self->m_AuraFlags |= uint8(Given(step, 0));
            }
            else
            {
                self->m_AuraFlags &= uint8(~Given(step, 0));
            }
            return false;
        }

        /**
         * Follow, without fighting.
         *
         * `attack_start` makes a creature fight what it is chasing -- it sets
         * a victim, adds threat and swings -- which is a different instruction
         * and the wrong one for anything not meant to swing. Sepethrea's
         * Raging Flames are immune to every school of damage and exist only to
         * be run away from; told to attack, they would stand and hit somebody
         * they cannot hurt instead of herding the raid.
         */
        bool Chase(Doing& doing, Step const& step)
        {
            Creature* self = doing.SourceCreature();
            Unit* who = doing.TargetUnit();

            if (!self || !who)
            {
                sLog.outErrorDb("MAI: chase needs a creature and somebody to "
                                "follow");
                return false;
            }

            self->GetMotionMaster()->MoveChase(who, GivenF(step, 0),
                                               GivenF(step, 1));
            return false;
        }

        /**
         * Stop unless the target is the right sort of thing.
         *
         * `terminate_script` asks whether a named creature is NEARBY, which is
         * a different question: a sequence started by a spell's dummy effect
         * knows exactly who was hit, and Morbent's cleansing means to weaken
         * Morbent rather than whoever happens to be standing next to him.
         *
         * @return true, which stops the sequence, when the target is not it.
         */
        bool RequireTarget(Doing& doing, Step const& step)
        {
            Creature const* victim = doing.target ? doing.target->ToCreature()
                                                  : nullptr;
            if (!victim)
            {
                return true;
            }

            // Up to four, because "a sickly deer or a sickly gazelle" is one
            // question and not two. Slot 0 is always given; the rest are a
            // list and stop at the first one that is not.
            for (std::size_t slot = 0; slot < 4; ++slot)
            {
                if (step.Has(slot) && victim->GetEntry() == Given(step, slot))
                {
                    return false;
                }
            }
            return true;
        }

        bool SetHealth(Doing& doing, Step const& step)
        {
            Unit* self = doing.SourceUnit();
            if (!self || !self->IsAlive())
            {
                return false;
            }

            // Out of 100 and clamped there. The maximum is the creature's own,
            // so a script says "to full" and stays right when the template
            // changes underneath it.
            uint32 const percent = Given(step, 0) > 100 ? 100 : Given(step, 0);
            uint32 const wanted = (self->GetMaxHealth() * percent) / 100;

            self->SetHealth(wanted ? wanted : 1);
            return false;
        }

        // ---- what a creature remembers --------------------------------------

        /**
         * The other half of a deadline.
         *
         * Something is given one, something else makes it moot, and whichever
         * happens first has to stop the other. Nothing before this could say
         * the second half: a rule armed itself and re-armed itself and there
         * was no way in from outside.
         */
        bool SetTimer(Doing& doing, Step const& step)
        {
            if (doing.driver)
            {
                doing.driver->Arm(Given(step, 0), Given(step, 1),
                                  !step.Has(2) || Given(step, 2) != 0);
            }
            return false;
        }

        bool SetState(Doing& doing, Step const& step)
        {
            if (!doing.actor)
            {
                return false;
            }

            std::size_t const slot = Given(step, 0);
            if (slot < MaxStates)
            {
                doing.actor->states[slot] = Given(step, 1);
            }
            return false;
        }

        bool AddState(Doing& doing, Step const& step)
        {
            if (!doing.actor)
            {
                return false;
            }

            std::size_t const slot = Given(step, 0);
            if (slot >= MaxStates)
            {
                return false;
            }

            // Signed, and floored at zero rather than wrapped. A count that
            // goes below zero becomes four billion, and a guard reading
            // `kills>=3` would then be true for ever.
            int32 const by = step.Has(1) ? step.operands[1].i : 1;
            int64 const now = int64(doing.actor->states[slot]) + by;
            doing.actor->states[slot] = uint32(now < 0 ? 0 : now);
            return false;
        }

        // ---- threat ---------------------------------------------------------

        bool ThreatChange(Doing& doing, Step const& step)
        {
            Unit* self = doing.SourceUnit();
            if (!self)
            {
                return false;
            }

            int32 const percent = step.Has(0) ? step.operands[0].i : 0;
            bool const all = Given(step, 1) != 0;

            ThreatList const& threats = self->GetThreatManager().getThreatList();
            for (HostileReference* reference : threats)
            {
                Unit* victim = reference->getTarget();
                if (!victim)
                {
                    continue;
                }
                if (!all && victim != doing.target)
                {
                    continue;
                }

                float const held = self->GetThreatManager()
                                       .getThreat(victim);
                self->GetThreatManager()
                    .modifyThreatPercent(victim, percent);
                (void)held;
            }
            return false;
        }

        bool CallForHelp(Doing& doing, Step const& step)
        {
            if (Creature* self = doing.SourceCreature())
            {
                self->CallForHelp(GivenF(step, 0, 5.0f));
            }
            return false;
        }

        bool FleeForAssist(Doing& doing, Step const&)
        {
            if (Creature* self = doing.SourceCreature())
            {
                self->DoFleeToGetAssistance();
            }
            return false;
        }

        bool ZoneCombatPulse(Doing& doing, Step const&)
        {
            if (Creature* self = doing.SourceCreature())
            {
                self->SetInCombatWithZone();
            }
            return false;
        }

        // ---- the AI's own switches ------------------------------------------

        bool AutoAttack(Doing& doing, Step const& step)
        {
            if (doing.actor)
            {
                doing.actor->meleeAllowed = Given(step, 0) != 0;
            }
            return false;
        }

        /**
         * Whether the AI drives movement in combat.
         *
         * Told to the AI, not merely written down beside it. The flag the base
         * class keeps -- COMBAT_MOVEMENT_SCRIPT, and the unit state that
         * follows it -- is what HandleMovementOnAttackStart reads, and that
         * runs at every retarget: a creature told to stand still by moving it
         * to idle alone starts chasing again the moment its victim changes,
         * which is the whole of "the script stopped working half way through
         * the fight".
         *
         * THE SECOND PARAMETER IS `melee`, and is the column EventAI's action
         * 21 has always had: whether to tell the CLIENT that the swing is
         * starting or stopping, which is what makes a caster stop showing an
         * attack animation at something it is no longer walking towards. It
         * was being read as "start chasing now" -- so a row saying "stand
         * still and stop swinging" said nothing about the swing, and one
         * saying "resume, quietly" was the only shape that ever chased.
         */
        bool CombatMovement(Doing& doing, Step const& step)
        {
            bool const enable = Given(step, 0) != 0;
            Driver* const driver = DriverFor(doing);

            if (doing.actor && driver)
            {
                doing.actor->combatMovement = enable;
            }

            if (driver)
            {
                driver->SetCombatMovementAllowed(enable, Given(step, 1) != 0);
                return false;
            }

            // Redirected at somebody else, whose AI this cannot reach: the
            // movement is all there is to change, and it lasts until that
            // creature's own AI decides otherwise.
            if (Creature* self = doing.SourceCreature())
            {
                if (Unit* victim = self->getVictim())
                {
                    if (enable)
                    {
                        self->GetMotionMaster()->MoveChase(victim);
                    }
                    else
                    {
                        self->GetMotionMaster()->MoveIdle();
                    }
                }
                else if (!enable)
                {
                    self->GetMotionMaster()->MoveIdle();
                }
            }
            return false;
        }

        /**
         * How far away, and at what angle, this creature fights from.
         *
         * The pair is the AI's, not this one chase's. Issued as a bare
         * MoveChase it lasts until the next retarget and no further -- the
         * caster kites for one victim and then walks into melee for the next,
         * because HandleMovementOnAttackStart chases at whatever the AI's own
         * distance and angle say, which nobody had written.
         */
        bool RangedMovement(Doing& doing, Step const& step)
        {
            if (Driver* const driver = DriverFor(doing))
            {
                driver->SetChase(GivenF(step, 0), GivenF(step, 1));
                return false;
            }

            // No AI behind the step -- a sequence the world started -- so
            // there is nothing to remember it on, and the one chase is all
            // this can mean.
            if (Creature* self = doing.SourceCreature())
            {
                if (Unit* victim = self->getVictim())
                {
                    self->GetMotionMaster()->MoveChase(victim, GivenF(step, 0),
                                                       GivenF(step, 1));
                }
            }
            return false;
        }

        bool ChangeMovement(Doing& doing, Step const& step)
        {
            Creature* self = doing.SourceCreature();
            if (!self)
            {
                return false;
            }

            switch (Given(step, 0))
            {
                case IDLE_MOTION_TYPE:
                    self->GetMotionMaster()->MoveIdle();
                    break;
                case RANDOM_MOTION_TYPE:
                {
                    Geometry::Placement const& where = self->Where();
                    self->GetMotionMaster()->MoveRandomAroundPoint(
                        where.X(), where.Y(), where.Z(), GivenF(step, 1, 5.0f));
                    break;
                }
                case WAYPOINT_MOTION_TYPE:
                    self->GetMotionMaster()->MoveWaypoint();
                    break;
                default:
                    sLog.outErrorDb("MAI: change_movement %u is not a movement "
                                    "type", Given(step, 0));
                    break;
            }
            return false;
        }

        bool Evade(Doing& doing, Step const&)
        {
            if (Creature* self = doing.SourceCreature())
            {
                if (self->AI())
                {
                    self->AI()->EnterEvadeMode();
                }
            }
            return false;
        }

        bool Die(Doing& doing, Step const& step)
        {
            Unit* self = doing.SourceUnit();
            if (!self || !self->IsAlive())
            {
                return false;
            }

            // `silent` is death with no killer. Damaging itself to death is
            // still a death EVENT -- procs, credit, loot -- and a script that
            // kills something the player never fought means none of that.
            if (step.Has(0) && Given(step, 0))
            {
                self->SetDeathState(JUST_DIED);
                self->SetHealth(0);
                return false;
            }

            self->DealDamage(self, self->GetHealth(), nullptr,
                             DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL,
                             nullptr, false);
            return false;
        }

        bool SetInvincibility(Doing& doing, Step const& step)
        {
            if (doing.actor)
            {
                doing.actor->invincibilityHp = Given(step, 0);
                doing.actor->invincibilityIsPercent = Given(step, 1) != 0;
            }
            return false;
        }

        bool SetThrowMask(Doing& doing, Step const& step)
        {
            if (doing.actor)
            {
                doing.actor->throwMask = Given(step, 0);
            }
            return false;
        }

        bool ThrowAiEvent(Doing& doing, Step const& step)
        {
            Creature* self = doing.SourceCreature();
            if (!self)
            {
                return false;
            }

            self->AI() && (self->AI()->SendAIEventAround(
                               AIEventType(Given(step, 0)),
                               doing.TargetUnit(), 0,
                               GivenF(step, 1, 10.0f)), true);
            return false;
        }

        // ---- instance state -------------------------------------------------

        bool SetInstanceData(Doing& doing, Step const& step)
        {
            if (!doing.map)
            {
                return false;
            }

            if (InstanceData* data = doing.map->GetInstanceData())
            {
                data->SetData(Given(step, 0), Given(step, 1));
            }
            else
            {
                sLog.outErrorDb("MAI: set_instance_data on map %u, which has "
                                "no instance data", doing.map->GetId());
            }
            return false;
        }

        bool SetInstanceData64(Doing& doing, Step const& step)
        {
            if (!doing.map)
            {
                return false;
            }

            if (InstanceData* data = doing.map->GetInstanceData())
            {
                // Two halves, because an operand is one 32-bit cell. The high
                // one is optional and absent means zero, which is what a table
                // that only ever stored a guid low part wants.
                uint64 const value = uint64(Given(step, 1)) |
                                     (uint64(Given(step, 2)) << 32);
                data->SetData64(Given(step, 0), value);
            }
            return false;
        }

        bool SetInstanceDataGuid(Doing& doing, Step const& step)
        {
            if (!doing.map)
            {
                return false;
            }

            // WHO, not a number. The target is whoever the step's selector
            // picked, and its guid is the value -- which is why this cannot be
            // set_instance_data64 with two literal halves: nothing knows the
            // number until the step runs.
            if (!doing.target)
            {
                sLog.outErrorDb("MAI: set_instance_data_guid with no target");
                return false;
            }

            if (InstanceData* data = doing.map->GetInstanceData())
            {
                data->SetData64(Given(step, 0),
                                doing.target->GetObjectGuid().GetRawValue());
            }
            else
            {
                sLog.outErrorDb("MAI: set_instance_data_guid on map %u, which "
                                "has no instance data", doing.map->GetId());
            }
            return false;
        }

        // ---- fields and flags -----------------------------------------------

        bool SetUnitField(Doing& doing, Step const& step)
        {
            Unit* self = doing.SourceUnit();
            uint32 const field = Given(step, 0);

            // Bounded by the object's own block. Unbounded, this is the one
            // verb in the set that can stop the server from a table: every
            // accessor under it asserts, and MANGOS_ASSERT is live in release.
            if (!self || field >= self->GetValuesCount())
            {
                sLog.outErrorDb("MAI: set_unit_field %u is past the end of the "
                                "object's block", field);
                return false;
            }

            self->SetUInt32Value(field, Given(step, 1));
            return false;
        }

        bool SetUnitFlag(Doing& doing, Step const& step)
        {
            if (Unit* self = doing.SourceUnit())
            {
                self->SetFlag(UNIT_FIELD_FLAGS, Given(step, 0));
            }
            return false;
        }

        bool RemoveUnitFlag(Doing& doing, Step const& step)
        {
            if (Unit* self = doing.SourceUnit())
            {
                self->RemoveFlag(UNIT_FIELD_FLAGS, Given(step, 0));
            }
            return false;
        }

        bool SetSheath(Doing& doing, Step const& step)
        {
            if (Unit* self = doing.SourceUnit())
            {
                uint32 const state = Given(step, 0);
                if (state < MAX_SHEATH_STATE)
                {
                    self->SetSheath(SheathState(state));
                }
            }
            return false;
        }

        bool EmoteTarget(Doing& doing, Step const& step)
        {
            Unit* self = doing.SourceUnit();
            Unit* victim = doing.TargetUnit();
            if (self && victim)
            {
                self->HandleEmote(Given(step, 0));
            }
            return false;
        }

        // ---- quests ---------------------------------------------------------

        bool QuestEvent(Doing& doing, Step const& step)
        {
            uint32 const quest = Given(step, 0);
            bool const all = Given(step, 1) != 0;

            if (Player* player = doing.target ? doing.target->ToPlayer()
                                              : nullptr)
            {
                if (all && player->GetGroup())
                {
                    // The whole group gets it, which is what the flag is for
                    // and what a single-player credit quietly failed to do.
                    Group* group = player->GetGroup();
                    for (GroupReference* itr = group->GetFirstMember(); itr;
                         itr = itr->next())
                    {
                        if (Player* member = itr->getSource())
                        {
                            member->AreaExploredOrEventHappens(quest);
                        }
                    }
                }
                else
                {
                    player->AreaExploredOrEventHappens(quest);
                }
            }
            return false;
        }

        bool KilledMonster(Doing& doing, Step const& step)
        {
            if (Player* player = doing.target ? doing.target->ToPlayer()
                                              : nullptr)
            {
                player->KilledMonsterCredit(Given(step, 0),
                                            doing.source
                                                ? doing.source->GetObjectGuid()
                                                : ObjectGuid());
            }
            return false;
        }

        bool CastEvent(Doing& doing, Step const& step)
        {
            if (Player* player = doing.target ? doing.target->ToPlayer()
                                              : nullptr)
            {
                player->CastedCreatureOrGO(Given(step, 0),
                                           doing.source
                                               ? doing.source->GetObjectGuid()
                                               : ObjectGuid(),
                                           Given(step, 1));
            }
            return false;
        }
    }

    bool PerformNative(Doing& doing, Step const& step, bool& handled)
    {
        handled = true;

        switch (step.action)
        {
            case ActionId::CastSpell:         return CastSpell(doing, step,
                                                                handled);
            case ActionId::TempSummonCreature:
                                              return TempSummonCreature(doing,
                                                                        step,
                                                                        handled);
            case ActionId::MoveTo:            return MoveTo(doing, step,
                                                            handled);

            case ActionId::SetState:          return SetState(doing, step);
            case ActionId::SetTimer:          return SetTimer(doing, step);
            case ActionId::AddState:          return AddState(doing, step);

            case ActionId::SetPhase:          return SetPhase(doing, step);
            case ActionId::IncPhase:          return IncPhase(doing, step);
            case ActionId::RandomPhase:       return RandomPhase(doing, step);
            case ActionId::RandomPhaseRange:  return RandomPhaseRange(doing, step);
            case ActionId::RandomSound:       return RandomSound(doing, step);
            case ActionId::RandomEmote:       return RandomEmote(doing, step);

            case ActionId::ThreatChange:      return ThreatChange(doing, step);
            case ActionId::CallForHelp:       return CallForHelp(doing, step);
            case ActionId::FleeForAssist:     return FleeForAssist(doing, step);
            case ActionId::ZoneCombatPulse:   return ZoneCombatPulse(doing, step);

            case ActionId::AutoAttack:        return AutoAttack(doing, step);
            case ActionId::CombatMovement:    return CombatMovement(doing, step);
            case ActionId::RangedMovement:    return RangedMovement(doing, step);
            case ActionId::ChangeMovement:    return ChangeMovement(doing, step);
            case ActionId::Evade:             return Evade(doing, step);
            case ActionId::Die:               return Die(doing, step);
            case ActionId::SetInvincibility:  return SetInvincibility(doing, step);
            case ActionId::SetHealth:         return SetHealth(doing, step);
            case ActionId::RequireTarget:     return RequireTarget(doing, step);
            case ActionId::RequireVictim:     return RequireVictim(doing, step);
            case ActionId::RequireHealth:     return RequireHealth(doing, step);
            case ActionId::RequireStandState: return RequireStandState(doing, step);
            case ActionId::RequireCreature:   return RequireCreature(doing, step);
            case ActionId::RequirePlayer:     return RequirePlayer(doing, step);
            case ActionId::RequireSummoned:   return RequireSummoned(doing, step);
            case ActionId::UseDoor:           return UseDoor(doing, step);
            case ActionId::RefuseUse:         return RefuseUse(doing, step);
            case ActionId::Claim:             return Claim(doing, step);
            case ActionId::StartScript:       return StartScript(doing, step);
            case ActionId::RandomScript:      return RandomScript(doing, step);
            case ActionId::ConsumeGo:         return ConsumeGo(doing, step);
            case ActionId::KillTarget:        return KillTarget(doing, step);
            case ActionId::Follow:            return Follow(doing, step);
            case ActionId::SetAuraFlags:      return SetAuraFlags(doing, step);
            case ActionId::Chase:             return Chase(doing, step);
            case ActionId::RememberTarget:    return RememberTarget(doing, step);
            case ActionId::SummonAtTarget:    return SummonAtTarget(doing, step);
            case ActionId::SetImmunity:       return SetImmunity(doing, step);
            case ActionId::SendAiEvent:       return SendAiEvent(doing, step);
            case ActionId::TeleportToTarget:  return TeleportToTarget(doing, step);
            case ActionId::SetThrowMask:      return SetThrowMask(doing, step);
            case ActionId::ThrowAiEvent:      return ThrowAiEvent(doing, step);

            case ActionId::SetInstanceData:   return SetInstanceData(doing, step);
            case ActionId::SetInstanceData64: return SetInstanceData64(doing, step);
            case ActionId::SetInstanceDataGuid:
                                              return SetInstanceDataGuid(doing, step);

            case ActionId::SetUnitField:      return SetUnitField(doing, step);
            case ActionId::SetUnitFlag:       return SetUnitFlag(doing, step);
            case ActionId::RemoveUnitFlag:    return RemoveUnitFlag(doing, step);
            case ActionId::SetSheath:         return SetSheath(doing, step);
            case ActionId::EmoteTarget:       return EmoteTarget(doing, step);

            case ActionId::QuestEvent:        return QuestEvent(doing, step);
            case ActionId::KilledMonster:     return KilledMonster(doing, step);
            case ActionId::CastEvent:         return CastEvent(doing, step);

            default:
                // One of the 47 the DB scripts already implement. Not an
                // error: see the note at the top of this file for why they are
                // still borrowed rather than rewritten.
                handled = false;
                return false;
        }
    }
}
