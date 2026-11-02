#pragma once

// The game's language from the system's (boat batch 36, case system_language). 0x4EBFE0 (caller 0x4D9900, app start), a VuMetroSys
// method on the WinRT language tag: the platform hands the tag (a hook, the caller's), the mapping is the game's and the player
// sees it. The global 0x7367E0 starts "en-us" (CRT initializer 0x64B4F0, the string 0x6C8430).
// The tag is cut at its LAST '-' (0x4ECA50), then compared whole, so "zh-Hant-TW" matches and "zh-Hant" alone does not, and a
// tag with nothing matching leaves the default.

#include <string>

inline constexpr const char* k_language_default = "en-us";

inline void system_language( const std::string& tag, std::string& language )
{
    std::string t = tag;
    const std::size_t dash = t.rfind( '-' );
    if ( dash != std::string::npos )
        t.resize( dash );
    static const char* const table[ ][ 2 ] = { { "en", "en-us" }, { "de", "de-de" }, { "es", "es-es" }, { "fr", "fr-fr" }, { "it", "it-it" },
                                               { "pt", "pt-pt" }, { "ja", "ja-jp" }, { "ko", "ko-kr" }, { "zh-Hant", "zh-tw" }, { "zh-Hans", "zh-cn" } };
    for ( const auto& row : table )
        if ( t == row[ 0 ] )
            language = row[ 1 ];
}
