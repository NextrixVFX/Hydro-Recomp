#pragma once

// The player's name: THE ONE SOURCE (OURS, not retail; the user's decision). Retail reads it from g_vumetrosigninmanager 0x758FB0 +0x14, the signed-in Xbox gamer tag: at
// 0x4C3192 (the human's Driver.Name, so driver +0x60, Results' DriverName, the ghost file's owner), 0x452EAC (the championship's PlayerBoats[0].DriverName), 0x485BB8 /
// 0x48711D (the ghost and leaderboard rows) and VuGamerTagUITextEntity's text 0x445DE0, whose own answer with no manager is "Player" (0x6920E8).
// Sign-in stays a hook (scope: no Xbox services); a backend or a settings option replaces this one function's body.

#include <string>

inline std::string platform_gamer_tag( )
{
    return "Player";
}
