#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// VuGame's Results / Awards names (world batch 32, case game_awards): slot 9, slot 10, slot 11 of the four games and Awards'
// enter 0x484320. The services they ask are platform (saves / progress, the user's scope - hooks, not copied): the hidden
// package manager g 0x758FD0 vt+0x2C, the progress manager g 0x758FD8 vt+0x80, the unlock manager g 0x758FE8.

// What the enter reads of the human (the boat manager's +0x24 list [0]).
struct vugame_award_boat_t
{
    std::string name;          // +0x8C (the Boat property)
    std::string boat_class;    // +0x160
    std::int32_t tier = 0;     // +0x4CC
};

struct vugame_award_hooks_t
{
    std::function< bool( const std::string& track ) > hidden_found;                               // g 0x758FD0 vt+0x2C
    std::function< void( const std::string& boat ) > progress;                                    // g 0x758FD8 vt+0x80
    // g 0x758FE8 slot 9's call: vt+0x18 Race, +0x20 Slalom, +0x24 TimeTrial, +0x2C Ghost (class, tier) -> the value.
    std::function< std::int32_t( std::uint32_t slot, const std::string& boat_class, std::int32_t tier ) > unlock_value;
    std::function< bool( std::int32_t value, const std::vector< std::string >& names ) > unlock_show;   // vt+0x58
    // The award screen: new 0x94, ctor 0x454D60, 0x47ECC0( "Screens/Award" ) -> +0x88; 0x47F350( tier, class, value, names ).
    std::function< bool( const char* project, std::int32_t tier, const std::string& boat_class, std::int32_t value,
                         const std::vector< std::string >& names ) > award_open;
};

// Slot 11, the results project: 0x489C40 VuRaceGame, 0x48CAD0 VuSlalomGame, 0x48F540 VuTimeTrialGame: by +0x80 (GameData
// "IsChampionship"); 0x485670 VuGhostGame: always "Screens/RaceResults".
inline const char* vugame_results_project( const std::string& mode, bool championship )
{
    return championship && mode != "Ghost" ? "Screens/RaceResultsPoints" : "Screens/RaceResults";
}

// Slot 10 (thiscall game, std::vector<std::string>&; retn 4): 0x4830B0 under each: "Find" + track + "HiddenPackages" when the
// manager answers; then 0x489B30 / 0x48C9A0 / 0x48F410 / 0x485540: at tier 0, track + the mode's name + boat + "Local".
inline std::vector< std::string > vugame_award_names( const std::string& mode, const std::string& track, const vugame_award_boat_t& b,
                                                      const vugame_award_hooks_t& h )
{
    std::vector< std::string > names;
    if ( h.hidden_found( track ) )
        names.push_back( "Find" + track + "HiddenPackages" );
    if ( b.tier == 0 )
        names.push_back( track + mode + b.name + "Local" );
    return names;
}

// Slot 9's vtable offset on g 0x758FE8 (0x489B00, 0x48C970, 0x48F3E0, 0x485510), each game's only difference.
inline std::uint32_t vugame_unlock_slot( const std::string& mode )
{
    return mode == "Race" ? 0x18u : mode == "Slalom" ? 0x20u : mode == "TimeTrial" ? 0x24u : 0x2Cu;
}

// Awards' enter 0x484320: the progress call; slot 10, slot 9; value = 0 in a championship (the cmovne on +0x80); vt+0x58(value,
// names); with a value or that answer true, the award screen: (tier, class, the slot's answer), in a championship (4, class, 0).
// Returns +0x88 non-null.
inline bool vugame_awards_enter( const std::string& mode, const std::string& track, bool championship, const vugame_award_boat_t& b,
                                 const vugame_award_hooks_t& h )
{
    h.progress( b.name );
    const std::vector< std::string > names = vugame_award_names( mode, track, b, h );
    const std::int32_t answer = h.unlock_value( vugame_unlock_slot( mode ), b.boat_class, b.tier );
    const std::int32_t value = championship ? 0 : answer;
    const bool shown = h.unlock_show( value, names );
    if ( value == 0 && !shown )
        return false;
    return championship ? h.award_open( "Screens/Award", 4, b.boat_class, 0, names ) : h.award_open( "Screens/Award", b.tier, b.boat_class, answer, names );
}
