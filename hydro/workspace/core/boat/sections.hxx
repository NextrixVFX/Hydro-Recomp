#pragma once

#include <workspace/core/assets/tree.hxx>
#include <workspace/core/boat/nuisance.hxx>
#include <workspace/core/boat/parts.hxx>
#include <workspace/core/entity/property.hxx>

#include <cstdint>
#include <cstring>
#include <functional>
#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <vector>

// The BoatDB section readers. postLoad's 0x420A70 calls each of them twice -
// with BoatDB Default's section, then with the boat's own - so a key the boat
// leaves out keeps Default's value, and one neither has keeps the ctor's.
// Each reader is a run of four shapes Vu inlines over its JSON container
// (0x4FBDF0 = container[key], the static null 0x7593A0 when absent):
//
//   float   tag 1 -> cvtdq2ps, 2 -> the bits, 7 -> fild / fstp; else unchanged
//   vec3    0x4FDC60 into the field (X / Y / Z, earlier lanes kept on a miss)
//   colour  0x4FDEC0 into the field (stored only when R, G and B all parse)
//   string  tag 4 -> 0x401DB0 assign(node's string, 0, npos)
//
// The listings were read into their ops by hydro-tools/codegen/sections.py,
// which accepts only those four shapes and stops at anything else; the stops
// (the driver-class branches) were read by hand. The objects are the ctor
// images of parts.hxx / construct.hxx; their std::string fields are kept by
// offset in `boat_strings_t` (MSVC's string storage is the library's, served
// by std::string - not modelled byte for byte).

using boat_strings_t = std::map< int, std::string >;

// The five driver classes' predicate slots 12..17 (vtable +0x30..+0x44), each
// 0x4052D0 (`xor al, al; ret`) or 0x404BD0 (`mov al, 1; ret`), read off the
// vtables: VuAiDriver 0x737B5C, VuHumanDriver 0x737F44, VuAiUiDriver
// 0x737CDC, VuAiNuisanceDriver 0x737C4C, VuGhostDriver 0x737DDC.
struct boat_driver_t
{
    bool slot[ 6 ];   // [k] = vtable slot 12 + k
};

inline boat_driver_t boat_driver_of( std::uint32_t vtable )
{
    switch ( vtable )
    {
    case 0x00737B5Cu: return { { false, true, true, false, false, false } };
    case 0x00737F44u: return { { true, false, true, false, false, false } };
    case 0x00737CDCu: return { { false, true, true, true, false, false } };
    case 0x00737C4Cu: return { { false, true, true, false, true, false } };
    case 0x00737DDCu: return { { false, false, true, false, false, true } };
    default: return { { false, false, false, false, false, false } };
    }
}

inline void section_float( std::uint8_t* image, int at, const value_t& value )
{
    parts_db_float( image, at, value );
}

inline void section_vec3( std::uint8_t* image, int at, const value_t& value )
{
    float lanes[ 3 ];
    std::memcpy( lanes, image + at, sizeof( lanes ) );
    vec3( value, lanes );
    std::memcpy( image + at, lanes, sizeof( lanes ) );
}

// 0x4FDEC0: R, G, B required (any other tag: false, nothing stored), A
// optional (0xFF); each channel an int, a float truncated or an int64's low
// dword (property_channel), packed R, G, B, A from the lowest byte up.
inline void section_colour( std::uint8_t* image, int at, const value_t& value )
{
    std::int32_t r, g, b;
    if ( !property_channel( member( value, "R" ), r ) || !property_channel( member( value, "G" ), g ) ||
         !property_channel( member( value, "B" ), b ) )
        return;
    std::int32_t a = 0xFF;
    property_channel( member( value, "A" ), a );
    image_put32( image, at, ( static_cast< std::uint32_t >( r ) & 0xFF ) | ( ( static_cast< std::uint32_t >( g ) & 0xFF ) << 8 ) |
                            ( ( static_cast< std::uint32_t >( b ) & 0xFF ) << 16 ) | ( ( static_cast< std::uint32_t >( a ) & 0xFF ) << 24 ) );
}

