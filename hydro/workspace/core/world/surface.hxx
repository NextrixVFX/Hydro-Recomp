#pragma once

#include <workspace/core/entity/node.hxx>
#include <workspace/core/entity/property.hxx>
#include <workspace/core/entity/values.hxx>
#include <workspace/core/world/transform.hxx>
#include <workspace/core/world/tree.hxx>
#include <workspace/core/world/field.hxx>
#include <workspace/core/world/query.hxx>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <xmmintrin.h>

// VuWater AS A LEVEL BUILDS IT: the water surfaces and the waves over them,
// made by the water entities' slot 6 (onGameInitialize) in VuEntity::
// gameInitialize 0x500B10's order - an entity's children (+0x38, in order)
// before the entity itself (then +0x14 |= 4). 0x500B10's other steps, the
// entity repository registration (vt+0x20) and each component's vt+0x24, are
// hooks.
//
//   VuWaterSurfaceEntity 0x5DB510: descriptor 0x5DBA90, VuWater_createSurface
//     0x5284C0 -> the surface ctor 0x5359E0 / 0x535C50, the surface-tree add
//     0x529860 (which also tests every wave already held); the draw
//     registration (g_vu3ddrawmanager vt+0x28) and VuWater's listeners
//     (+0x34) are hooks.
//   VuWaterRampEntity 0x5D6F80: the same surface, then its ramp wave through
//     0x528D70 (VuWaterRampWave: base ctor 0x5A6390(1), 0x5A2570, bounds
//     0x5A28F0) and the wave add 0x529A90.
//   the six wave entities whose creation wave.hxx recreates (rectangular /
//     circular ocean, banked turn, whirlpool, directional, bump): their
//     allocator's pool node on VuWater's wave list (+0x30, PUSHED AT THE HEAD)
//     and 0x529A90.
// Any other water class an entity tree holds is counted, not built.
//
// The surface entity's water-shader lookup on its post-load (0x5DB800 ->
// 0x528610) is the render side and not modelled.

// ---------------------------------------------------------------------------
// THE ENTITIES' PROPERTIES. Each water class's targets as words from entity
// +0x60, loaded by the verified VuBasicProperty loads (engine
// entity/property.hxx) with the constructor's defaults (schema.hxx, captured
// from the ctors under emulation; the ramp's three from its ctor 0x5D6D50).
enum class water_prop_t : std::uint8_t
{
    real, angle, percent, int_enum,
    vec3   // a VuVector3 property (entity/property.hxx property_vector3): three lanes; the fourth target word is not loaded
};

struct water_prop_row_t
{
    const char* name;
    std::uint32_t offset;
    water_prop_t kind;
    std::uint32_t fallback;
    const property_choice_t* choices;
    std::uint32_t fallback_y = 0, fallback_z = 0;   // a vec3's other two lanes
};

// The choice tables, read out of the image: 0x736010 (X Size / Y Size) and
// 0x735FE0 (Complexity).
inline const property_choice_t k_water_sizes[] = { { "1", 1 }, { "2", 2 }, { "4", 4 }, { "8", 8 }, { "16", 16 }, { "32", 32 },
    { "64", 64 }, { "128", 128 }, { "256", 256 }, { "512", 512 }, { "1024", 1024 }, { "2048", 2048 }, { "4096", 4096 },
    { "8192", 8192 }, { nullptr, 0 } };
inline const property_choice_t k_water_complexity[] = { { "4", 4 }, { "5", 5 }, { "6", 6 }, { "7", 7 }, { "8", 8 }, { nullptr, 0 } };

// VuWaterSurfaceEntity's ctor 0x5DA4F0 (the ones its creation reads). Its
// ProceduralReflection (+0xEC, a bool, default 1; the descriptor copies it to
// the surface's +0x38, which VuWater 0x529470 reads) is loaded by
// water_entity_load as a byte of k_water_reflection_word; WaterMap (+0x8C)
// is the string below.
inline const water_prop_row_t k_water_surface_props[] = {
    { "X Size", 0x70, water_prop_t::int_enum, 256, k_water_sizes },
    { "Y Size", 0x74, water_prop_t::int_enum, 256, k_water_sizes },
    { "Max Wave Depth", 0x78, water_prop_t::real, 0x40A00000u, nullptr },
    { "Max Wave Height", 0x7C, water_prop_t::real, 0x40A00000u, nullptr },
    { "Reflection Height", 0x80, water_prop_t::real, 0x40A00000u, nullptr },
    { "Reflection Offset", 0x84, water_prop_t::real, 0, nullptr },
    { nullptr, 0, water_prop_t::real, 0, nullptr } };

// VuWaterRampEntity's ctor 0x5D6D50 adds three: Z Size (1.0), Transition
// Ratio % (a VuPercentageProperty, 0.5) and Flow Speed (10.0).
inline const water_prop_row_t k_water_ramp_props[] = {
    { "Z Size", 0x134, water_prop_t::real, 0x3F800000u, nullptr },
    { "Transition Ratio %", 0x138, water_prop_t::percent, 0x3F000000u, nullptr },
    { "Flow Speed", 0x13C, water_prop_t::real, 0x41200000u, nullptr },
    { nullptr, 0, water_prop_t::real, 0, nullptr } };

inline const water_prop_row_t k_water_ocean_props[] = {
    { "Complexity", 0x68, water_prop_t::int_enum, 5, k_water_complexity },
    { "Bin Siz", 0x6C, water_prop_t::real, 0x42C80000u, nullptr },
    { "Wave Direction", 0x70, water_prop_t::angle, 0, nullptr },
    { "Gravity", 0x74, water_prop_t::real, 0x411CE560u, nullptr },
    { "Wind Speed", 0x78, water_prop_t::real, 0x40A00000u, nullptr },
    { "Directional Power", 0x7C, water_prop_t::real, 0x40400000u, nullptr },
    { "Suppression Wave Length", 0x80, water_prop_t::real, 0x3F000000u, nullptr },
    { "Height Multiplier", 0x84, water_prop_t::real, 0x3DCCCCCDu, nullptr },
    { nullptr, 0, water_prop_t::real, 0, nullptr } };

inline const water_prop_row_t k_water_rectangular_props[] = {
    { "Decay Ratio X %", 0x8C, water_prop_t::percent, 0x3F666666u, nullptr },
    { "Decay Ratio Y %", 0x90, water_prop_t::percent, 0x3F666666u, nullptr },
    { nullptr, 0, water_prop_t::real, 0, nullptr } };

inline const water_prop_row_t k_water_circular_props[] = {
    { "Radius", 0x8C, water_prop_t::real, 0x42C80000u, nullptr },
    { "Decay Ratio %", 0x90, water_prop_t::percent, 0x3F666666u, nullptr },
    { nullptr, 0, water_prop_t::real, 0, nullptr } };

inline const water_prop_row_t k_water_banked_props[] = {
    { "Outer Radius", 0x60, water_prop_t::real, 0x41A00000u, nullptr },
    { "Inner Radius", 0x64, water_prop_t::real, 0x41200000u, nullptr },
    { "Height", 0x68, water_prop_t::real, 0x40000000u, nullptr },
    { "Angular Size", 0x6C, water_prop_t::angle, 0x40C90FDBu, nullptr },
    { "Angular Decay Ratio", 0x70, water_prop_t::real, 0x3F000000u, nullptr },
    { nullptr, 0, water_prop_t::real, 0, nullptr } };

inline const water_prop_row_t k_water_whirlpool_props[] = {
    { "Outer Radius", 0x60, water_prop_t::real, 0x41A00000u, nullptr },
    { "Inner Radius", 0x64, water_prop_t::real, 0x40A00000u, nullptr },
    { "Depth", 0x68, water_prop_t::real, 0x41200000u, nullptr },
    { "Angular Speed", 0x6C, water_prop_t::angle, 0, nullptr },
    { "Linear Speed", 0x70, water_prop_t::real, 0, nullptr },
    { "Foaminess", 0x74, water_prop_t::real, 0x3F800000u, nullptr },
    { nullptr, 0, water_prop_t::real, 0, nullptr } };

inline const water_prop_row_t k_water_directional_props[] = {
    { "Max Height", 0x6C, water_prop_t::real, 0x3F800000u, nullptr },
    { "Speed", 0x70, water_prop_t::real, 0x3F800000u, nullptr },
    { "Period", 0x74, water_prop_t::real, 0x3F800000u, nullptr },
    { "Longitudinal Decay Ratio %", 0x78, water_prop_t::percent, 0x3F000000u, nullptr },
    { "Lateral Decay Ratio %", 0x7C, water_prop_t::percent, 0x3F000000u, nullptr },
    { nullptr, 0, water_prop_t::real, 0, nullptr } };

inline const water_prop_row_t k_water_bump_props[] = {
    { "Max Height", 0x60, water_prop_t::real, 0x3F800000u, nullptr },
    { "Lateral Decay Ratio %", 0x64, water_prop_t::percent, 0x3F000000u, nullptr },
    { nullptr, 0, water_prop_t::real, 0, nullptr } };

// VuDirectionalFlowWaveEntity's ctor 0x5D6410 (engine schema.hxx's capture):
// Flow Velocity (a VuVector3 at +0x60, (0, 5, 0); its fourth word +0x6C is
// written by no ctor - heap garbage, case water_ctors_game - and after a load
// the vector load's stack word: UNKNOWN), the two decay ratios (percentages,
// 0.5). The whole creation: world/watchers.hxx water_game_entity_construct.
inline const water_prop_row_t k_water_flow_props[] = {
    { "Flow Velocity", 0x60, water_prop_t::vec3, 0, nullptr, 0x40A00000u, 0 },
    { "Longitudinal Decay Ratio %", 0x70, water_prop_t::percent, 0x3F000000u, nullptr },
    { "Lateral Decay Ratio %", 0x74, water_prop_t::percent, 0x3F000000u, nullptr },
    { nullptr, 0, water_prop_t::real, 0, nullptr } };

// The classes built here, in the kind order the oracle case uses.
enum water_kind_t
{
    water_kind_surface, water_kind_ramp, water_kind_rectangular, water_kind_circular, water_kind_banked, water_kind_whirlpool,
    water_kind_directional, water_kind_bump, water_kind_none,
    water_kind_point,   // made at run time (VuWater_createPointWave 0x528E10), not by an entity's slot 6
    // VuInfiniteOceanWaveEntity (vtable 0x753B34; the front end's Screens/Background authors one): the ocean ctor
    // 0x5D8380's eight properties (+0x68 .. +0x84, k_water_ocean_props' names and defaults), slot 6 0x5D8970.
    water_kind_infinite,
    // made at run time by a hull's wake update 0x417D80 (world/wake.hxx): VuWaterWakeWave (0x5287D0, vtable 0x74D148) and
    // VuWaterFlatWakeWave (0x528850, vtable 0x74D164); slot 2 both 0x52C210, slot 0 0x52BE40 / 0x52BE80
    water_kind_wake, water_kind_flat_wake,
    // VuDirectionalFlowWaveEntity's VuWaterDirectionalFlowWave (0x5289B0, vtable 0x7507B0; slot 6 0x5D66D0, slot 7 0x5D6710):
    // no level authors one (verified on synthetic entities, case flow_wave)
    water_kind_flow
};

inline water_kind_t water_kind_of( const std::string& cls )
{
    if ( cls == "VuInfiniteOceanWaveEntity" )
        return water_kind_infinite;
    if ( cls == "VuDirectionalFlowWaveEntity" )
        return water_kind_flow;
    static const char* const names[] = { "VuWaterSurfaceEntity", "VuWaterRampEntity", "VuRectangularOceanWaveEntity",
        "VuCircularOceanWaveEntity", "VuBankedTurnWaveEntity", "VuWhirlpoolWaveEntity", "VuDirectionalWaveEntity", "VuBumpWaveEntity" };
    for ( int k = 0; k < water_kind_none; ++k )
        if ( cls == names[ k ] )
            return static_cast< water_kind_t >( k );
    return water_kind_none;
}

