/*
 * Copyright (C)   2013    uWoW <http://uwow.biz>
 * Copyright (C) 2008-2013 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Bracket.h"
#include "DatabaseEnv.h"

Bracket::Bracket(Player *player, BracketType type) :
    m_owner(player->GetGUID()), m_Type(type), m_rating(0)
{
    m_mmv    = sWorld->getIntConfig(CONFIG_ARENA_START_MATCHMAKER_RATING);
    memset(values, 0, sizeof(uint32) * BRACKET_END);
}

void Bracket::InitStats(uint16 rating, uint16 mmr, uint32 games, uint32 wins, uint32 week_games, uint32 week_wins, uint16 best_week, uint16 best)
{
    m_rating = rating;
    m_mmv = mmr;

    values[BRACKET_SEASON_GAMES] = games;
    values[BRACKET_SEASON_WIN] = wins;
    values[BRACKET_WEEK_GAMES] = week_games;
    values[BRACKET_WEEK_WIN] = week_wins;
    values[BRACKET_WEEK_BEST] = best_week;
    values[BRACKET_BEST] = best;
}

float GetChanceAgainst(int ownRating, int opponentRating)
{
    // Returns the chance to win against a team with the given rating, used in the rating adjustment calculation
    // ELO system
    return 1.0f / (1.0f + exp(log(10.0f) * (float)((float)opponentRating - (float)ownRating) / 400.0f));
}

int GetRatingMod(int ownRating, int opponentRating, bool won /*, float confidence_factor*/)
{
    // 'Chance' calculation - to beat the opponent
    // This is a simulation. Not much info on how it really works
    float chance = GetChanceAgainst(ownRating, opponentRating);
    float won_mod = (won) ? 1.0f : 0.0f;

    // Calculate the rating modification
    float mod;

    // TODO: Replace this hack with using the confidence factor (limiting the factor to 2.0f)
    if (won && ownRating < 1300)
    {
        if (ownRating < 1000)
            mod = 48.0f * (won_mod - chance);
        else
            mod = (24.0f + (24.0f * (1300.0f - float(ownRating)) / 300.0f)) * (won_mod - chance);
    }
    else
        mod = 24.0f * (won_mod - chance);

    return (int)ceil(mod);

}


int GetMatchmakerRatingMod(int ownRating, int opponentRating, bool won )
{
    // 'Chance' calculation - to beat the opponent
    // This is a simulation. Not much info on how it really works
    float chance = GetChanceAgainst(ownRating, opponentRating);
    float won_mod = (won) ? 1.0f : 0.0f;
    float mod = won_mod - chance;

    // Work in progress:
    /*
    // This is a simulation, as there is not much info on how it really works
    float confidence_mod = min(1.0f - fabs(mod), 0.5f);

    // Apply confidence factor to the mod:
    mod *= confidence_factor

    // And only after that update the new confidence factor
    confidence_factor -= ((confidence_factor - 1.0f) * confidence_mod) / confidence_factor;
    */

    // Real rating modification
    mod *= 24.0f;

    return (int)ceil(mod);
}

void Bracket::SaveStats()
{
    int32 index = 0;
    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_CHARACTER_BRACKETS_STATS);
    stmt->setUInt32(index++, GUID_LOPART(m_owner));
    stmt->setUInt8(index++, m_Type);
    stmt->setUInt16(index++, m_rating);
    stmt->setUInt16(index++, values[BRACKET_BEST]);
    stmt->setUInt16(index++, values[BRACKET_WEEK_BEST]);
    stmt->setUInt16(index++, m_mmv);
    stmt->setUInt32(index++, values[BRACKET_SEASON_GAMES]);
    stmt->setUInt32(index++, values[BRACKET_SEASON_WIN]);
    stmt->setUInt32(index++, values[BRACKET_WEEK_GAMES]);
    stmt->setUInt32(index++, values[BRACKET_WEEK_WIN]);
    CharacterDatabase.Execute(stmt);
}

uint16 Bracket::FinishGame(bool win, uint16 opponents_mmv)
{
    values[BRACKET_SEASON_GAMES]++;
    values[BRACKET_WEEK_GAMES]++;

    if (win)
    {
        values[BRACKET_SEASON_WIN]++;
        values[BRACKET_WEEK_WIN]++;
    }

    int16 mod = (win) ? WonAgainst(opponents_mmv) : LostAgainst(opponents_mmv);

    m_rating += mod;
    m_mmv += GetMatchmakerRatingMod(m_mmv, opponents_mmv, win);

    if (m_rating > values[BRACKET_WEEK_BEST])
        values[BRACKET_WEEK_BEST] = m_rating;

    if (m_rating > values[BRACKET_BEST])
        values[BRACKET_BEST] = m_rating;

    if (Player* player = ObjectAccessor::FindPlayer(m_owner))
    {
        if (m_rating > player->GetMaxPersonalArenaRatingRequirement(BRACKET_TYPE_ARENA_2))
            player->UpdateConquestCurrencyCap(CURRENCY_TYPE_CONQUEST_META_ARENA);        
    }

    SaveStats();

    return mod;
}

int16 Bracket::WonAgainst(uint16 opponents_mmv)
{
    return GetRatingMod(m_rating, opponents_mmv, true);
}

int16 Bracket::LostAgainst(uint16 opponents_mmv)
{
    return GetRatingMod(m_rating, opponents_mmv, false);
}