// Tag 4 -> assign. (Retail skips the assign when the node's string IS the
// field - never true for a document's node.)
inline void section_string( boat_strings_t& strings, int at, const value_t& value )
{
    if ( value.tag == 4 )
        strings[ at ] = value.text;
}

// VuBoatDB_readCameraSection 0x409AF0 (thiscall, the 0x409750 camera; retn 4).
inline void boat_read_camera( std::uint8_t* camera, const value_t& section )
{
    section_vec3( camera, 0x244, member( section, "Target Local Offset" ) );
    section_float( camera, 0x254, member( section, "Target World Z Offset" ) );
    section_float( camera, 0x258, member( section, "Ideal Distance" ) );
    section_float( camera, 0x25C, member( section, "Ideal Pitch" ) );
    section_float( camera, 0x264, member( section, "Lag Distance" ) );
    section_float( camera, 0x268, member( section, "Max Roll Angle" ) );
    section_float( camera, 0x26C, member( section, "Roll Period" ) );
    section_vec3( camera, 0x270, member( section, "Cockpit Cam Offset" ) );
    section_vec3( camera, 0x280, member( section, "Nose Cam Offset" ) );
    section_vec3( camera, 0x290, member( section, "Reverse Cam Offset" ) );
}

// VuBoatDB_readHullSection 0x417480 (thiscall, the 0x415D60 hull - its +0 the
// boat, whose +0x220 is the driver; retn 4).
inline void boat_read_hull( std::uint8_t* hull, boat_strings_t& strings, const value_t& section, const boat_driver_t& driver )
{
    section_string( strings, 0x4, member( section, "Fluids Mesh Asset" ) );
    section_vec3( hull, 0x1C, member( section, "Hydrodynamic Center" ) );
    section_vec3( hull, 0x2C, member( section, "Aerodynamic Center" ) );
    section_vec3( hull, 0x3C, member( section, "Stability" ) );
    static const struct { int at; const char* key; } floats[] = {
        { 0x4C, "Water Drag Density" }, { 0x50, "Water Buoyancy Density" }, { 0x54, "Air Density" },
        { 0x58, "Density Ratio" }, { 0x5C, "Density Ratio Speed" }, { 0x60, "Lin Vel Speed 0" },
        { 0x64, "Lin Vel Factor 0" }, { 0x68, "Lin Vel Speed 1" }, { 0x6C, "Lin Vel Factor 1" },
        { 0x70, "Lat Skin Friction Coeff" }, { 0x74, "Long Skin Friction Coeff" }, { 0x78, "Power Slide Coeff" },
        { 0x7C, "Slow Steering Boat Speed" }, { 0x80, "Slow Steering Speed" }, { 0x84, "Slow Steering Accel" },
        { 0x88, "Fast Steering Boat Speed" }, { 0x8C, "Fast Steering Speed" }, { 0x90, "Fast Steering Accel" },
        { 0x94, "Aero Lift" }, { 0x98, "Wake Magnitude" }, { 0x1D0, "Wake Range Start Ratio" },
        { 0x1D4, "Wake Range Decay Ratio" }, { 0x1D8, "Wake Physics Scale" } };
    for ( const auto& f : floats )
        section_float( hull, f.at, member( section, f.key ) );
    section_vec3( hull, 0x9C, member( section, "Wake Position" ) );
    section_vec3( hull, 0xAC, member( section, "Air Control Speed" ) );
    section_vec3( hull, 0xBC, member( section, "Air Control Accel" ) );
    section_float( hull, 0xCC, member( section, "Air Control Start" ) );
    section_float( hull, 0xD0, member( section, "Air Control Max" ) );
    section_float( hull, 0x158, member( member( section, "Audio" ), "Bounce Threshold" ) );
    // 0x417BA4: the audio set by the driver class - slots 15 and 13 "MenuAI",
    // slot 15 "MenuPlayer", slots 12 and 14 "Player", else "AI".
    const value_t& audio = member( section, "Audio" );
    const char* set = driver.slot[ 3 ] && driver.slot[ 1 ] ? "MenuAI"
                    : driver.slot[ 3 ]                     ? "MenuPlayer"
                    : driver.slot[ 0 ] && driver.slot[ 2 ] ? "Player"
                                                           : "AI";
    const value_t& events = member( audio, set );
    section_string( strings, 0xD4, member( events, "SlipEvent" ) );
    section_string( strings, 0xEC, member( events, "BounceEvent" ) );
    section_string( strings, 0x104, member( events, "BigBounceEvent" ) );
    section_string( strings, 0x11C, member( events, "WindEvent" ) );
    section_float( hull, 0x154, member( events, "SlipThreshold" ) );
    section_float( hull, 0x158, member( events, "BounceThreshold" ) );
}