// Words +0x60 .. +0x143 (57), the targets each loaded property fills; `known`
// marks the ones loaded.
constexpr int k_water_entity_words = 57;
constexpr int k_water_reflection_word = ( 0xEC - 0x60 ) / 4;   // a surface's ProceduralReflection byte (+0xEC), its low byte

struct water_entity_props_t
{
    std::uint32_t word[ k_water_entity_words ]{};
    bool known[ k_water_entity_words ]{};
};

struct water_entity_state_t
{
    water_kind_t kind = water_kind_none;
    water_entity_props_t value;
    water_entity_props_t defaults;
    // The surface / ramp "WaterMap" (+0x8C, a VuAssetProperty<VuWaterMapAsset>
    // 0x5DC3D0, vtable 0x753ECC): its load and value read are the string
    // property's (0x406C20 / 0x406CD0, property_string; default the entity's
    // +0x8C, empty from the ctor); its change hook 0x5DC4B0 releases the old
    // asset and, when the asset manager has the name (0x5DC730), loads it
    // (0x5DC7E0) into the node's +0x4C - the asset manager is a hook here
    // (water_build_maps).
    std::string water_map;
    // The property's own default (its node +0x14, empty from the ctor): the
    // template mark's slot 7 0x406A70 copies the value into it (game/types.hxx
    // entity_mark_properties), and the load 0x406C20 first resets the value to
    // it - so an instance of a template keeps the template's WaterMap unless it
    // authors its own.
    std::string water_map_default;
    // Each load's "Properties", in order (a template's, then the instance's): the desc +0xA4's nodes, whose default after the template mark is the template's value, as water_map_default.
    // waterpass.hxx read_desc walks them when the surface's shader is made (0x5DB800).
    std::vector< value_t > shader_loads;
};

inline void water_props_rows( water_kind_t kind, const water_prop_row_t* rows[ 3 ] )
{
    rows[ 0 ] = rows[ 1 ] = rows[ 2 ] = nullptr;
    switch ( kind )
    {
    case water_kind_surface: rows[ 0 ] = k_water_surface_props; break;
    case water_kind_ramp: rows[ 0 ] = k_water_surface_props; rows[ 1 ] = k_water_ramp_props; break;
    case water_kind_rectangular: rows[ 0 ] = k_water_ocean_props; rows[ 1 ] = k_water_rectangular_props; break;
    case water_kind_circular: rows[ 0 ] = k_water_ocean_props; rows[ 1 ] = k_water_circular_props; break;
    case water_kind_banked: rows[ 0 ] = k_water_banked_props; break;
    case water_kind_whirlpool: rows[ 0 ] = k_water_whirlpool_props; break;
    case water_kind_directional: rows[ 0 ] = k_water_directional_props; break;
    case water_kind_bump: rows[ 0 ] = k_water_bump_props; break;
    case water_kind_infinite: rows[ 0 ] = k_water_ocean_props; break;
    case water_kind_flow: rows[ 0 ] = k_water_flow_props; break;
    default: break;
    }
}

// The state an entity of `cls` starts with (its ctor's defaults), or null for
// a class not built here.
inline std::shared_ptr< water_entity_state_t > water_entity_state_for( const std::string& cls )
{
    const water_kind_t kind = water_kind_of( cls );
    if ( kind == water_kind_none )
        return nullptr;
    auto s = std::make_shared< water_entity_state_t >( );
    s->kind = kind;
    const water_prop_row_t* rows[ 3 ];
    water_props_rows( kind, rows );
    for ( const water_prop_row_t* table : rows )
        for ( const water_prop_row_t* r = table; r && r->name; ++r )
        {
            const int k = static_cast< int >( r->offset - 0x60 ) / 4;
            s->value.word[ k ] = r->fallback;
            s->value.known[ k ] = true;
            if ( r->kind == water_prop_t::vec3 )
            {
                s->value.word[ k + 1 ] = r->fallback_y;
                s->value.word[ k + 2 ] = r->fallback_z;
                s->value.word[ k + 3 ] = 0;   // the ctor's fourth lane
                s->value.known[ k + 1 ] = s->value.known[ k + 2 ] = s->value.known[ k + 3 ] = true;
            }
        }
    // The surface / ramp's ProceduralReflection byte +0xEC: 1 from the ctor 0x5DA4F0 (its node +0x14 / +0x15 = 1).
    if ( kind == water_kind_surface || kind == water_kind_ramp )
        s->value.word[ k_water_reflection_word ] |= 1u;
    s->defaults = s->value;
    return s;
}

// VuEntity::load's "Properties" step for these classes: each node's load in
// turn (reset to its default, then the value read).
inline void water_entity_load( water_entity_state_t& s, const value_t& properties )
{
    if ( s.kind == water_kind_surface || s.kind == water_kind_ramp )
    {
        property_string( properties, "WaterMap", s.water_map, s.water_map_default );   // 0x406C20: reset to the default, then read
        s.shader_loads.push_back( properties );
    }
    const water_prop_row_t* rows[ 3 ];
    water_props_rows( s.kind, rows );
    for ( const water_prop_row_t* table : rows )
        for ( const water_prop_row_t* r = table; r && r->name; ++r )
        {
            const int k = static_cast< int >( r->offset - 0x60 ) / 4;
            std::uint32_t& word = s.value.word[ k ];
            const std::uint32_t fallback = s.defaults.word[ k ];
            if ( r->kind == water_prop_t::vec3 )
            {
                float v[ 3 ], fb[ 3 ];
                std::memcpy( fb, &s.defaults.word[ k ], 12 );
                property_vector3( properties, r->name, v, fb );
                std::memcpy( &s.value.word[ k ], v, 12 );
                s.value.known[ k + 3 ] = false;   // the vector load's 16-byte store: its fourth lane a stack word (UNKNOWN)
                continue;
            }
            if ( r->kind == water_prop_t::int_enum )
            {
                std::int32_t v;
                int count = 0;
                while ( r->choices[ count ].m_name )
                    ++count;
                property_enum( properties, r->name, v, static_cast< std::int32_t >( fallback ), r->choices, count );
                word = static_cast< std::uint32_t >( v );
                continue;
            }
            float fb;
            std::memcpy( &fb, &fallback, 4 );
            float v;
            if ( r->kind == water_prop_t::percent )
                property_percentage( properties, r->name, v, fb );
            else
                property_float( properties, r->name, v, fb, r->kind == water_prop_t::angle );
            std::memcpy( &word, &v, 4 );
        }
    if ( s.kind == water_kind_surface || s.kind == water_kind_ramp )
    {
        // "ProceduralReflection" (VuBasicProperty<bool,2> 0x737450, target +0xEC): reset to the node's default, then read
        // (0x407220); the byte only - +0xED .. +0xEF are other properties' and stay as they are (the word not `known`).
        std::uint32_t& word = s.value.word[ k_water_reflection_word ];
        std::uint8_t v;
        property_bool( properties, "ProceduralReflection", v, static_cast< std::uint8_t >( s.defaults.word[ k_water_reflection_word ] & 0xFFu ) );
        word = ( word & ~0xFFu ) | v;
    }
}

// ---------------------------------------------------------------------------
// THE TRANSFORM WORDS the creations read: VuTransformComponent +0x10 .. +0xBF -
// the local (+0x10), the world (+0x50), the rotation (+0x90), the world euler
// (+0xA0), the scale (+0xB0). Each vector's fourth lane is the stack word the
// property parse left (transform.hxx): UNKNOWN, held 0.
constexpr int k_water_transform_words = 44;

// 0x534AA0's world euler WITH a parent (transform.hxx leaves it unknown):
//   x = (float) atan2( (double) world[ 6 ], (double) world[ 10 ] )
//   y = asinf( maxss( -1.0, minss( 1.0, -world[ 2 ] ) ) )
//   z = (float) atan2( (double) world[ 1 ], (double) world[ 0 ] )
// SUBSTITUTION: __libm_sse2_atan2 / __libm_sse2_asinf are the CRT DLL's; these
// are the host CRT's atan2 / asinf (the oracle's emulation measures the same
// host functions, so a check against it cannot see a difference between them).
inline void water_world_euler( const float* world, float* euler )
{
    const float lo = _mm_cvtss_f32( _mm_min_ss( _mm_set_ss( 1.0f ), _mm_set_ss( -world[ 2 ] ) ) );
    const float s = _mm_cvtss_f32( _mm_max_ss( _mm_set_ss( -1.0f ), _mm_set_ss( lo ) ) );
    euler[ 0 ] = static_cast< float >( std::atan2( static_cast< double >( world[ 6 ] ), static_cast< double >( world[ 10 ] ) ) );
    euler[ 1 ] = static_cast< float >( std::asin( static_cast< double >( s ) ) );   // the CRT double function narrowed, as field.hxx's powf / expf
    euler[ 2 ] = static_cast< float >( std::atan2( static_cast< double >( world[ 1 ] ), static_cast< double >( world[ 0 ] ) ) );
}

inline void water_transform_words( const transform_component_t& c, std::uint32_t* out )
{
    std::memset( out, 0, 4 * k_water_transform_words );
    std::memcpy( out, c.local, 64 );
    std::memcpy( out + 16, c.world, 64 );
    std::memcpy( out + 32, c.rotation, 12 );
    float euler[ 3 ];
    if ( c.world_rotation_known )
        std::memcpy( euler, c.world_rotation, 12 );
    else
        water_world_euler( c.world, euler );
    std::memcpy( out + 36, euler, 12 );
    std::memcpy( out + 40, c.scale, 12 );
}

// ---------------------------------------------------------------------------
// An object's words by byte offset with the ones recreated marked.
struct water_words_t
{
    std::uint32_t word[ 80 ]{};
    bool known[ 80 ]{};
    void set( int offset, std::uint32_t v )
    {
        word[ offset / 4 ] = v;
        known[ offset / 4 ] = true;
    }
    void setf( int offset, float v )
    {
        std::uint32_t b;
        std::memcpy( &b, &v, 4 );
        set( offset, b );
    }
    float f( int offset ) const
    {
        float v;
        std::memcpy( &v, &word[ offset / 4 ], 4 );
        return v;
    }
};

// The two inputs every creation reads: entity words from +0x60 and the
// transform component's from +0x10.
struct water_input_t
{
    const std::uint32_t* entity = nullptr;
    const std::uint32_t* transform = nullptr;
    const char* water_map = nullptr;   // a surface's / ramp's WaterMap name (null: none)
    std::uint32_t e( int offset ) const { return entity[ ( offset - 0x60 ) / 4 ]; }
    std::uint32_t t( int offset ) const { return transform[ ( offset - 0x10 ) / 4 ]; }
    wave_source_t source( ) const
    {
        wave_source_t s{};
        s.entity = entity;
        s.transform = transform + ( 0x80 - 0x10 ) / 4;
        return s;
    }
};

// 0x41b2a0 (out, box*, matrix*): the box seeded with +-FLT_MAX, then 0x5175e0
// folds the eight corners (0x5177a0's order: x fastest, then y, then z) each
// through the matrix - ((m0 x + y m4) + m8 z) + m12, likewise y and z - into
// it, VuMath_min / max 0x403260 / 0x403280 with the corner first. The result's
// w lanes are stack words never written: not produced.
inline void water_box_transform( const float* lo, const float* hi, const float* m, float* out_lo, float* out_hi )
{
    for ( int k = 0; k < 3; ++k )
    {
        out_lo[ k ] = 3.4028235e38f;
        out_hi[ k ] = -3.4028235e38f;
    }
    for ( int c = 0; c < 8; ++c )
    {
        const float x = ( c & 1 ) ? hi[ 0 ] : lo[ 0 ];
        const float y = ( c & 2 ) ? hi[ 1 ] : lo[ 1 ];
        const float z = ( c & 4 ) ? hi[ 2 ] : lo[ 2 ];
        const float p[ 3 ] = { ( ( ( m[ 0 ] * x ) + ( y * m[ 4 ] ) ) + ( m[ 8 ] * z ) ) + m[ 12 ],
            ( ( ( m[ 1 ] * x ) + ( m[ 5 ] * y ) ) + ( z * m[ 9 ] ) ) + m[ 13 ],
            ( ( ( x * m[ 2 ] ) + ( y * m[ 6 ] ) ) + ( z * m[ 10 ] ) ) + m[ 14 ] };
        for ( int k = 0; k < 3; ++k )
        {
            out_lo[ k ] = water_min( p[ k ], out_lo[ k ] );
            out_hi[ k ] = water_max( p[ k ], out_hi[ k ] );
        }
    }
}

