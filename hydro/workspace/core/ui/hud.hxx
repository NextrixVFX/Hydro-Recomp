#pragma once

#include <cfloat>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include <xmmintrin.h>

// The race HUD's per-frame text, transliterated.
//
// VuHUDElementEntity (ctor 0x423D00, vtable 0x738C64) is a VuUITextBaseEntity
// (ctor 0x627070: +256 / +260 the text offset 0, +264 the alpha 1.0) with one
// property, "Type" (+268, a VuStaticIntEnumProperty over 0x738C18, default 0),
// and its text at +272. Its slot 12 0x423F40 is the text the base's draw (slot
// 14 0x627C60) asks for every frame. It reads the local boat through
// g_vu_boat_manager 0x758FB8 (+0x58 the local index, +0xC / +0x10 the boat
// array and its size, +0x1C the boat count) only when the settings manager
// 0x758FE0 exists too; then from the boat:
//   +1196 lap, +1200 / +1204 / +1208 three times, +1228 place (0-based),
//   +1236 laps, +1308 wrong way;
// and the units (settings +0x44 == "Metric"). Without them it shows its own
// numbers: lap 2, laps 3, times 143.45 / 183.45 / 144.56, place 2, wrong way
// 1.0, 6 boats, imperial.
//
// UNKNOWN: which race fields of OUR boat feed +1196..+1308 - the boat side has
// not recreated them. ui_hud_world_t is what the element reads; whoever owns
// the race fills it (today nobody: no manager, so the element's own numbers).

struct ui_hud_boat_t
{
    std::int32_t m_lap = 2;           // +1196
    float m_time_1200 = 143.45f;      // +1200 (Type 1)
    float m_time_1204 = 183.45f;      // +1204 (Type 0)
    float m_time_1208 = 144.56f;      // +1208 (Type 2; >= FLT_MAX: no time)
    std::int32_t m_place = 1;         // +1228 (shown + 1)
    std::int32_t m_laps = 3;          // +1236
    float m_wrong_way = 1.0f;         // +1308
    std::uint8_t m_byte_812 = 0;      // +812 (the speedometer's boost-meter rate)
};

// The element's fields straight from a boat's 0x520 VuBoatEntity image (world
// rules.hxx keeps them in race_rules_boat_t::boat): +0x4AC (1196) lap, +0x4B0 /
// +0x4B4 / +0x4B8 (1200 / 1204 / 1208) the lap / race / best-lap times, +0x4CC
// (1228) place, +0x4D4 (1236), +0x51C (1308) wrong way, +0x32C (812) - the
// offsets 0x423F40 / 0x42BA00 read, nothing else.
inline ui_hud_boat_t ui_hud_boat_of( const std::uint8_t* image )
{
    ui_hud_boat_t b;
    std::memcpy( &b.m_lap, image + 1196, 4 );
    std::memcpy( &b.m_time_1200, image + 1200, 4 );
    std::memcpy( &b.m_time_1204, image + 1204, 4 );
    std::memcpy( &b.m_time_1208, image + 1208, 4 );
    std::memcpy( &b.m_place, image + 1228, 4 );
    std::memcpy( &b.m_laps, image + 1236, 4 );
    std::memcpy( &b.m_wrong_way, image + 1308, 4 );
    b.m_byte_812 = image[ 812 ];
    return b;
}

// OURS, not retail (world/benchmark.hxx): the texts of the Benchmark HUD's two elements, Types 100 / 101.
inline std::string g_benchmark_hud[ 2 ];

struct ui_hud_world_t
{
    bool m_manager = false;                      // g_vu_boat_manager 0x758FB8 != 0
    bool m_settings = false;                     // g_vusettingsmanager 0x758FE0 != 0
    std::int32_t m_local = 0;                    // manager +0x58
    std::vector< const ui_hud_boat_t* > m_boats; // manager +0xC / +0x10
    std::int32_t m_boat_count = 6;               // manager +0x1C
    std::string m_units;                         // settings +0x44
};