// VuBoatDB_readDefaultSection 0x4205A0 = VuBoatEntity vtable slot 15
// (thiscall, the boat - its +0x220 the driver; retn 4).
inline void boat_read_default( std::uint8_t* boat, boat_strings_t& strings, const value_t& section, const boat_driver_t& driver )
{
    section_string( strings, 0xC4, member( section, "Model Asset" ) );
    section_string( strings, 0xDC, member( section, "LOD 1 Model Asset" ) );
    section_string( strings, 0xF4, member( section, "LOD 2 Model Asset" ) );
    section_float( boat, 0x10C, member( section, "LOD 1 Distance" ) );
    section_float( boat, 0x110, member( section, "LOD 2 Distance" ) );
    section_colour( boat, 0x114, member( section, "Ambient Color" ) );
    section_colour( boat, 0x118, member( section, "Diffuse Color" ) );
    section_string( strings, 0x11C, member( section, "Surface Type" ) );
    section_float( boat, 0x134, member( section, "Mass" ) );
    section_float( boat, 0x144, member( section, "InertiaFactor" ) );
    section_float( boat, 0x138, member( section, "Min Jump Speed" ) );
    section_float( boat, 0x13C, member( section, "Vertical Jump Speed" ) );
    section_float( boat, 0x140, member( section, "Destruction Speed" ) );
    section_string( strings, 0x148, member( section, "Collision Mesh Asset" ) );
    section_string( strings, 0x160, member( section, "Class" ) );
    section_string( strings, 0x178, member( section, "StringID" ) );
    section_vec3( boat, 0x190, member( member( section, "SlalomBounds" ), "Pos" ) );
    section_float( boat, 0x1A0, member( member( section, "SlalomBounds" ), "Radius" ) );
    // 0x420950: the audio set - slots 12 and 14 "Player", else "AI".
    const value_t& events = member( member( section, "Audio" ), driver.slot[ 0 ] && driver.slot[ 2 ] ? "Player" : "AI" );
    section_string( strings, 0x1A4, member( events, "BoostMisfire" ) );
    section_string( strings, 0x1BC, member( events, "Jump" ) );
    section_string( strings, 0x1D4, member( events, "JumpMisfire" ) );
    section_string( strings, 0x1EC, member( events, "Destruction" ) );
    section_string( strings, 0x204, member( events, "Drafting" ) );
}