// VuWaterSurfaceEntity's descriptor 0x5DBA90 (56 bytes, memset first): the
// transform's world translation (+0x80, four words), its world euler z
// (+0xA8), log2 of X / Y Size as (int) of logf( (float) size ) * (1 / (float)
// log( 2.0 )) rounded by fistp (to nearest), Max Wave Depth / Height,
// Reflection Height / Offset, ProceduralReflection's byte (+0x2C, the entity's
// +0xEC), the WaterMap node's +0x4C (+0x30, an asset: UNKNOWN), and 1.
// SUBSTITUTION: logf / log are the host CRT's for __libm_sse2_logf / _log.
inline int water_log2( std::uint32_t size )
{
    const float inv = 1.0f / static_cast< float >( std::log( 2.0 ) );
    return static_cast< int >( std::nearbyint( std::log( static_cast< float >( static_cast< std::int32_t >( size ) ) ) * inv ) );
}

// The surface ctor 0x5359E0 then 0x535C50 on that descriptor - the words, by
// retail offset:
//   +0 vtable 0x74D378, +4 refcount 1, +8 0; +12 .. +67 the descriptor;
//   +68 the owner entity (a pointer: not produced); +72 / +76 1 << the log2s;
//   +80 identity . translation(+12) then rotZ(+28) . that (0x426b40);
//   +144 its inverse (0x4157c0); +208 .. +239 the box (-hx, -hy, -depth) ..
//   (hx, hy, height) through +80 (0x41b2a0); +240 / +244 hx / hy (each size
//   (float) * 0.5); +248 the surface-tree leaf, +252 the wave tree (the tree
//   itself is water_held_surface_t's), +256 = +60.
inline void water_surface_desc( const water_input_t& in, std::uint32_t* d )   // 0x5DBA90 (d: 14 words, 12 left 0)
{
    std::memset( d, 0, 56 );
    d[ 11 ] = in.e( 0xEC ) & 0xFFu;   // mov al, [entity +0xEC]; mov [desc +0x2C], al (bytes +0x2D .. +0x2F from the memset)
    for ( int k = 0; k < 4; ++k )
        d[ k ] = in.t( 0x80 + 4 * k );
    d[ 4 ] = in.t( 0xA8 );
    d[ 5 ] = static_cast< std::uint32_t >( water_log2( in.e( 0x70 ) ) );
    d[ 6 ] = static_cast< std::uint32_t >( water_log2( in.e( 0x74 ) ) );
    d[ 7 ] = in.e( 0x78 );
    d[ 8 ] = in.e( 0x7C );
    d[ 9 ] = in.e( 0x80 );
    d[ 10 ] = in.e( 0x84 );
    d[ 13 ] = 1;
}

// 0x535C50 (thiscall surface; desc*; retn 4) up to its tail: the 56 bytes to
// +12 (rep movsd), +72 / +76, the matrix +80, its inverse +144, +240 / +244 and
// the box +208 (+0x100 = desc +0x30, the map asset - water_built_t::maps here).
// Its tail - when the surface already has its surface-tree leaf (+0xF8), VuWater
// 0x529990 then 0x529860 on it - is water_surface_redescribe's (watchers.hxx).
inline void water_surface_set( water_words_t& s, const std::uint32_t* d )
{
    for ( int k = 0; k < 14; ++k )
        if ( k != 12 )   // +0x3C the WaterMap asset pointer: not produced
            s.set( 12 + 4 * k, d[ k ] );
    s.set( 72, 1u << d[ 5 ] );
    s.set( 76, 1u << d[ 6 ] );
    float m[ 16 ];
    float t[ 16 ];
    float o[ 16 ];
    wave_matrix_identity( m );
    const float at[ 3 ] = { s.f( 12 ), s.f( 16 ), s.f( 20 ) };
    hull_translation( t, at );
    hull_multiply( m, o, t );
    std::memcpy( m, o, 64 );
    wave_matrix_rotation_z( t, s.f( 28 ) );
    hull_multiply( t, o, m );
    std::memcpy( m, o, 64 );
    for ( int k = 0; k < 16; ++k )
        s.setf( 80 + 4 * k, m[ k ] );
    float inv[ 16 ];
    std::memcpy( inv, m, 64 );
    hull_invert( inv );
    for ( int k = 0; k < 16; ++k )
        s.setf( 144 + 4 * k, inv[ k ] );
    const float hx = static_cast< float >( static_cast< std::int32_t >( s.word[ 72 / 4 ] ) ) * 0.5f;
    const float hy = static_cast< float >( static_cast< std::int32_t >( s.word[ 76 / 4 ] ) ) * 0.5f;
    s.setf( 240, hx );
    s.setf( 244, hy );
    const float lo[ 3 ] = { -hx, -hy, -s.f( 40 ) };
    const float hi[ 3 ] = { hx, hy, s.f( 44 ) };
    float blo[ 3 ];
    float bhi[ 3 ];
    water_box_transform( lo, hi, m, blo, bhi );
    for ( int k = 0; k < 3; ++k )
    {
        s.setf( 208 + 4 * k, blo[ k ] );
        s.setf( 224 + 4 * k, bhi[ k ] );
    }
}

inline water_words_t water_surface_create( const water_input_t& in )
{
    water_words_t s{};
    std::uint32_t d[ 14 ];
    water_surface_desc( in, d );
    s.set( 0, 0x74D378u );
    s.set( 4, 1 );
    s.set( 8, 0 );
    water_surface_set( s, d );
    return s;
}

// VuWaterRampEntity's ramp wave: the descriptor 0x5D6F80 packs (44 bytes,
// memset first) - the world translation, (float) X / Y Size, Z Size, 0, the
// world euler z, Transition Ratio %, Flow Speed - and 0x528D70 builds it:
// VuWaterWave's ctor 0x5A6390 with 1 (+0x44 = 1: tested by its centre), vtable
// 0x75093C, +76 .. +119 zeroed, then 0x5A2570 and 0x5A28F0. +4 is 2 (the
// ctor's 1 and the allocator's hold).
inline void ramp_wave_desc( const water_input_t& in, std::uint32_t* d )   // 11 words, memset first
{
    std::memset( d, 0, 44 );
    for ( int k = 0; k < 4; ++k )
        d[ k ] = in.t( 0x80 + 4 * k );
    float x;
    x = static_cast< float >( static_cast< std::int32_t >( in.e( 0x70 ) ) );
    std::memcpy( &d[ 4 ], &x, 4 );
    x = static_cast< float >( static_cast< std::int32_t >( in.e( 0x74 ) ) );
    std::memcpy( &d[ 5 ], &x, 4 );
    d[ 6 ] = in.e( 0x134 );
    d[ 8 ] = in.t( 0xA8 );
    d[ 9 ] = in.e( 0x138 );
    d[ 10 ] = in.e( 0x13C );
}

// 0x5A2570 (thiscall wave; desc*; retn 4): the descriptor to +76 .. +116, the
// frame, the shape terms and the flow rows.
inline void ramp_wave_set( water_words_t& w, const std::uint32_t* d )
{
    for ( int k = 0; k < 11; ++k )
        w.set( 76 + 4 * k, d[ k ] );
    // The frame at +120: identity . fromScale( +92 / 2, +96 / 2, +100 / 2 ) .
    // rotZ( +108 ) . translation( +76 ); its inverse at +184.
    float m[ 16 ];
    float t[ 16 ];
    float o[ 16 ];
    wave_matrix_identity( m );
    const float scale[ 3 ] = { w.f( 92 ) * 0.5f, w.f( 96 ) * 0.5f, w.f( 100 ) * 0.5f };
    wave_matrix_scale( t, scale );
    hull_multiply( m, o, t );
    std::memcpy( m, o, 64 );
    wave_matrix_rotation_z( t, w.f( 108 ) );
    hull_multiply( m, o, t );
    std::memcpy( m, o, 64 );
    const float at[ 3 ] = { w.f( 76 ), w.f( 80 ), w.f( 84 ) };
    hull_translation( t, at );
    hull_multiply( m, o, t );
    std::memcpy( m, o, 64 );
    for ( int k = 0; k < 16; ++k )
        w.setf( 120 + 4 * k, m[ k ] );
    float inv[ 16 ];
    std::memcpy( inv, m, 64 );
    hull_invert( inv );
    for ( int k = 0; k < 16; ++k )
        w.setf( 184 + 4 * k, inv[ k ] );
    // The ramp's shape terms: +248 = 1 / ((2 - band) * band) when band (+112)
    // > 0, else 0; +252 = ((band * 2) * that) * (+100 * 0.5); +256 / +260 =
    // inverse +188 / +204 times +252.
    const float band = w.f( 112 );
    float curve = 0.0f;
    if ( _mm_comigt_ss( _mm_set_ss( band ), _mm_setzero_ps( ) ) )
        curve = 1.0f / ( ( 2.0f - band ) * band );
    const float half_z = w.f( 100 ) * 0.5f;
    w.setf( 248, curve );
    const float slope = ( ( band * 2.0f ) * curve ) * half_z;
    w.setf( 252, slope );
    const float sx = w.f( 188 ) * slope;
    const float sy = w.f( 204 ) * slope;
    w.setf( 256, sx );
    w.setf( 260, sy );
    // +264 .. +272: the frame's first row normalised (+276 a stack word: not
    // produced); +280 .. +288 its cross term with ( -sx, -sy ) times Flow Speed
    // (+292 likewise); +296 .. +304 the second row normalised times -Flow
    // Speed (+308 likewise).
    const float len = _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( ( ( m[ 0 ] * m[ 0 ] ) + ( m[ 1 ] * m[ 1 ] ) ) + ( m[ 2 ] * m[ 2 ] ) ) ) );
    const float r = 1.0f / len;
    w.setf( 264, m[ 0 ] * r );
    w.setf( 268, m[ 1 ] * r );
    w.setf( 272, m[ 2 ] * r );
    const float v27 = -w.f( 272 );
    const float v28 = -sx;
    const float v29 = -w.f( 268 );
    const float v30 = -sy;
    const float v31 = -w.f( 264 );
    const float flow = w.f( 116 );
    w.setf( 280, ( ( v30 * v27 ) - v29 ) * flow );
    w.setf( 284, ( v31 - ( v28 * v27 ) ) * flow );
    w.setf( 288, ( ( v28 * v29 ) - ( v30 * v31 ) ) * flow );
    const float r2 = 1.0f / _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( ( ( w.f( 136 ) * w.f( 136 ) ) + ( w.f( 140 ) * w.f( 140 ) ) ) +
                                                                    ( w.f( 144 ) * w.f( 144 ) ) ) ) );
    const float nflow = -flow;
    w.setf( 296, ( w.f( 136 ) * r2 ) * nflow );
    w.setf( 300, ( w.f( 140 ) * r2 ) * nflow );
    w.setf( 304, ( w.f( 144 ) * r2 ) * nflow );
}