struct ui_hud_hooks_t
{
    // VuStringDB getString (g 0x759078 slot 6): the localised text of a key.
    // HOOK: the string tables are not loaded by the build yet.
    std::function< std::string( const char* ) > m_string;
};
// cvttss2si: truncation; NaN / out of range -> 0x80000000.
inline std::int32_t ui_cvtt( float x ) { return _mm_cvtt_ss2si( _mm_set_ss( x ) ); }

// The time formatter 0x53F5E0 (buffer, format, seconds, size): a negative time
// gets '-' and is made positive, then t = (int)(seconds * 1000) and the format
// picks the fields (0 "%d:%02d:%02d" h:m:s, 1 "%d:%02d" m:s, 2 "%d:%02d.%02d",
// 3 "%d:%02d.%03d", 4 "%d" s, 5 "%d.%02d", 6 "%d.%03d"; anything else writes
// nothing). _snprintf_s with count = size: the outputs here always fit.
inline std::string ui_format_time( int format, float seconds )
{
    std::string sign;
    if ( 0.0f > seconds )
    {
        seconds = -seconds;
        sign = "-";
    }
    const std::int32_t t = ui_cvtt( seconds * 1000.0f );
    const std::int32_t s = t / 10 / 100;
    const std::int32_t c = t / 10;
    char out[ 256 ] = { };
    switch ( format )
    {
    case 0: std::snprintf( out, sizeof( out ), "%d:%02d:%02d", s / 60 / 60, s / 60 % 60, s % 60 ); break;
    case 1: std::snprintf( out, sizeof( out ), "%d:%02d", s / 60, s % 60 ); break;
    case 2: std::snprintf( out, sizeof( out ), "%d:%02d.%02d", s / 60, s % 60, c - 100 * s ); break;
    case 3: std::snprintf( out, sizeof( out ), "%d:%02d.%03d", s / 60, s % 60, t % 1000 ); break;
    case 4: std::snprintf( out, sizeof( out ), "%d", s ); break;
    case 5: std::snprintf( out, sizeof( out ), "%d.%02d", s, c - 100 * s ); break;
    case 6: std::snprintf( out, sizeof( out ), "%d.%03d", s, t % 1000 ); break;
    default: return sign;
    }
    return sign + out;
}

class c_ui_hud_element
{
public:
    explicit c_ui_hud_element( ui_hud_hooks_t& hooks ) : m_hooks( hooks ) { }

    // slot 12 0x423F40: the text for this frame, by Type (0 the +1204 time as
    // "%d:%02d", 1 the +1200 time through 0x53F5E0 format 1, 2 the +1208 time
    // the same way or HUD_NoTime at FLT_MAX and above (or NaN), 3
    // "lap/laps" with lap clamped to laps then to >= 1, 4 the place + 1,
    // 5 "/boats", 6 HUD_WrongWay while wrong way > 0 and the alpha (+264) set
    // to it either way, 7 the units' string; any other Type: "").
    const std::string& text( const ui_hud_world_t& world )
    {
        if ( m_type == 100 || m_type == 101 )   // ours: world/benchmark.hxx
            return g_benchmark_hud[ m_type - 100 ];
        // The defaults, then the local boat when the manager and the settings
        // both exist, the index is below the array size (a signed compare; a
        // negative index would read before the array - never authored, NOT
        // exercised: treated as no boat here) and the slot holds a boat.
        ui_hud_boat_t r;
        std::int32_t boats = 6;
        bool metric = false;
        if ( world.m_manager && world.m_settings && world.m_local >= 0 &&
             world.m_local < static_cast< std::int32_t >( world.m_boats.size( ) ) && world.m_boats[ world.m_local ] )
        {
            r = *world.m_boats[ world.m_local ];
            boats = world.m_boat_count;
            metric = world.m_units == "Metric";  // VuString_equals 0x404990
        }
        std::string out;
        switch ( m_type )
        {
        case 0: out = ui_format_time( 1, r.m_time_1204 ); break;  // inlined in 0x423F40: the same arithmetic
        case 1: out = ui_format_time( 1, r.m_time_1200 ); break;
        case 2:
            if ( FLT_MAX > r.m_time_1208 )
                out = ui_format_time( 1, r.m_time_1208 );
            else
                out = string( "HUD_NoTime" );
            break;
        case 3:
        {
            std::int32_t lap = r.m_lap;
            if ( r.m_laps < lap )
                lap = r.m_laps;
            if ( lap < 1 )
                lap = 1;
            out = std::to_string( lap ) + "/" + std::to_string( r.m_laps );
            break;
        }
        case 4: out = std::to_string( r.m_place + 1 ); break;
        case 5: out = "/" + std::to_string( boats ); break;
        case 6:
            if ( r.m_wrong_way > 0.0f )
                out = string( "HUD_WrongWay" );
            m_alpha = r.m_wrong_way;
            break;
        case 7: out = string( metric ? "Settings_Units_Metric" : "Settings_Units_Imperial" ); break;
        default: break;
        }
        // strcpy_s / sprintf_s into a 256-byte buffer, then the VuString assign
        // up to the first NUL.
        m_text.assign( out.c_str( ) );
        return m_text;
    }

