#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

// The two text leaves of the ScreenFooter template that are not the ticker, and the license answer. Each a slot 12 of a VuUITextEntity
// subclass (the text 0x627C60 draws), case footer_text:
//   VuGamerTagUITextEntity (ctor 0x445D40, vtable 0x73BCDC) slot 12 0x445DE0
//   VuCreditsUITextEntity  (ctor 0x445E60, vtable 0x73BC9C) slot 12 0x445F00
//   VuLicenseEntity        (ctor 0x42DC60, vtable 0x7399B4) IsTrialVersion 0x42DE10, IsFullVersion 0x42DE40
// HOOKS, the platform's: g_vumetrosigninmanager 0x758FB0 and the license manager 0x758FAC. The credits are the save's (0x758FE8 vt+0x38).

inline constexpr std::uint32_t k_ui_gamertag_vtable = 0x73BCDCu;
inline constexpr std::uint32_t k_ui_credits_vtable = 0x73BC9Cu;
inline constexpr std::uint32_t k_ui_license_vtable = 0x7399B4u;

// What 0x445DE0 reads of the sign-in manager.
struct ui_signin_t
{
    bool m_present = true;     // g 0x758FB0 not null
    bool m_signed_in = false;  // +8
    bool m_busy = false;       // +9
    std::string m_name;        // +0x14
};

// 0x445DE0: no manager: "Player". Busy: VuStringDB slot 6 on "SignIn_InProgress". Signed in: the name. Else empty.
inline std::string ui_gamertag_text( const ui_signin_t& s, const std::function< std::string( const char* ) >& string6 )
{
    if ( !s.m_present )
        return "Player";
    if ( s.m_busy )
        return string6( "SignIn_InProgress" );
    return s.m_signed_in ? s.m_name : std::string( );
}

// 0x445F00: no unlock manager: 1234567 (0x12D687); else the credits capped at 9999999 (0x98967F, the cmovl). Grouped with VuStringDB slot 6
// "Common_Numeric_Separator" by the three formats 0x692138 / 0x69213C / 0x692148, then slot 6 "CreditsSuffix" (strcat_s into the 0x100 buffer).
// A negative count takes the first format on its remainder, as retail's unsigned-free comparisons do.
// SUBSTITUTION, flagged: snprintf for the CRT's sprintf_s; a text past 0x100 bytes is truncated, retail's sprintf_s / strcat_s would abort.
inline std::string ui_credits_text( bool present, std::int32_t credits, const std::function< std::string( const char* ) >& string6 )
{
    const std::int32_t v = !present ? 0x12D687 : credits < 0x98967F ? credits : 0x98967F;
    const std::int32_t millions = v / 1000000;
    const std::int32_t thousands = ( v - millions * 1000000 ) / 1000;
    const std::int32_t units = v - ( v / 1000 ) * 1000;
    const std::string sep = string6( "Common_Numeric_Separator" );
    char buffer[ 0x100 ];
    if ( v < 1000 )
        std::snprintf( buffer, sizeof( buffer ), "%d", units );
    else if ( v < 1000000 )
        std::snprintf( buffer, sizeof( buffer ), "%d%s%03d", thousands, sep.c_str( ), units );
    else
        std::snprintf( buffer, sizeof( buffer ), "%d%s%03d%s%03d", millions, sep.c_str( ), thousands, sep.c_str( ), units );
    return std::string( buffer ) + string6( "CreditsSuffix" );
}

// 0x42DE10 IsTrialVersion: the license manager's vt+0x18. 0x42DE40 IsFullVersion: its inverse. Both a bool VuRetVal.
inline bool ui_license_answer( bool trial, bool full_plug ) { return full_plug ? !trial : trial; }