inline void ramp_wave_bounds( water_words_t& w )
{
    // 0x5A28F0: the bounds - the rotated half-extent rectangle's corners (x87
    // fsincos of +108) folded through minss / maxss, z = +84 -+ +100 * 0.5, the
    // centre +44 / +48 and the radius sqrt( hy * hy + hx * hx ). +24 / +40 are
    // the high halves of zero-extended qwords: 0.
    const float c = wave_x87_cos( w.f( 108 ) );
    const float s = wave_x87_sin( w.f( 108 ) );
    const float v1 = w.f( 92 ) * 0.5f;
    const float v2 = w.f( 96 ) * 0.5f;
    const float v3 = w.f( 76 );
    const float v26 = w.f( 80 );
    const float v4 = c * v1;
    const float v24 = v26 - ( ( -s ) * v1 );
    const float v27b = v24 - ( c * v2 );
    const float v5 = ( ( -s ) * v1 ) + v26;
    const float v22 = v3 - ( c * v1 );
    const float v17 = s * v2;
    const float v21 = c * v2;
    const float v6 = v22 - v17;
    const float v18 = v4 + v3;
    const float v7 = v5 - v21;
    const float v8 = ( v4 + v3 ) - v17;
    const float v9 = water_min( v8, water_min( v6, 3.4028235e38f ) );
    const float v19 = v18 + v17;
    const float v10 = water_max( v8, water_max( v6, -3.4028235e38f ) );
    const float max_y = water_max( v24 + v21, water_max( v5 + v21, water_max( v7, water_max( v27b, -3.4028235e38f ) ) ) );
    const float min_y = water_min( v24 + v21, water_min( v5 + v21, water_min( v7, water_min( v27b, 3.4028235e38f ) ) ) );
    const float min_x = water_min( v22 + v17, water_min( v19, v9 ) );
    const float max_x = water_max( v22 + v17, water_max( v19, v10 ) );
    const float hz = w.f( 100 ) * 0.5f;
    const float z = w.f( 84 );
    w.set( 44, w.word[ 76 / 4 ] );
    w.setf( 48, v26 );
    w.setf( 12, min_x );
    w.setf( 16, min_y );
    w.setf( 20, z - hz );
    w.set( 24, 0 );
    w.setf( 28, max_x );
    w.setf( 32, max_y );
    w.setf( 36, z + hz );
    w.set( 40, 0 );
    w.setf( 52, _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( ( v2 * v2 ) + ( v1 * v1 ) ) ) ) );
}

inline water_words_t ramp_wave_create( const water_input_t& in )
{
    water_words_t w{};
    std::uint32_t d[ 11 ];
    ramp_wave_desc( in, d );
    w.set( 0, 0x75093Cu );
    w.set( 4, 2 );
    w.set( 8, 0 );
    w.set( 56, 0 );
    w.set( 60, 0 );
    w.set( 64, 0 );
    w.set( 68, 1 );
    w.set( 72, 0x3F800000u );
    ramp_wave_set( w, d );
    ramp_wave_bounds( w );
    return w;
}

// ---------------------------------------------------------------------------
// VuWater's pool, as its ctor's 0x594B20 on +4 builds it: 1024 eight-byte
// nodes linked in order, the last to null; count 1024, the free head the
// first. Shared by the wave list (+0x30) and every wave's entry list (+0x40).
// (The pool's +4 capacity is 1024 too; a node's first word is never written by
// it - case water_create reads it as the heap's fill.)
inline void water_hold_pool_init( water_hold_t& w )
{
    w.pool.assign( 1024, water_pool_node_t{} );
    for ( int k = 0; k < 1024; ++k )
        w.pool[ k ].next = k < 1023 ? k + 1 : -1;
    w.pool_free = 0;
    w.pool_count = 1024;
}

// The pool plus the surface tree VuWater's init 0x528200 makes (+0x3C: new +
// 0x598C70) - the state every level build starts from.
inline void water_hold_init( water_hold_t& w )
{
    w = water_hold_t{};
    water_tree_init( w.surface_tree );
    water_hold_pool_init( w );
}

// A wave the hold takes: its held fields from its words (+12 .. +40 the box,
// +44 / +48 the centre, +52 the radius, +0x44 bit 0 the centre test).
inline water_held_wave_t water_held_from( const std::uint32_t* word )
{
    water_held_wave_t v{};
    std::memcpy( v.box, word + 3, 32 );
    std::memcpy( v.centre, word + 11, 8 );
    std::memcpy( &v.radius, word + 13, 4 );
    v.point = ( word[ 17 ] & 1u ) != 0;
    return v;
}

// The allocators' common tail (0x528B40 / 0x528C20 / 0x528F40 / 0x528D00 /
// 0x528910 / 0x528E90 / 0x528D70): a pool node holding the wave pushed at the
// HEAD of the wave list, then 0x529A90.
inline int water_hold_wave_at( water_hold_t& w, int wave, const water_held_wave_t& v );

inline int water_hold_wave( water_hold_t& w, const water_held_wave_t& v )
{
    w.waves.emplace_back( );
    return water_hold_wave_at( w, static_cast< int >( w.waves.size( ) ) - 1, v );
}

// The same into an index the hold already has (a destroyed wave's, reused:
// wake.hxx water_create_wake).
inline int water_hold_wave_at( water_hold_t& w, int wave, const water_held_wave_t& v )
{
    w.waves[ static_cast< std::size_t >( wave ) ] = v;
    const int e = w.pool_free;
    w.pool_free = w.pool[ e ].next;
    --w.pool_count;
    w.pool[ e ].leaf = wave;
    w.pool[ e ].surface = -1;
    w.pool[ e ].next = w.list;
    w.list = e;
    water_hold_add( w, wave );
    return wave;
}

// 0x529860 (thiscall water, surface): the surface's box (+208 / +212 / +224 /
// +228) into the surface tree, its leaf kept (+248), then every wave on the
// list from the head - a centre-tested wave by its box's centre inside the
// surface's box, any other by box overlap - through 0x529C60.
// `surface` is an index the hold already has (its wave tree as the surface
// ctor 0x5359E0 made it, or as 0x529990 left it); the held view of the words
// (retail reads them from the surface itself) is refreshed first.
inline void water_hold_surface_insert( water_hold_t& w, const water_words_t& s, int surface )
{
    water_held_surface_t& h = w.surfaces[ static_cast< std::size_t >( surface ) ];
    for ( int k = 0; k < 16; ++k )
        h.rows[ k ] = s.f( 144 + 4 * k );
    h.z_low = s.f( 216 );
    h.z_high = s.f( 232 );
    h.half[ 0 ] = s.f( 240 );
    h.half[ 1 ] = s.f( 244 );
    h.below = s.f( 40 );
    h.above = s.f( 44 );
    const float r[ 4 ] = { s.f( 208 ), s.f( 212 ), s.f( 224 ), s.f( 228 ) };
    w.surfaces[ surface ].leaf = water_tree_add( w.surface_tree, surface, r );
    for ( int e = w.list; e >= 0; e = w.pool[ e ].next )
    {
        const water_held_wave_t& v = w.waves[ w.pool[ e ].leaf ];
        bool hit;
        if ( v.point )
        {
            const float cx = ( v.box[ 4 ] + v.box[ 0 ] ) * 0.5f;
            const float cy = ( v.box[ 5 ] + v.box[ 1 ] ) * 0.5f;
            hit = water_tree_ge( cx, r[ 0 ] ) && water_tree_ge( cy, r[ 1 ] ) && water_tree_ge( r[ 2 ], cx ) && water_tree_ge( r[ 3 ], cy );
        }
        else
        {
            hit = water_tree_ge( v.box[ 4 ], r[ 0 ] ) && water_tree_ge( r[ 2 ], v.box[ 0 ] ) && water_tree_ge( v.box[ 5 ], r[ 1 ] ) &&
                  water_tree_ge( r[ 3 ], v.box[ 1 ] );
        }
        if ( hit )
            water_hold_test( w, w.pool[ e ].leaf, surface );
    }
}

inline int water_hold_surface( water_hold_t& w, const water_words_t& s )
{
    water_held_surface_t h{};
    water_tree_init( h.waves );
    const int surface = static_cast< int >( w.surfaces.size( ) );
    w.surfaces.push_back( std::move( h ) );
    water_hold_surface_insert( w, s, surface );
    return surface;
}

// What a build leaves: the hold and every surface / wave object's words in
// creation order (a wave entity's from wave.hxx's image), per wave its kind.
struct water_built_t
{
    water_hold_t hold;
    std::vector< water_words_t > surfaces;
    std::vector< water_words_t > waves;
    std::vector< water_kind_t > wave_kind;
    int skipped = 0;   // water entities of a class not built here, or with no state / transform
    // Per wave: destroyed (its refcount reached 0 and slot 0 ran with 1); the
    // words stay for inspection, retail's object is freed.
    std::vector< std::uint8_t > wave_gone;
    // Destructions of a class not built here, or whose +8 chain was non-empty:
    // UNKNOWN effects (every built class's dtor is recreated).
    int unread_dtors = 0;
    // Per rectangular / circular ocean wave: its field (world/field.hxx) - the
    // arrays the ctor 0x59CB10 -> 0x59CCB0 builds and the tick 0x59CC00
    // rebuilds; `wave` is its index in `waves`.
    struct ocean_t
    {
        int wave = -1;
        ocean_params_t params{};
        ocean_field_t field{};
    };
    std::vector< ocean_t > oceans;
    // Per surface (as `surfaces`): its WaterMap name ("" none) and the
    // VuWaterMapAsset at its +0x100 (null until water_build_maps finds it).
    std::vector< std::string > map_names;
    std::vector< std::shared_ptr< water_map_t > > maps;
    // VuWater's surface list (+0x20, a std::list; size +0x24) as surface
    // indices in list order: createSurface 0x5284C0 appends and then sorts it
    // (0x52A670 = std::list::sort with 0x527F50: a before b when a's +0x14 -
    // its world z - is below b's, comiss / seta).
    std::vector< int > surface_list;
    // Per surface: destroyed (its slot 0 0x535B80 ran); the words stay.
    std::vector< std::uint8_t > surface_gone;
    // VuWater +0x40: 1.0 from its ctor 0x528050 (the wake update 0x417D80's segment rate: a new segment once the
    // fixed end's age reaches 1 / (4 x this)).
    float wake_rate = 1.0f;
    // Per entity water_build_world built (visit order): the entity, its kind, its
    // surface (the surface / ramp entity's +0x128) and its wave (-1 none) - what
    // the entity's handlers / releases take (world/watchers.hxx water_entity_of).
    struct entity_ref_t
    {
        const void* entity = nullptr;
        water_kind_t kind = water_kind_none;
        int surface = -1;
        int wave = -1;
    };
    std::vector< entity_ref_t > entities;
    // Destroyed wake / flat wake indices (water_built_destroy), reused by the
    // next water_create_wake - OUR index bookkeeping, not retail's (retail frees
    // the 0xC0-byte object; its list / trees / pool are unaffected): without it
    // every segment ever made stays in `waves` and every view build walks it.
    // Off by default (the checks compare wave indices in creation order); the
    // race sets wake_reuse = true after water_build_world.
    std::vector< int > wake_free;
    bool wake_reuse = false;
    // VuWater +0x44 / +0x45 / +0x46: three bytes its ctor 0x528050 sets to 1 (a
    // word store of 0x101, then a byte); the surface's shader rebuild 0x5DB840
    // reads +0x44 / +0x46 (render side). Nothing here reads them.
    std::uint8_t render_flags[ 3 ] = { 1, 1, 1 };
};

// VuWater's vtable (its ctor's word +0).
constexpr std::uint32_t k_water_vtable = 0x74D05Cu;

// VuWater's create 0x527E90 (cdecl; g_vuwater 0x759058 = the answer, 0 when
// operator new fails): new 0x48, +0xC .. +0x3C zeroed (+0x1C the renderer
// pointer among them), then the ctor 0x528050: vtable 0x74D05C, the pool
// 0x594B20 on +4 (water_hold_pool_init), +0x14 (the shader object) 0, the +0x18
// byte (water_t::skip_waves) 0, the surface list +0x20 / size +0x24 and the
// +0x28 / +0x2C list (0x4031B0: a self-linked std::list sentinel each - the
// +0x28 one is not modelled), +0x30 (the wave list) 0, the listener list +0x34
// / +0x38 (0x52A640), +0x40 = 1.0 (wake_rate), render_flags. The surface tree
// (+0x3C) stays null: the init 0x528200 makes it (water_tree_init) - here an
// empty `nodes`. Verified: case water_create.
inline void water_create( water_built_t& b )
{
    b = water_built_t{};
    water_hold_pool_init( b.hold );
    b.wake_rate = 1.0f;
}