// btConvexTriangleMeshShape_load_AI 0x410360 - IDA's name; it is the
// Animation section reader (thiscall, the 0x40FE00 object at boat +0x230 -
// its +0 the boat, whose +0x220 is the driver; retn 4). postLoad calls
// 0x4108C0 first (releases the refcounted assets at +4..+0x10) and, when the
// owner's flags have bit 2 (set by VuEntity::gameInitialize 0x500B10 - clear
// on a fresh boat's load), 0x410610 after (the animation asset loads) - both
// asset-side, not recreated here.
inline void boat_read_animation( std::uint8_t* animation, boat_strings_t& strings, const value_t& section, const boat_driver_t& driver )
{
    section_string( strings, 0x14, member( section, "ThrustAsset" ) );
    section_string( strings, 0x2C, member( section, "TurnAsset" ) );
    section_string( strings, 0x44, member( section, "TransformAsset" ) );
    section_float( animation, 0x5C, member( section, "ThrustRevRate" ) );
    section_float( animation, 0x60, member( section, "ThrustIdleRate" ) );
    section_float( animation, 0x64, member( section, "ThrustFwdRate" ) );
    section_float( animation, 0x68, member( section, "ThrustAccel" ) );
    section_float( animation, 0x6C, member( section, "TransformOpenTime" ) );
    section_float( animation, 0x70, member( section, "TransformCloseTime" ) );
    // 0x410548: the audio set - slots 12 and 14 "Player", else "AI".
    const value_t& events = member( member( section, "Audio" ), driver.slot[ 0 ] && driver.slot[ 2 ] ? "Player" : "AI" );
    section_string( strings, 0x7C, member( events, "BoostHardwareOpen" ) );
    section_string( strings, 0x94, member( events, "BoostHardwareClose" ) );
    section_string( strings, 0xAC, member( events, "BoostShutdown" ) );
}
// 0x4FBEA0, the container's hasMember: a map (tag 6) holding the key.
inline bool section_has( const value_t& value, const char* key )
{
    if ( value.tag != 6 )
        return false;
    for ( const auto& field : value.object )
        if ( field.first == key )
            return true;
    return false;
}

// The list reads 0x4140C0 inlines: a list (tag 5) has (end - begin) >> 4
// elements, anything else none; element k of a list in range, else the
// static null container.
inline int section_count( const value_t& value )
{
    return value.tag == 5 ? static_cast< int >( value.list.size( ) ) : 0;
}

inline const value_t& section_at( const value_t& value, int k )
{
    static const value_t null{};
    return value.tag == 5 && k >= 0 && k < static_cast< int >( value.list.size( ) ) ? value.list[ k ] : null;
}

// The engine object 0x413590 builds (boat +0x22C, the tick path's O), with
// the three containers its section fills: the Power Curve (a Vu array: data
// +0x30, size +0x34, capacity +0x38 - the size and capacity stay in the
// image), the Drives (std::vector of 32-byte records at +0x3C / +0x40 /
// +0x44: TestLocation then ForceLocation, each a vec3 and a w lane the code
// never writes - an unset stack word), and the +0xF8 std::vector of 8-byte
// per-drive records 0x414760 fills each tick (only its length is set here).
struct boat_engine_t
{
    boat_413590_t object;
    std::vector< std::uint32_t > curve;
    std::vector< std::array< std::uint32_t, 8 > > drives;
    std::size_t records = 0;
    boat_strings_t strings;
};