    std::int32_t m_type = 0;   // +268 "Type"
    float m_alpha = 1.0f;      // +264 (VuUITextBaseEntity's ctor)
    std::string m_text;        // +272

private:
    std::string string( const char* key ) const { return m_hooks.m_string ? m_hooks.m_string( key ) : std::string( ); }

    ui_hud_hooks_t& m_hooks;
};

// x87 fld / fsub / fistp under the default control word (round to nearest,
// ties to even); out of range or NaN -> 0x80000000 (the integer indefinite).
inline std::int32_t ui_fistp( double x )
{
    if ( !( x >= -2147483648.5 && x < 2147483647.5 ) )
        return static_cast< std::int32_t >( 0x80000000u );
    return static_cast< std::int32_t >( std::nearbyint( x ) );
}

// VuHUDSpeedometerEntity (ctor 0x42AB60, vtable 0x739250): its "OnUITick"
// (registered through the VuMethod1 thunk 0x4077E3, so slot 12 0x42B860).
// The draw (0x42C1E0 -> 0x42BA00: the speed |boat +540 -> +320 xy| x
// 2.2369363, the boost count +796, +800, +784 x 0.0625) is NOT recreated.
class c_ui_hud_speedometer
{
public:
    // 0x42B860: dt = the first record when it is a float (else 0); the units;
    // the boost texture's V offset and the countdown. The boat manager and
    // the settings manager are read without a null test - retail needs both;
    // without them m_unknown is set and nothing changes.
    void on_tick( const std::uint8_t* params, int size, const ui_hud_world_t& world )
    {
        float dt = 0.0f;
        std::uint32_t type = 0xFFFFFFFFu;
        if ( size > 0 )
        {
            std::memcpy( &type, params, 4 );
            if ( type == 2 )
                std::memcpy( &dt, params + 4, 4 );
        }
        if ( !world.m_manager || !world.m_settings )
        {
            m_unknown = true;
            return;
        }
        m_metric = world.m_units == "Metric" ? 1 : 0;  // 0x404AC0 over min(size, 6) bytes, then size == 6
        const bool fast = world.m_local < static_cast< std::int32_t >( world.m_boats.size( ) ) && world.m_local >= 0 &&
                          world.m_boats[ world.m_local ] && world.m_boats[ world.m_local ]->m_byte_812;
        const float rate = fast ? 0.7f : 0.06f;  // 0x6F7670 / 0x6F75FC
        m_v_offset = dt * rate + m_v_offset;
        if ( m_v_offset > 1.0f )
            m_v_offset = m_v_offset - static_cast< float >( ui_fistp( static_cast< double >( m_v_offset ) - 0.5 ) );
        const float left = m_countdown - dt;
        m_countdown = left < 0.0f || std::isnan( left ) ? 0.5f : left;  // comiss / jb: below or unordered
    }

    std::uint32_t m_metric = 0;   // +224
    float m_v_offset = 0.0f;      // +228 (gBoostTextureVOffset)
    float m_countdown = 0.0f;     // +232
    bool m_unknown = false;
};