// The field of ocean wave `wave`, or null (not an ocean wave / not built).
inline water_built_t::ocean_t* water_ocean_of( water_built_t& b, int wave )
{
    for ( water_built_t::ocean_t& o : b.oceans )
        if ( o.wave == wave )
            return &o;
    return nullptr;
}

inline const water_built_t::ocean_t* water_ocean_of( const water_built_t& b, int wave )
{
    for ( const water_built_t::ocean_t& o : b.oceans )
        if ( o.wave == wave )
            return &o;
    return nullptr;
}

// Each wave class's vtable, read from the image (word +0 after its ctor; the
// slot 2 each holds is in water_built_tick).
inline std::uint32_t water_wave_vtable( water_kind_t k )
{
    switch ( k )
    {
    case water_kind_ramp: return 0x75093Cu;
    case water_kind_rectangular: return 0x750888u;
    case water_kind_circular: return 0x7508A4u;
    case water_kind_banked: return 0x7509F4u;
    case water_kind_whirlpool: return 0x750900u;
    case water_kind_directional: return 0x750768u;
    case water_kind_bump: return 0x7509B4u;
    case water_kind_point: return 0x750978u;
    case water_kind_infinite: return 0x7508C0u;
    case water_kind_wake: return 0x74D148u;
    case water_kind_flat_wake: return 0x74D164u;
    case water_kind_flow: return 0x7507B0u;
    default: return 0u;
    }
}

inline void water_built_init( water_built_t& b )
{
    b = water_built_t{};
    water_hold_init( b.hold );
}

// One entity's slot 6.
inline void water_build_entity( water_built_t& b, water_kind_t kind, const water_input_t& in )
{
    auto add_wave = [ & ]( water_words_t w, water_kind_t k ) {
        // +0 the class's vtable (its ctor's store), +4 the refcount: the base
        // ctor's 1 plus the allocator's own (the entity's) - 2.
        w.set( 0, water_wave_vtable( k ) );
        w.set( 4, 2 );
        water_hold_wave( b.hold, water_held_from( w.word ) );
        b.waves.push_back( w );
        b.wave_kind.push_back( k );
    };
    auto from_image = [ & ]( const wave_image_t& image ) {
        water_words_t w{};
        for ( int k = 0; k < 64; ++k )
        {
            w.word[ k ] = image.word[ k ];
            w.known[ k ] = image.known[ k ];
        }
        return w;
    };
    switch ( kind )
    {
    case water_kind_surface:
    case water_kind_ramp:
    {
        b.surfaces.push_back( water_surface_create( in ) );
        b.map_names.push_back( in.water_map ? in.water_map : "" );
        b.maps.push_back( nullptr );
        b.surface_gone.push_back( 0 );
        // 0x5284C0: push_back on +0x20, then the list sort. SUBSTITUTION:
        // std::stable_sort for MSVC's std::list::sort (a stable merge sort -
        // for this strict weak order both give the one stable order).
        b.surface_list.push_back( static_cast< int >( b.surfaces.size( ) ) - 1 );
        std::stable_sort( b.surface_list.begin( ), b.surface_list.end( ), [ & ]( int x, int y ) {
            return _mm_comigt_ss( _mm_set_ss( b.surfaces[ y ].f( 20 ) ), _mm_set_ss( b.surfaces[ x ].f( 20 ) ) ) != 0;
        } );
        water_hold_surface( b.hold, b.surfaces.back( ) );
        if ( kind == water_kind_ramp )
            add_wave( ramp_wave_create( in ), kind );
        break;
    }
    case water_kind_rectangular:
    case water_kind_circular:
    case water_kind_infinite:
    {
        if ( kind == water_kind_infinite )
        {
            // VuInfiniteOceanWaveEntity's slot 6 0x5D8970: the descriptor is the entity's +0x68 .. +0x87 (a zeroed
            // 32-byte block, then the eight words); 0x528A50: new 0xD0, +8 / +0x38 / +0x3C / +0x40 / +0x90 .. +0xA0
            // zeroed, the ocean ctor 0x59CB10 (ocean_construct), vtable 0x7508C0, +4 incremented, the allocators'
            // common tail; the entity's +0x88 = the wave. No bounds of its own.
            std::uint32_t d[ 8 ];
            for ( int k = 0; k < 8; ++k )
                d[ k ] = in.source( ).e( 0x68 + 4 * k );
            wave_image_t image{};
            ocean_construct( image, d );
            add_wave( from_image( image ), kind );
        }
        else
            add_wave( from_image( kind == water_kind_rectangular ? rectangular_ocean_create( in.source( ) ) : circular_ocean_create( in.source( ) ) ),
                kind );
        // the ctor's 0x59CCB0 field half, at the ctor's time (+0x88, 0.0)
        water_built_t::ocean_t o{};
        o.wave = static_cast< int >( b.waves.size( ) ) - 1;
        o.params = ocean_params_of( b.waves.back( ).word );
        double time;
        std::memcpy( &time, &b.waves.back( ).word[ 0x88 / 4 ], 8 );
        ocean_field_update( o.field, o.params, time );
        b.oceans.push_back( std::move( o ) );
        break;
    }
    case water_kind_banked: add_wave( from_image( banked_turn_create( in.source( ) ) ), kind ); break;
    case water_kind_whirlpool: add_wave( from_image( whirlpool_create( in.source( ) ) ), kind ); break;
    case water_kind_directional: add_wave( from_image( directional_create( in.source( ) ) ), kind ); break;
    case water_kind_bump: add_wave( from_image( bump_create( in.source( ) ) ), kind ); break;
    case water_kind_flow: add_wave( from_image( flow_create( in.source( ) ) ), kind ); break;   // slot 6 0x5D66D0 (entity +0x78 = it)
    default: ++b.skipped; break;
    }
}

// VuWater_createPointWave 0x528E10 (thiscall water; desc*): new 0x94 bytes,
// +8 / +0x38 / +0x3C / +0x40 / +0x8C zeroed, VuWaterPointWave_ctor 0x5A3340,
// +4 incremented (the ctor's 1 and the manager's hold), then the allocators'
// common tail (water_hold_wave). The ctor: VuWaterWave's ctor 0x5A6390 with 0
// (+0x44 = 0: tested by its box, not its centre), vtable 0x750978, +0x4C ..
// +0x77 zeroed then the 44-byte descriptor copied over them (VuPointWaveEntity
// _tick 0x5D4C50's: x, y, z, w, Falloff Time, Magnitude, Range Start, Range
// End, Speed, Frequency, Foaminess), +0x78 (the clock) = 0, +0x8C (the expiry
// callback) = 0, the byte +0x90 (expired) = 0; then 1.0 / +0x6C (+0x7C),
// 1.0 / ( +0x68 - +0x64 ) (+0x80), 1.0 / +0x5C (+0x84), 1.0 / +0x60 (+0x88);
// the box (x - End, y - End, z) .. (End + x, End + y, z), the centre (x, y),
// the radius End. +24 / +40 are the high halves of stack qwords the ctor never
// writes and +0x91 .. +0x93 heap bytes nobody writes: UNKNOWN, not produced.
inline water_words_t point_wave_create( const std::uint32_t* desc )
{
    water_words_t w{};
    w.set( 0, 0x750978u );
    w.set( 4, 2 );
    w.set( 8, 0 );
    w.set( 56, 0 );
    w.set( 60, 0 );
    w.set( 64, 0 );
    w.set( 68, 0 );
    w.set( 72, 0x3F800000u );
    for ( int k = 0; k < 11; ++k )
        w.set( 76 + 4 * k, desc[ k ] );
    w.set( 120, 0 );
    w.set( 140, 0 );
    w.setf( 124, 1.0f / w.f( 108 ) );
    w.setf( 128, 1.0f / ( w.f( 104 ) - w.f( 100 ) ) );
    w.setf( 132, 1.0f / w.f( 92 ) );
    w.setf( 136, 1.0f / w.f( 96 ) );
    const float x = w.f( 76 );
    const float y = w.f( 80 );
    const float r = w.f( 104 );
    w.setf( 12, x - r );
    w.setf( 16, y - r );
    w.set( 20, w.word[ 84 / 4 ] );
    w.setf( 28, r + x );
    w.setf( 32, r + y );
    w.set( 36, w.word[ 84 / 4 ] );
    w.setf( 44, x );
    w.setf( 48, y );
    w.setf( 52, r );
    w.word[ 144 / 4 ] = 0;   // the byte +0x90 only: `known` stays false for the word
    return w;
}

// 0x528E10 on a build: the wave's words, its hold entry and kind; returns its
// index in the hold.
inline int water_create_point_wave( water_built_t& b, const std::uint32_t* desc )
{
    b.waves.push_back( point_wave_create( desc ) );
    b.wave_kind.push_back( water_kind_point );
    return water_hold_wave( b.hold, water_held_from( b.waves.back( ).word ) );
}

// VuPointWaveEntity_tick 0x5D4C50 (thiscall entity; float dt), THE EXPORT the
// entity's Emit path calls: when the pending byte +0x9C is set it is cleared
// and the descriptor at entity +0x70 is the transform component's +0x80 ..
// +0x8F (the world position, four words) followed by the entity's +0x80 ..
// +0x9B - the ctor 0x5D43B0's bindings, in order: Falloff Time, Magnitude,
// Range Start, Range End, Speed, Frequency, Foaminess; then 0x528E10, entity
// +0xA0 = the wave, wave +0x8C = entity + 0x5C (its VuPointWaveCallbackIF:
// the wave's tick fires it once on expiry). The +0x8C pointer is held here as
// a non-zero word with `known` false - the caller owns the expiry hook.
// READ FROM THE DISASSEMBLY, NOT ORACLE-MEASURED as a unit (0x528E10 and the
// ctor it calls are: case point_waves).
inline int water_point_wave_emit( water_built_t& b, const std::uint32_t* position, const std::uint32_t* properties )
{
    std::uint32_t desc[ 11 ];
    for ( int k = 0; k < 4; ++k )
        desc[ k ] = position[ k ];
    for ( int k = 0; k < 7; ++k )
        desc[ 4 + k ] = properties[ k ];
    const int wave = water_create_point_wave( b, desc );
    b.waves.back( ).word[ 140 / 4 ] = 1u;   // entity + 0x5C: non-null
    return wave;
}

// ---------------------------------------------------------------------------
// THE BUILT WATER'S TICK: VuWater's "Water" tick 0x529E30 (wave.hxx's
// water_tick, verified on synthetic lists) on the hold's own wave list, each
// wave's slot 2 the class's verified tick on its words.
//
// The hooks a caller supplies (all optional; a missing one that is reached is
// COUNTED in the return value, never stood in for):
struct water_built_hooks_t
{
    void* user = nullptr;
    // A point wave's +0x8C callback: slot 0 of the emitting entity's
    // VuPointWaveCallbackIF, VuPointWaveEntity's OnExpired 0x5D4B60. Its wave
    // half is water_point_wave_expired; the entity's +0xA0 = 0 and its
    // "OnExpired" plug (VuScriptComponent_findPlug 0x504770, the plug's slot 2)
    // are the caller's.
    void ( *expire )( void* user, water_built_t& b, int wave ) = nullptr;
    // An ocean wave's field rebuild inside 0x59CC00 (EvolveField 0x59D4D0,
    // 0x608BB0, 0x59D640, BuildBezierPatches 0x59D6C0) is now RECREATED on the
    // wave's b.oceans field (world/field.hxx); this hook is reached only for an
    // ocean wave with no field (none is made that way here); with a field it is
    // only TOLD, after the rebuild.
    void ( *rebuild )( void* user, int wave ) = nullptr;
    // 0x595610 on water +0x1C: the render thread's kick, a hook.
    void ( *render )( void* user ) = nullptr;
    // Told after a wave is destroyed (slot 0 with 1).
    void ( *destroyed )( void* user, int wave ) = nullptr;
};

inline void water_built_destroy( water_built_t& b, int wave );