// VuBoatDB_readEngineSection 0x4140C0 (thiscall, the engine - its +0 the
// boat, whose +0x220 is the driver; retn 4).
inline void boat_read_engine( boat_engine_t& e, const value_t& section, const boat_driver_t& driver )
{
    std::uint8_t* b = e.object.image;
    static const struct { int at; const char* key; } floats[] = {
        { 0x8, "Max Steering Angle" }, { 0xC, "Max Forward Speed" }, { 0x10, "Max Reverse Speed" },
        { 0x14, "Max Thrust" }, { 0x18, "Air Boost Long Thrust" }, { 0x1C, "Air Boost Lat Thrust" },
        { 0x20, "Reverse Ratio" }, { 0x24, "Out of Water Ratio" }, { 0x28, "Boost Speed Modifier" },
        { 0x2C, "Boost Thrust Modifier" } };
    for ( const auto& f : floats )
        section_float( b, f.at, member( section, f.key ) );
    // 0x41432B: the audio set, as the hull's (slots 15 + 13, 15, 12 + 14).
    const char* set = driver.slot[ 3 ] && driver.slot[ 1 ] ? "MenuAI"
                    : driver.slot[ 3 ]                     ? "MenuPlayer"
                    : driver.slot[ 0 ] && driver.slot[ 2 ] ? "Player"
                                                           : "AI";
    const value_t& events = member( member( section, "Audio" ), set );
    section_string( e.strings, 0x4C, member( events, "RunEvent" ) );
    section_string( e.strings, 0xAC, member( events, "BoostLoop" ) );
    section_string( e.strings, 0x64, member( events, "StartEvent" ) );
    section_string( e.strings, 0x7C, member( events, "RevEvent" ) );
    section_string( e.strings, 0x94, member( events, "BoostInitiate" ) );
    // 0x41446E: each number of the Power Curve list APPENDED (any other tag
    // skipped). The array grows to max(size + 1, capacity + capacity / 2)
    // (_aligned_malloc(.., 16), memcpy_s, _aligned_free).
    if ( section_has( section, "Power Curve" ) )
    {
        for ( int k = 0; k < section_count( member( section, "Power Curve" ) ); ++k )
        {
            const value_t& v = section_at( member( section, "Power Curve" ), k );
            float lane;
            if ( v.tag == 1 )
                lane = static_cast< float >( v.integer );
            else if ( v.tag == 2 )
                std::memcpy( &lane, &v.real, 4 );
            else if ( v.tag == 7 )
                lane = static_cast< float >( v.int64 );
            else
                continue;
            std::int32_t size, capacity;
            std::memcpy( &size, b + 0x34, 4 );
            std::memcpy( &capacity, b + 0x38, 4 );
            const std::int32_t need = size + 1;
            if ( need > capacity )
            {
                const std::int32_t half = capacity / 2 + capacity;   // cdq / sub / sar: halved toward zero
                const std::int32_t grown = need > half ? need : half;   // cmovg
                if ( grown > capacity )
                    image_put32( b, 0x38, static_cast< std::uint32_t >( grown ) );
            }
            std::uint32_t bits;
            std::memcpy( &bits, &lane, 4 );
            e.curve.push_back( bits );
            image_put32( b, 0x34, static_cast< std::uint32_t >( need ) );
        }
    }
    // 0x414593: each Drives element as a record on the stack - lanes 0-2 and
    // 4-6 zeroed, 3 and 7 never written (kept 0 here; the check masks them) -
    // TestLocation's vec3 into 0, ForceLocation's into 4, then push_back.
    if ( section_has( section, "Drives" ) )
    {
        for ( int k = 0; k < section_count( member( section, "Drives" ) ); ++k )
        {
            float record[ 8 ] = { 0, 0, 0, 0, 0, 0, 0, 0 };
            vec3( member( section_at( member( section, "Drives" ), k ), "TestLocation" ), record );
            vec3( member( section_at( member( section, "Drives" ), k ), "ForceLocation" ), record + 4 );
            std::array< std::uint32_t, 8 > bits;
            std::memcpy( bits.data( ), record, sizeof( record ) );
            e.drives.push_back( bits );
        }
    }
}