// VuWater's wave removal 0x529800 (thiscall water; wave*; retn 4) - the same
// body the tick 0x529E30 runs on a dead wave: the FIRST list node holding the
// wave unlinked and pushed on the pool's free list (+0x10, count +8), then
// 0x529B90 (water_hold_remove: its tree leaves), then the manager's
// reference (+4) dropped, destroying the wave at zero. A wave not on the
// list: nothing at all.
inline void water_built_remove( water_built_t& b, int wave )
{
    water_hold_t& w = b.hold;
    int prev = -1;
    int e = w.list;
    while ( e >= 0 && w.pool[ e ].leaf != wave )
    {
        prev = e;
        e = w.pool[ e ].next;
    }
    if ( e < 0 )
        return;
    if ( prev >= 0 )
        w.pool[ prev ].next = w.pool[ e ].next;
    else
        w.list = w.pool[ e ].next;
    w.pool[ e ].next = w.pool_free;
    ++w.pool_count;
    w.pool_free = e;
    water_hold_remove( w, wave );
    if ( --b.waves[ wave ].word[ 1 ] == 0 )
        water_built_destroy( b, wave );
}

// Slot 0 with 1, the scalar deleting dtor - every class's is its vtable
// restored, VuWaterWave's dtor 0x5A6470, then operator delete: point 0x5A3310,
// whirlpool 0x5A1440, banked turn 0x5A4D50, ramp 0x5A2340, bump 0x5A3FB0,
// directional 0x59AAA0, directional flow 0x59C350, wake 0x52BE40, flat wake 0x52BE80, VuWaterWave's own
// 0x5A6440; the oceans' 0x59CAB0 (VuWaterBaseOceanWave, rectangular and
// circular alike) first frees the field when +0x4C is set (0x59CF90: the
// arrays +0x90 / +0x94 / +0xA0, the +0x98 / +0x9C tables, the two buffers'
// pairs from +0xB8 - here the ocean's field is dropped). 0x5A6470: 0x529800 on
// g_vuwater (a no-op once the tick has unlinked it), then while +8 is set,
// slot 0 of the object there (UNREAD: counted; every wave built here has +8 =
// 0).
inline void water_built_destroy( water_built_t& b, int wave )
{
    if ( b.wave_gone.size( ) < b.waves.size( ) )
        b.wave_gone.resize( b.waves.size( ), 0 );
    const int kind = b.wave_kind[ static_cast< std::size_t >( wave ) ];
    if ( kind == water_kind_none )
    {
        ++b.unread_dtors;   // a class this file does not build
        return;
    }
    if ( kind == water_kind_rectangular || kind == water_kind_circular || kind == water_kind_infinite )
        if ( water_built_t::ocean_t* o = water_ocean_of( b, wave ) )
            o->field = decltype( o->field )( );
    water_built_remove( b, wave );
    if ( b.waves[ wave ].word[ 2 ] != 0 )
        ++b.unread_dtors;
    b.wave_gone[ static_cast< std::size_t >( wave ) ] = 1;
    if ( b.wake_reuse && ( kind == water_kind_wake || kind == water_kind_flat_wake ) )
        b.wake_free.push_back( wave );
}

// A wave entity's slot 7 (gameRelease): VuBankedTurnWaveEntity 0x5D3690 (its
// wave at +0x74), VuBumpWaveEntity 0x5D3F10 (+0x68), VuWhirlpoolWaveEntity and
// VuDirectionalFlowWaveEntity 0x5D6710 (+0x78), VuDirectionalWaveEntity and
// VuStaticWakeWaveEntity 0x5D5E90 (+0x80), the ocean entities' 0x5D8D30
// (+0x94; 0x5D81A0 is its getter), VuInfiniteOceanWaveEntity 0x5D8A00 (+0x88;
// measured, case water_release_menu): the entity's reference (+4) dropped - slot
// 0 with 1 at zero -, then VuWater's removal 0x529800 of the same pointer (a
// no-op when the dtor already unlinked it), then the entity's word = 0 (the
// caller's). VuWaterRampEntity's 0x5D7030 releases its surface (+0x128) first
// and then its wave (+0x140) the same way: water_ramp_entity_release below.
// VuPointWaveEntity's slot 7 0x5D4AF0: water_point_entity_release below.
inline void water_entity_release( water_built_t& b, int wave )
{
    if ( --b.waves[ static_cast< std::size_t >( wave ) ].word[ 1 ] == 0u )
        water_built_destroy( b, wave );
    water_built_remove( b, wave );
}

// The wave half of VuPointWaveEntity's OnExpired 0x5D4B60 (thiscall on the
// entity's +0x5C): the entity's reference (+0xA0's +4) dropped, slot 0 with 1
// at zero. (Then the entity's +0xA0 = 0 and its "OnExpired" plug fires.)
inline void water_point_wave_expired( water_built_t& b, int wave )
{
    if ( --b.waves[ wave ].word[ 1 ] == 0 )
        water_built_destroy( b, wave );
}

// ---------------------------------------------------------------------------
// THE SURFACE'S RELEASE. The hooks (all optional; one reached while missing is
// COUNTED in the return value):
struct water_surface_hooks_t
{
    void* user = nullptr;
    // The renderer's (water +0x1C) +0x2B5 byte: its thread is drawing. When
    // set, 0x759098 vt+0x3C( renderer +0x2C0, -1 ) waits for it (platform:
    // the hook) and the byte is cleared. Null = clear.
    bool* render_busy = nullptr;
    void ( *render_wait )( void* user ) = nullptr;
    // VuWater's listener list (+0x34, a std::list of interfaces): each one's
    // slot 1 is told of a removed surface (slot 0 of an added one, in
    // createSurface). `listeners` = the list's length.
    int listeners = 0;
    void ( *listener )( void* user, int listener, int surface ) = nullptr;
    // The entity's Vu3dDrawComponent (+0x60) removed from g_vu3ddrawmanager
    // 0x7590B8 (vt+0x2C) when its +0x1E byte (registered) is set.
    void ( *draw_remove )( void* user, int surface ) = nullptr;
    // Told (not retail: a notification) as a surface's dtor reaches 0x598D10,
    // with the wave tree as that dtor receives it.
    void ( *destroying )( void* user, int surface, const water_tree_t& waves ) = nullptr;
};

// 0x529990 (thiscall water; surface*; retn 4): the surface's leaf (+0xF8) out
// of the surface tree (0x599220) and freed (the leaf count +0x14 one fewer),
// +0xF8 = 0; its wave tree's (+0xFC) nodes all freed from the root
// (0x598FC0, inlined on the root); then per wave-list node (+0x30, from the
// head) per entry of the wave's +0x40 list (its next taken first) whose leaf's
// tag (+0x1C) is this surface: the FIRST entry holding that leaf unlinked and
// pushed on the pool (+0x10, count +8).
inline void water_hold_drop_surface( water_hold_t& w, int surface )
{
    water_held_surface_t& s = w.surfaces[ static_cast< std::size_t >( surface ) ];
    water_tree_remove( w.surface_tree, s.leaf );
    water_tree_release_leaf( w.surface_tree, s.leaf );
    s.leaf = -1;
    if ( s.waves.root >= 0 )
        water_tree_free( s.waves, s.waves.root );
    for ( int e = w.list; e >= 0; e = w.pool[ e ].next )
    {
        water_held_wave_t& v = w.waves[ static_cast< std::size_t >( w.pool[ e ].leaf ) ];
        for ( int c = v.entries; c >= 0; )
        {
            const int next = w.pool[ c ].next;
            const int leaf = w.pool[ c ].leaf;
            const int tree = w.pool[ c ].surface;
            if ( w.surfaces[ static_cast< std::size_t >( tree ) ].waves.nodes[ leaf ].tag == static_cast< std::uint32_t >( surface ) )
            {
                int prev = -1;
                int x = v.entries;
                while ( x >= 0 && !( w.pool[ x ].leaf == leaf && w.pool[ x ].surface == tree ) )
                {
                    prev = x;
                    x = w.pool[ x ].next;
                }
                if ( x >= 0 )
                {
                    if ( prev >= 0 )
                        w.pool[ prev ].next = w.pool[ x ].next;
                    else
                        v.entries = w.pool[ x ].next;
                    w.pool[ x ].next = w.pool_free;
                    ++w.pool_count;
                    w.pool_free = x;
                }
            }
            c = next;
        }
    }
}

// VuWater's removeSurface 0x529770 (thiscall water; surface*; retn 4): the
// renderer's wait (hooks.render_busy), every list node (+0x20) holding the
// surface erased (0x4A44A0 - std::list::remove: all matches, size +0x24 one
// fewer each), 0x529990, then every listener's slot 1 with the surface.
inline int water_built_remove_surface( water_built_t& b, int surface, const water_surface_hooks_t& hooks )
{
    int unrecreated = 0;
    if ( hooks.render_busy && *hooks.render_busy )
    {
        if ( hooks.render_wait )
            hooks.render_wait( hooks.user );
        else
            ++unrecreated;
        *hooks.render_busy = false;
    }
    b.surface_list.erase( std::remove( b.surface_list.begin( ), b.surface_list.end( ), surface ), b.surface_list.end( ) );
    water_hold_drop_surface( b.hold, surface );
    for ( int k = 0; k < hooks.listeners; ++k )
        if ( hooks.listener )
            hooks.listener( hooks.user, k, surface );
        else
            ++unrecreated;
    return unrecreated;
}

// The surface's slot 0 with 1, 0x535B80: the dtor 0x535BB0 then operator
// delete. 0x535BB0: vtable 0x74D378, removeSurface 0x529770 on g_vuwater, the
// wave tree (+0xFC, when set) destroyed (0x598D10) and deleted; then the base
// (vtable 0x7458B8): while +8 is set, slot 0 of the object there (UNREAD:
// counted in b.unread_dtors, as the waves' are; every surface built here has
// +8 = 0); vtable 0x7458C0.
inline int water_built_destroy_surface( water_built_t& b, int surface, const water_surface_hooks_t& hooks )
{
    const int unrecreated = water_built_remove_surface( b, surface, hooks );
    water_tree_t& waves = b.hold.surfaces[ static_cast< std::size_t >( surface ) ].waves;
    if ( hooks.destroying )
        hooks.destroying( hooks.user, surface, waves );
    water_tree_destroy( waves );
    if ( b.surfaces[ static_cast< std::size_t >( surface ) ].word[ 2 ] != 0 )
        ++b.unread_dtors;
    if ( b.surface_gone.size( ) < b.surfaces.size( ) )
        b.surface_gone.resize( b.surfaces.size( ), 0 );
    b.surface_gone[ static_cast< std::size_t >( surface ) ] = 1;
    return unrecreated;
}

// VuWaterSurfaceEntity's slot 7 (gameRelease) 0x5DB5C0, THE EXPORT for an
// entity's release: when the draw component's byte (`drawn`, its +0x1E) is
// set, the draw manager's removal (hooks.draw_remove) and the byte cleared;
// then the entity's reference to its surface (+0x128; the surface's +4)
// dropped - slot 0 with 1 at zero -, the entity's +0x128 = 0 (the caller's).
// Returns the hooks reached while missing.
inline int water_surface_entity_release( water_built_t& b, int surface, bool& drawn, const water_surface_hooks_t& hooks )
{
    int unrecreated = 0;
    if ( drawn )
    {
        if ( hooks.draw_remove )
            hooks.draw_remove( hooks.user, surface );
        else
            ++unrecreated;
        drawn = false;
    }
    if ( --b.surfaces[ static_cast< std::size_t >( surface ) ].word[ 1 ] == 0u )
        unrecreated += water_built_destroy_surface( b, surface, hooks );
    return unrecreated;
}

// VuWaterRampEntity's slot 7 0x5D7030: 0x5DB5C0's body on its surface, then
// its wave (+0x140) as water_entity_release (the reference dropped, slot 0
// at zero, 0x529800), the entity's +0x140 = 0 (the caller's).
inline int water_ramp_entity_release( water_built_t& b, int surface, int wave, bool& drawn, const water_surface_hooks_t& hooks )
{
    const int unrecreated = water_surface_entity_release( b, surface, drawn, hooks );
    water_entity_release( b, wave );
    return unrecreated;
}

// ---------------------------------------------------------------------------
// VUWATER'S OWN LIFECYCLE (vtable 0x74D05C: slot 0 0x528120, slot 3 release
// 0x528410, slot 4 0x527E80, slot 5 init 0x528200 - the renderer, the dev
// menu and the "Water" tick: render / platform, not recreated).
//
// The release's hooks (all optional; one reached while missing is COUNTED):
struct water_release_hooks_t
{
    void* user = nullptr;
    // g_vutickmanager 0x759008 vt+0x28( water ): its ticks unregistered.
    void ( *tick_unregister )( void* user ) = nullptr;
    // +0x14, a refcounted object (its +4) - the water shader: dropped, its
    // slot 0 with 1 at zero (VuWaterShader's 0x59A1B0 -> 0x59A3A0: render
    // side). `shader_refs` = its +4 (null = no object: retail would fault).
    std::uint32_t* shader_refs = nullptr;
    void ( *shader_destroy )( void* user ) = nullptr;
    // +0x1C, the renderer (when set): 0x5953F0 then operator delete - its
    // thread, render side.
    bool renderer = false;
    void ( *renderer_destroy )( void* user ) = nullptr;
    // Told (not retail: a notification) as the surface tree reaches 0x598D10.
    void ( *tree_destroying )( void* user, const water_tree_t& tree ) = nullptr;
};

// Slot 3 0x528410 (thiscall water): the tick unregistered; while the wave list
// (+0x30) is not empty, its head's wave unlinked (the first node holding it,
// pooled), 0x529B90 on it, its reference dropped - slot 0 with 1 at zero (the
// body of 0x529800, water_built_remove); the +0x14 object dropped, +0x14 = 0;
// the renderer destroyed; the surface tree (+0x3C, when set) destroyed by
// 0x598D10 and deleted. Surfaces are not touched (their entities own them).
inline int water_built_release( water_built_t& b, const water_release_hooks_t& hooks )
{
    int unrecreated = 0;
    if ( hooks.tick_unregister )
        hooks.tick_unregister( hooks.user );
    else
        ++unrecreated;
    while ( b.hold.list >= 0 )
        water_built_remove( b, b.hold.pool[ b.hold.list ].leaf );
    if ( !hooks.shader_refs )
        ++unrecreated;
    else if ( --*hooks.shader_refs == 0u )
    {
        if ( hooks.shader_destroy )
            hooks.shader_destroy( hooks.user );
        else
            ++unrecreated;
    }
    if ( hooks.renderer )
    {
        if ( hooks.renderer_destroy )
            hooks.renderer_destroy( hooks.user );
        else
            ++unrecreated;
    }
    if ( hooks.tree_destroying )
        hooks.tree_destroying( hooks.user, b.hold.surface_tree );
    water_tree_destroy( b.hold.surface_tree );
    return unrecreated;
}

// Slot 4 0x527E80: g_vuwater 0x759058 = 0 - `water` is the caller's pointer.
inline void water_global_clear( water_built_t*& water )
{
    water = nullptr;
}

// Slot 0 0x528120 (the scalar deleting dtor): 0x528150 then operator delete.
// 0x528150: the listener list (+0x34, size +0x38), the +0x28 list (size
// +0x2C: nothing of it is modelled here) and the surface list (+0x20, size
// +0x24) each emptied - nodes and sentinel freed -, the pool's block (+0xC)
// freed, the base vtable 0x736EE8. Here: the surface list and the pool
// dropped.
inline void water_built_destroy_water( water_built_t& b )
{
    b.surface_list.clear( );
    b.hold.pool.clear( );
    b.hold.pool.shrink_to_fit( );
}

// ---------------------------------------------------------------------------
// VuPointWaveEntity (vtable 0x75344C; slot 0 0x405000 = VuEntity's dtor
// 0x500870 + operator delete): the words its slot 6 / tick / slot 7 use.
struct water_point_entity_t
{
    // +0x70: the descriptor - the position (the tick's copy of the transform's
    // +0x80 .. +0x8F), then +0x80 .. +0x9B the properties the ctor 0x5D43B0
    // binds (Falloff Time, Magnitude, Range Start, Range End, Speed,
    // Frequency, Foaminess).
    std::uint32_t desc[ 11 ]{};
    bool pending = false;   // +0x9C, set by the Emit input (its handler: not read here)
    int wave = -1;          // +0xA0, -1 = null
};

// g_vutickmanager 0x759008 - the engine's tick manager, a hook here.
struct water_point_entity_hooks_t
{
    void* user = nullptr;
    // slot 6: vt+0x20( functor, "Decision" 0x6DFA50 ), the functor a new
    // 16 bytes {0x753414, entity, 0x5D4C50 (the tick), 0} (a failed new
    // registers null - not modelled).
    void ( *tick_register )( void* user, const char* phase ) = nullptr;
    // slot 7: vt+0x28( entity ) - the entity's ticks unregistered.
    void ( *tick_unregister )( void* user ) = nullptr;
};

// Slot 6 (gameInitialize) 0x5D4A90: the tick registered. Returns 1 when the
// hook is missing (counted), else 0.
inline int water_point_entity_init( const water_point_entity_hooks_t& hooks )
{
    if ( !hooks.tick_register )
        return 1;
    hooks.tick_register( hooks.user, "Decision" );
    return 0;
}

// The tick 0x5D4C50 (thiscall entity; float dt - unread): when +0x9C is set
// it is cleared, the transform's position copied to +0x70, 0x528E10 on the
// descriptor, +0xA0 = the wave, wave +0x8C = entity + 0x5C
// (water_point_wave_emit's body). `position` = the transform's +0x80 words.
inline void water_point_entity_tick( water_built_t& b, water_point_entity_t& e, const std::uint32_t* position )
{
    if ( !e.pending )
        return;
    e.pending = false;
    for ( int k = 0; k < 4; ++k )
        e.desc[ k ] = position[ k ];
    e.wave = water_point_wave_emit( b, e.desc, e.desc + 4 );
}

// Slot 7 (gameRelease) 0x5D4AF0: the tick unregistered, then when +0xA0 is
// set water_entity_release's body on it and +0xA0 = 0. Returns 1 when the
// hook is missing (counted), else 0.
inline int water_point_entity_release( water_built_t& b, water_point_entity_t& e, const water_point_entity_hooks_t& hooks )
{
    int unrecreated = 0;
    if ( hooks.tick_unregister )
        hooks.tick_unregister( hooks.user );
    else
        ++unrecreated;
    if ( e.wave >= 0 )
    {
        water_entity_release( b, e.wave );
        e.wave = -1;
    }
    return unrecreated;
}

// 0x529E30 (thiscall water; float dt): per list node from +0x30, the next
// node taken BEFORE the call, alive = slot 2( +0x48 * dt ); a dead wave goes
// through 0x529800's body; then the renderer's kick. The slot 2s, read from
// each class's vtable: ramp 0x75093C / banked 0x7509F4 / bump 0x7509B4 ->
// 0x59C380; rectangular 0x750888 / circular 0x7508A4 -> 0x59CC00; whirlpool
// 0x750900 -> 0x5A1680 (clock +0x74); directional 0x750768 -> 0x59AAD0
// (clock +0x7C); point 0x750978 -> 0x5A34B0. Returns how many unrecreated
// paths were reached with no hook to answer them (0 = none).
inline int water_built_tick( water_built_t& b, float dt, const water_built_hooks_t& hooks )
{
    struct point_call_t
    {
        water_built_t* b;
        const water_built_hooks_t* hooks;
        int wave;
        int ref;
        int missing;
    };
    struct ocean_call_t
    {
        const water_built_hooks_t* hooks;
        int wave;
        int missing;
        water_built_t::ocean_t* ocean;
        const ocean_clock_t* clock;
    };
    const int unread_before = b.unread_dtors;
    int missing = 0;
    for ( int at = b.hold.list; at >= 0; )
    {
        const int next = b.hold.pool[ at ].next;
        const int wave = b.hold.pool[ at ].leaf;
        const water_words_t& v = b.waves[ static_cast< std::size_t >( wave ) ];
        const float scaled = v.f( 0x48 ) * dt;
        bool alive = true;
        switch ( b.wave_kind[ static_cast< std::size_t >( wave ) ] )
        {
        case water_kind_whirlpool:
        case water_kind_directional:
        {
            const bool whirlpool = b.wave_kind[ static_cast< std::size_t >( wave ) ] == water_kind_whirlpool;
            const int at_clock = whirlpool ? 0x74 : 0x7C;
            float clock = v.f( at_clock );
            alive = whirlpool ? whirlpool_tick( clock, static_cast< int >( v.word[ 1 ] ), scaled )
                              : directional_tick( clock, static_cast< int >( v.word[ 1 ] ), scaled );
            b.waves[ static_cast< std::size_t >( wave ) ].setf( at_clock, clock );
            break;
        }
        case water_kind_ramp:
        case water_kind_banked:
        case water_kind_bump:
        case water_kind_flow: alive = static_wave_tick( static_cast< int >( v.word[ 1 ] ) ); break;   // 0x59C380
        case water_kind_rectangular:
        case water_kind_circular:
        case water_kind_infinite:   // its vtable 0x7508C0's slot 2 is 0x59CC00 too
        {
            ocean_clock_t c{};
            std::memcpy( &c.time, &v.word[ 0x88 / 4 ], 8 );
            c.current = static_cast< int >( v.word[ 0xA4 / 4 ] );
            c.previous = static_cast< int >( v.word[ 0xA8 / 4 ] );
            std::memcpy( &c.buffer_time[ 0 ], &v.word[ 0xB0 / 4 ], 8 );
            std::memcpy( &c.buffer_time[ 1 ], &v.word[ 0xC0 / 4 ], 8 );
            ocean_call_t call{ &hooks, wave, 0, water_ocean_of( b, wave ), &c };
            alive = ocean_tick( c, static_cast< int >( v.word[ 1 ] ), scaled,
                []( void* u ) {
                    ocean_call_t& o = *static_cast< ocean_call_t* >( u );
                    const bool built = o.ocean && o.ocean->field.n != 0;
                    if ( built )
                        ocean_field_rebuild( o.ocean->field, o.ocean->params, o.clock->time, o.clock->current );
                    if ( o.hooks->rebuild )
                        o.hooks->rebuild( o.hooks->user, o.wave );   // told (after the rebuild when built)
                    else if ( !built )
                        ++o.missing;
                },
                &call );
            missing += call.missing;
            water_words_t& o = b.waves[ static_cast< std::size_t >( wave ) ];
            std::uint32_t t[ 2 ];
            std::memcpy( t, &c.time, 8 );
            o.set( 0x88, t[ 0 ] );
            o.set( 0x8C, t[ 1 ] );
            o.set( 0xA4, static_cast< std::uint32_t >( c.current ) );
            o.set( 0xA8, static_cast< std::uint32_t >( c.previous ) );
            for ( int k = 0; k < 2; ++k )
            {
                std::memcpy( t, &c.buffer_time[ k ], 8 );
                o.set( 0xB0 + 16 * k, t[ 0 ] );
                o.set( 0xB4 + 16 * k, t[ 1 ] );
            }
            break;
        }
        case water_kind_point:
        {
            point_wave_life_t life{};
            life.tail_width = v.f( 0x5C );
            life.outer_radius = v.f( 0x68 );
            life.elapsed = v.f( 0x78 );
            life.rate = v.f( 0x7C );
            life.has_callback = v.word[ 0x8C / 4 ] != 0;
            life.expired = ( v.word[ 0x90 / 4 ] & 0xFFu ) != 0;
            point_call_t call{ &b, &hooks, wave, static_cast< int >( v.word[ 1 ] ), 0 };
            alive = point_wave_tick( life, call.ref, scaled,
                []( void* u ) {
                    point_call_t& p = *static_cast< point_call_t* >( u );
                    if ( p.hooks->expire )
                        p.hooks->expire( p.hooks->user, *p.b, p.wave );
                    else
                        ++p.missing;
                    p.ref = static_cast< int >( p.b->waves[ static_cast< std::size_t >( p.wave ) ].word[ 1 ] );
                },
                &call );
            missing += call.missing;
            water_words_t& o = b.waves[ static_cast< std::size_t >( wave ) ];
            o.setf( 0x78, life.elapsed );
            o.word[ 0x90 / 4 ] = ( o.word[ 0x90 / 4 ] & ~0xFFu ) | ( life.expired ? 1u : 0u );
            break;
        }
        case water_kind_wake:
        case water_kind_flat_wake:   // 0x52C210: both records' ages (+0x88, +0xBC), the held copy kept in step
        {
            wake_wave_t k{};
            std::memcpy( &k.a, &v.word[ 0x58 / 4 ], sizeof( k.a ) );
            std::memcpy( &k.b, &v.word[ 0x8C / 4 ], sizeof( k.b ) );
            alive = wake_tick( k, static_cast< int >( v.word[ 1 ] ), scaled );
            water_words_t& o = b.waves[ static_cast< std::size_t >( wave ) ];
            o.setf( 0x88, k.a.age );
            o.setf( 0xBC, k.b.age );
            b.hold.waves[ static_cast< std::size_t >( wave ) ].wake_data.a.age = k.a.age;
            b.hold.waves[ static_cast< std::size_t >( wave ) ].wake_data.b.age = k.b.age;
            break;
        }
        default: ++missing; break;   // no class built here reaches this
        }
        if ( !alive )
        {
            water_built_remove( b, wave );
            if ( hooks.destroyed && wave < static_cast< int >( b.wave_gone.size( ) ) && b.wave_gone[ static_cast< std::size_t >( wave ) ] )
                hooks.destroyed( hooks.user, wave );
        }
        at = next;
    }
    if ( hooks.render )
        hooks.render( hooks.user );
    else
        ++missing;
    return missing + ( b.unread_dtors - unread_before );
}