// VuBoatDB_loadEngine 0x413700 (thiscall, the engine; the boat's section,
// then Default's; retn 8): the five strings emptied, the curve's size and
// the drives cleared, readEngineSection on DEFAULT'S section first, then the
// boat's, then the +0xF8 records resized to the drive count. When the
// owner's flags (+0x14) have bit 2 (set by VuEntity::gameInitialize 0x500B10;
// clear on a fresh boat's load - the police loader 0x42FDC0 loads first) -
// the five FMOD events are released and the run event restarted: an audio
// HOOK (FMOD::Event release / start through the imports 0x64C334 / 0x64C340 /
// 0x64C354 and 0x50FBF0), not recreated.
inline void boat_load_engine( boat_engine_t& e, const value_t& section, const value_t& fallback, const boat_driver_t& driver,
    std::uint32_t owner_flags )
{
    for ( int at : { 0x4C, 0x64, 0x7C, 0x94, 0xAC } )
        e.strings[ at ] = "";
    image_put32( e.object.image, 0x34, 0 );
    e.curve.clear( );
    e.drives.clear( );
    boat_read_engine( e, fallback, driver );
    boat_read_engine( e, section, driver );
    e.records = e.drives.size( );
    if ( owner_flags & 4 )
    {
        // HOOK: the FMOD event release / restart (platform audio).
    }
}
// ---- The engine's dtor body (boat +0x22C; VuBoatEntity's dtor 0x41CB10 runs it, then operator delete), on retail's
// words (boat batch 28) ----
//
// 0x41CF20 (thiscall engine): the +0xF8 std::vector's block freed when its begin is non-null (begin / end / capacity
// = 0 then), the five 8-byte audio events at +0xC4 released by `eh vector destructor iterator` 0x630EF0 (last to
// first) with 0x40D820, the five std::strings at +0x4C reset the same way with 0x401C30, the +0x3C std::vector as
// +0xF8, then _aligned_free( +0x30 ) whatever it holds (the Power Curve's Vu array data).
// 0x40D820 (thiscall event { +0 byte, +4 FMOD::Event* }): with an event, FMOD::Event::setCallback( 0, 0 ), then
// with the byte set FMOD::Event::stop( true ) (retail: edi = byte ? 2 : 0, stop( edi == 2 ) only when edi != 0);
// +4 = 0. Nothing at all without an event. The FMOD calls are audio PLATFORM hooks; the imports hooks too.
struct boat_engine_dtor_hooks_t
{
    std::function< void( std::uint32_t ) > free_block;                   // operator delete
    std::function< void( std::uint32_t ) > aligned_free;                 // _aligned_free
    std::function< void( std::uint32_t ) > set_callback_none;            // FMOD::Event::setCallback( event, 0, 0 )
    std::function< void( std::uint32_t, bool ) > stop;                   // FMOD::Event::stop( event, immediate )
};

// 0x40D820.
inline void boat_audio_event_release( const vu_words_t& w, std::uint32_t at, const boat_engine_dtor_hooks_t& h )
{
    const bool playing = ( w.get( at ) & 0xFFu ) != 0;
    const std::uint32_t event = w.get( at + 4 );
    if ( event == 0 )
        return;
    if ( h.set_callback_none )
        h.set_callback_none( event );
    if ( playing && h.stop )
        h.stop( event, true );
    w.put( at + 4, 0u );
}

// 0x41CF20.
inline void boat_engine_destroy( const vu_words_t& w, std::uint32_t self, const boat_engine_dtor_hooks_t& h )
{
    auto vector_free = [ & ]( std::uint32_t at ) {
        if ( const std::uint32_t begin = w.get( at ) )
        {
            if ( h.free_block )
                h.free_block( begin );
            w.put( at, 0u );
            w.put( at + 4, 0u );
            w.put( at + 8, 0u );
        }
    };
    vector_free( self + 0xF8 );
    for ( std::uint32_t k = 5; k-- > 0; )
        boat_audio_event_release( w, self + 0xC4 + 8u * k, h );
    for ( std::uint32_t k = 5; k-- > 0; )
    {
        const std::uint32_t s = self + 0x4C + 0x18u * k;   // 0x401C30
        if ( w.get( s + 0x14 ) >= 0x10u && h.free_block )
            h.free_block( w.get( s ) );
        w.put( s + 0x14, 0xFu );
        w.put( s + 0x10, 0u );
        w.put( s, w.get( s ) & 0xFFFFFF00u );
    }
    vector_free( self + 0x3C );
    if ( h.aligned_free )
        h.aligned_free( w.get( self + 0x30 ) );
}