// Is `cls` a water class (registry category "Water") this build does not
// make? Counted so the gap is visible.
inline bool water_class_unbuilt( const std::string& cls )
{
    static const char* const others[] = { "VuStaticWakeWaveEntity", "VuWakeWaveEntity" };
    for ( const char* o : others )
        if ( cls == o )
            return true;
    return false;
}

// THE EXPORT world.hxx calls once load_entity_tree has built m_tree: every
// water entity's slot 6 in 0x500B10's order over the whole tree. An entity
// whose world is unknown, or that has no transform or no property state, is
// skipped and counted (never stood in for). `transform_of` is game/types.hxx's
// entity_transform (this file is included above it).
using water_transform_of_fn = transform_component_t* ( * )( entity_t& );

inline void water_build_world( entity_t& root, water_built_t& out, water_transform_of_fn transform_of )
{
    water_built_init( out );
    const auto visit = [ & ]( const auto& self, entity_t& e ) -> void {
        for ( auto& child : e.m_children )
            self( self, *child );
        const water_kind_t kind = water_kind_of( e.m_type );
        if ( kind == water_kind_none )
        {
            out.skipped += water_class_unbuilt( e.m_type ) ? 1 : 0;
            return;
        }
        const transform_component_t* t = transform_of( e );
        const water_entity_state_t* s = static_cast< const water_entity_state_t* >( e.m_state.get( ) );
        if ( !t || !s || e.m_world_unknown )
        {
            ++out.skipped;
            return;
        }
        std::uint32_t transform[ k_water_transform_words ];
        water_transform_words( *t, transform );
        water_input_t in{};
        in.entity = s->value.word;
        in.transform = transform;
        in.water_map = s->water_map.c_str( );
        const std::size_t surfaces = out.surfaces.size( ), waves = out.waves.size( );
        water_build_entity( out, kind, in );
        water_built_t::entity_ref_t ref{};
        ref.entity = &e;
        ref.kind = kind;
        ref.surface = out.surfaces.size( ) > surfaces ? static_cast< int >( surfaces ) : -1;
        ref.wave = out.waves.size( ) > waves ? static_cast< int >( waves ) : -1;
        out.entities.push_back( ref );
    };
    visit( visit, root );
}

// THE WATER MAPS (each surface's +0x100, what 0x535E10 samples): retail
// resolves them while the entity loads (the WaterMap property's change hook
// 0x5DC4B0 - asset manager exists 0x5DC730, load 0x5DC7E0); the asset manager
// / file system is the platform layer, so `fetch` is the hook: true with the
// asset's bytes when the pack holds "VuWaterMapAsset/<name>". Each blob goes
// through VuWaterMapAsset's load 0x5C8650 (water_map_load). Call after
// water_build_world, before water_view_build. Returns the names given but not
// fetched or not loadable (their surfaces keep a null map, as retail's would
// when the manager lacks the asset).
using water_map_fetch_fn = bool ( * )( void* user, const std::string& asset, std::vector< std::uint8_t >& blob );

inline int water_build_maps( water_built_t& b, water_map_fetch_fn fetch, void* user )
{
    int missing = 0;
    for ( std::size_t i = 0; i < b.map_names.size( ) && i < b.maps.size( ); ++i )
    {
        b.maps[ i ] = nullptr;
        if ( b.map_names[ i ].empty( ) )
            continue;
        std::vector< std::uint8_t > blob;
        auto map = std::make_shared< water_map_t >( );
        if ( !fetch || !fetch( user, "VuWaterMapAsset/" + b.map_names[ i ], blob ) || !water_map_load( blob.data( ), blob.size( ), *map ) )
        {
            ++missing;
            continue;
        }
        b.maps[ i ] = map;
    }
    return missing;
}

// THE WATER CLASSES' getType SLOTS (6 bytes each: mov eax, descriptor; ret) -
// the static VuRTTI descriptor each returns (+0 the name, +4 the parent;
// statically initialised .data) and the chain it names. FACT: VuWaterRampEntity
// derives from VuWaterSurfaceEntity; VuActiveCameraTriggerEntity's 0x4AA970 is
// VuEntity's own.
struct water_type_t
{
    std::uint32_t address;
    std::uint32_t descriptor;
    const char* chain;
};

inline const water_type_t g_water_types[] = {
    { 0x4AA970u, 0x7367F8u, "VuEntity" },
    { 0x5226C0u, 0x7368B0u, "VuFluidsMeshAsset|VuAsset" },
    { 0x59AA90u, 0x736A70u, "VuWaterDirectionalWave|VuWaterWave" },
    { 0x59CA90u, 0x736A80u, "VuWaterBaseOceanWave|VuWaterWave" },
    { 0x59CAF0u, 0x736A90u, "VuWaterRectangularOceanWave|VuWaterBaseOceanWave|VuWaterWave" },
    { 0x59CB00u, 0x736A98u, "VuWaterCircularOceanWave|VuWaterBaseOceanWave|VuWaterWave" },
    { 0x5A1430u, 0x736AA0u, "VuWaterWhirlpoolWave|VuWaterWave" },
    { 0x5A2330u, 0x736AA8u, "VuWaterRampWave|VuWaterWave" },
    { 0x5A3FA0u, 0x736AB8u, "VuWaterBumpWave|VuWaterWave" },
    { 0x5A4D40u, 0x736AC0u, "VuWaterBankedTurnWave|VuWaterWave" },
    { 0x5A6380u, 0x736AC8u, "VuWaterWave" },
    { 0x5C7CF0u, 0x736B38u, "VuWaterMapAsset|VuAsset" },
    { 0x5D2AF0u, 0x736BE0u, "VuWaterTextureSettingsEntity|VuEntity" },
    { 0x5D3170u, 0x736BE8u, "VuBankedTurnWaveEntity|VuEntity" },
    { 0x5D3B40u, 0x736BF0u, "VuBumpWaveEntity|VuEntity" },
    { 0x5D4220u, 0x736BF8u, "VuPointWaveEntity|VuEntity" },
    { 0x5D6C20u, 0x736C18u, "VuWaterRampEntity|VuWaterSurfaceEntity|VuEntity" },
    { 0x5D7920u, 0x736C20u, "VuWhirlpoolWaveEntity|VuEntity" },
    { 0x5D8160u, 0x736C28u, "VuBaseOceanWaveEntity|VuEntity" },
    { 0x5D8190u, 0x736C38u, "VuRectangularOceanWaveEntity|VuBaseOceanWaveEntity|VuEntity" },
    { 0x5D81B0u, 0x736C40u, "VuCircularOceanWaveEntity|VuBaseOceanWaveEntity|VuEntity" },
    { 0x5D96E0u, 0x736C48u, "VuDirectionalWaveEntity|VuEntity" },
    { 0x5DA450u, 0x736C50u, "VuWaterSurfaceEntity|VuEntity" },
};

// VuWater 0x529470 (thiscall water; const VuVector3* point, a word it never reads, float* height, float* distance; retn
// 0x10): the nearest surface that reflects. Its one caller is the viewport loop 0x530F10 (per viewport +0x28 the point,
// then its reflection plane / blend - render side, not recreated). height = 0 first; best = FLT_MAX (0x6F7864). Per surface
// of the list +0x20 in list order (b.surface_list): only with the ProceduralReflection byte +0x38 set and the point's z
// above the box floor +0xD8 (comiss / jbe: false on NaN); the point in the surface's space (rows +0x90, retail's operand
// order), its z less (Reflection Height +0x30 - Max Wave Depth +0x28) x 0.5 (0x6F765C); per axis |c| (VuVector_abs) less
// +0xF0 / +0xF4 / (+0x28 + +0x30) x 0.5, maxss'ed with 0; d = ((dy^2 + dx^2) + dz^2) - Reflection Offset (+0x34) squared,
// maxss'ed with 0; best > d (comiss / jbe) -> height = the surface's +0x14 (its world z), best = d. distance = VuMath_sqrt(
// best ) - sqrt( FLT_MAX ) when nothing qualifies.
inline void water_reflection_nearest( const water_built_t& b, const float* p, float& height, float& distance )
{
    std::uint32_t h = 0u;
    float best = 3.4028234663852886e+38f;
    const __m128 zero = _mm_setzero_ps( );
    for ( int i : b.surface_list )
    {
        const water_words_t& s = b.surfaces[ static_cast< std::size_t >( i ) ];
        if ( ( s.word[ 0x38 / 4 ] & 0xFFu ) == 0u )
            continue;
        const float x = p[ 0 ], y = p[ 1 ], z = p[ 2 ];
        if ( !_mm_comigt_ss( _mm_set_ss( z ), _mm_set_ss( s.f( 0xD8 ) ) ) )
            continue;
        const float lx = ( ( s.f( 0xA0 ) * y + x * s.f( 0x90 ) ) + s.f( 0xB0 ) * z ) + s.f( 0xC0 );
        const float ly = ( ( s.f( 0x94 ) * x + s.f( 0xA4 ) * y ) + s.f( 0xB4 ) * z ) + s.f( 0xC4 );
        const float lz = ( ( ( s.f( 0x98 ) * x + s.f( 0xA8 ) * y ) + s.f( 0xB8 ) * z ) + s.f( 0xC8 ) ) - ( s.f( 0x30 ) - s.f( 0x28 ) ) * 0.5f;
        const float ax = water_abs( lx ) - s.f( 0xF0 );
        const float ay = water_abs( ly ) - s.f( 0xF4 );
        const float az = water_abs( lz ) - ( s.f( 0x28 ) + s.f( 0x30 ) ) * 0.5f;
        const float dx = _mm_cvtss_f32( _mm_max_ss( _mm_set_ss( ax ), zero ) );
        const float dy = _mm_cvtss_f32( _mm_max_ss( _mm_set_ss( ay ), zero ) );
        const float dz = _mm_cvtss_f32( _mm_max_ss( _mm_set_ss( az ), zero ) );
        const float d2 = ( ( dy * dy + dx * dx ) + dz * dz ) - s.f( 0x34 ) * s.f( 0x34 );
        const float d = _mm_cvtss_f32( _mm_max_ss( _mm_set_ss( d2 ), zero ) );
        if ( _mm_comigt_ss( _mm_set_ss( best ), _mm_set_ss( d ) ) )
        {
            h = s.word[ 0x14 / 4 ];
            best = d;
        }
    }
    std::memcpy( &height, &h, 4 );
    distance = _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( best ) ) );
}

#include <workspace/core/world/wake.hxx>
