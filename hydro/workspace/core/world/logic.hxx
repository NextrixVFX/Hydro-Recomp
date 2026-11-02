#pragma once

#include <workspace/core/assets/tree.hxx>
#include <workspace/core/boat/manager.hxx>
#include <workspace/core/boat/nuisance.hxx>
#include <workspace/core/boat/rtti.hxx>
#include <workspace/core/ui/button.hxx>
#include <workspace/core/ui/select.hxx>
#include <workspace/core/world/pfx.hxx>

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

// The engine's generic script / logic entities (the 0x5DC890 .. 0x5E56E0 block):
// integer math, compares, the Set* senders, the integer / string / counter
// stores, the sequencer, the trigger array, the random trigger, the platform /
// build filters and the switch. Recreated from their plug handlers and verified
// against retail (hydro checks/logic.hxx, case logic_plugs: every input plug
// fired through VuScriptInputPlug slot 2 0x404BE0 on entities made by the
// registry creates, the outputs linked to logged stubs).
//
// Shared shape. A handler is VuMethod1<T, VuRetVal, const VuParams&> (stdcall
// on the entity: VuRetVal* out, const VuParams& in; retn 8). It reaches an
// output plug by VuScriptComponent_findPlug 0x504770 on +0x5C and fires it
// through its slot 2 (VuScriptOutputPlug_fire 0x5023A0: {0, 0} for a void plug
// - a broadcast -, the first link's answer for a typed one, {0, 0} unlinked).
// A VuRetVal is { type, value } (type 1 int, 3 bool - only the value's LOW BYTE
// is written -, 4 a char*); a handler answering "void" writes ONLY the type word
// (0): the value word keeps what the caller's slot held. An output fired "with
// nothing" gets a fresh empty VuParams (size 0), "passing the params" the
// handler's own input.
//
// The ctor property sets are engine entity/schema.hxx's (every default 0; the
// VuString +0x60 empty, capacity 15) - the state here starts from that.
// NOT here (other areas): VuIfEntity, VuDelayEntity, VuMultiDelayEntity,
// VuTriggerForwarderEntity, VuOneShotFilterEntity, VuBooleanEntity,
// VuGenericEventEntity, VuOnProjectInitializedEntity (world, graph.hxx). The
// game tier's script entities are game/scripted.hxx's. The three game-tier
// filters VuIsTegraEntity / VuIsSkuEntity / VuHumanFilterEntity (0x42C..) are
// here too, their game environment through logic_game_t (case logic_game).

struct logic_retval_t
{
    std::uint32_t type = 0;
    std::uint32_t value = 0;
    const char* text = nullptr;   // type 4: the char* (the value word in retail)
};

// One entity's state: the words from +0x60 (k = (offset - 0x60) / 4) and, for
// the two string classes, the VuString at +0x60.
struct logic_entity_t
{
    std::uint32_t vtable = 0;
    std::uint8_t head[ 4 ] = { };     // +0x5C .. +0x5F (a class whose script component is not at +0x5C: game/scripted.hxx)
    std::uint32_t words[ 8 ] = { };
    std::string text;
};

// The game environment the three game-tier filters read (all optional; a
// missing piece a handler needs is counted in `unrecreated` and nothing fires).
struct logic_game_t
{
    // g_vugameutil 0x758FF8 +0x50: the game-data root (a VuJsonContainer).
    const value_t* game_data = nullptr;
    // The asset manager g 0x759004 vt+0x7C: the SKU string it answers.
    const std::string* sku = nullptr;
    void* user = nullptr;
    // The entity repository g 0x759080 vt+0x28: hash -> the entity (an image address; 0 = none).
    std::uint32_t ( *find )( void* user, std::uint32_t hash ) = nullptr;
    // A word of retail memory (the entity's vtable, its +0x220 driver, the driver's vtable).
    std::uint32_t ( *word )( void* user, std::uint32_t address ) = nullptr;
    int* unrecreated = nullptr;
};

// The managers game/scripted.hxx's handlers reach (the caller's; each one null: counted unrecreated, nothing happens).
struct logic_services_t
{
    std::function< void( const char* event, const ui_params_t& ) > broadcast;   // VuEventManager_broadcast 0x505690
    std::function< bool( ) > champ_data;                                        // the progress manager g 0x758FD8 vt+0x74
    std::function< void( ) > champ_clear;                                       // vt+0x68 ( the null container )
    std::function< void( const char* which ) > defaults;                        // settings: audio 0x4B4C60, pad 0x4B4E20, keys 0x4B4EA0, tilt 0x4B4F00, graphics 0x4B4FC0
    std::function< void( int method ) > input_defaults;                         // the input manager g 0x759038 vt+0x40 ( 0, method )
    std::function< void( ) > save_profile;                                      // 0x5547F0
    value_t* game_data = nullptr;                                               // gameutil +0x50
    const value_t* cloud_data = nullptr;                                        // the game save, g 0x758FA0 +0x20
    value_t* profile_saved = nullptr;                                           // g 0x759084 +8
    value_t* profile_temp = nullptr;                                            // +0x18
};

struct logic_hooks_t
{
    void* user = nullptr;
    // VuScriptOutputPlug_fire on the entity's output `plug` with `params`; returns what 0x5023A0 returns.
    logic_retval_t ( *fire )( void* user, logic_entity_t& e, const char* plug, const ui_params_t& params ) = nullptr;
    // The global VuRand 0x736810 (the same one the particles draw from - world/pfx.hxx pfx_rand_t).
    pfx_rand_t* rand = nullptr;
    const logic_game_t* game = nullptr;
    const logic_services_t* services = nullptr;
};

inline void logic_unrecreated( const logic_hooks_t& h )
{
    if ( h.game && h.game->unrecreated )
        ++*h.game->unrecreated;
}

using logic_handler_fn = void ( * )( logic_entity_t& e, logic_retval_t& out, const ui_params_t& in, const logic_hooks_t& h );

inline logic_retval_t logic_fire( logic_entity_t& e, const logic_hooks_t& h, const char* plug, const ui_params_t& p )
{
    return h.fire ? h.fire( h.user, e, plug, p ) : logic_retval_t{ };
}

inline logic_retval_t logic_fire_empty( logic_entity_t& e, const logic_hooks_t& h, const char* plug )
{
    const ui_params_t empty{ };
    return logic_fire( e, h, plug, empty );
}

// The getters behind the compares: an int when the answer's type is 1 (else 0,
// 0x5DE800); a bool = the value's low byte when the type is 1 (sic: not 3; else
// 0, 0x5DE2E0); a string when the type is 4 (else "" 0x64D5FA, 0x5DEBD0).
inline std::int32_t logic_get_int( logic_entity_t& e, const logic_hooks_t& h, const char* plug )
{
    const logic_retval_t r = logic_fire_empty( e, h, plug );
    return r.type == 1u ? static_cast< std::int32_t >( r.value ) : 0;
}

inline std::uint8_t logic_get_bool( logic_entity_t& e, const logic_hooks_t& h, const char* plug )
{
    const logic_retval_t r = logic_fire_empty( e, h, plug );
    return r.type == 1u ? static_cast< std::uint8_t >( r.value & 0xFFu ) : 0u;
}

inline const char* logic_get_string( logic_entity_t& e, const logic_hooks_t& h, const char* plug )
{
    const logic_retval_t r = logic_fire_empty( e, h, plug );
    return r.type == 4u && r.text ? r.text : "";
}

inline void logic_ret_bool( logic_retval_t& out, bool b )
{
    out.type = 3u;
    out.value = ( out.value & 0xFFFFFF00u ) | ( b ? 1u : 0u );
}

// VuAddIntegerConstantEntity "A + C" 0x5DCB20: C (+0x60) + A when A's type is 1.
inline void logic_add_constant( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    std::uint32_t v = e.words[ 0 ];
    const logic_retval_t a = logic_fire_empty( e, h, "A" );
    if ( a.type == 1u )
        v += a.value;
    out.type = 1u;
    out.value = v;
}

// VuAddIntegersEntity "A + B" 0x5DCEC0 / VuSubtractIntegersEntity "A - B" 0x5DD2F0:
// A fired first, then B; each counts only with type 1.
inline void logic_add_integers( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    const logic_retval_t a = logic_fire_empty( e, h, "A" );
    const logic_retval_t b = logic_fire_empty( e, h, "B" );
    std::uint32_t v = a.type == 1u ? a.value : 0u;
    if ( b.type == 1u )
        v += b.value;
    out.type = 1u;
    out.value = v;
}

inline void logic_subtract_integers( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    const logic_retval_t a = logic_fire_empty( e, h, "A" );
    const logic_retval_t b = logic_fire_empty( e, h, "B" );
    std::uint32_t v = a.type == 1u ? a.value : 0u;
    if ( b.type == 1u )
        v -= b.value;
    out.type = 1u;
    out.value = v;
}

// VuIntegerEntity "Get" 0x5DD680 (also VuCounterEntity "GetCount"): {1, +0x60}.
inline void logic_get_word0( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& )
{
    out.type = 1u;
    out.value = e.words[ 0 ];
}

// VuIntegerEntity "Set" 0x5DD9D0: the params' first record as an int
// (0x4237B0, ui/select.hxx ui_params_read_int: 0 unless an int record), then
// 0x5DDA10: a different value is stored and "OnChanged" fired with nothing.
inline void logic_integer_set( logic_entity_t& e, logic_retval_t& out, const ui_params_t& in, const logic_hooks_t& h )
{
    std::uint8_t view[ 132 ];
    std::memcpy( view, in.m_bytes, 128 );
    std::memcpy( view + 128, &in.m_size, 4 );
    ui_params_reader_t r( in );
    r.m_at = view;
    const std::uint32_t v = static_cast< std::uint32_t >( ui_params_read_int( r ) );
    if ( v != e.words[ 0 ] )
    {
        e.words[ 0 ] = v;
        logic_fire_empty( e, h, "OnChanged" );
    }
    out.type = 0u;
}

// VuCompareBooleansEntity "A == B" 0x5DE020 (B fetched first, then A).
inline void logic_compare_booleans( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    const std::uint8_t b = logic_get_bool( e, h, "B" );
    const std::uint8_t a = logic_get_bool( e, h, "A" );
    logic_ret_bool( out, a == b );
}

// VuCompareIntegersEntity "A > B" 0x5DE3D0, "A < B" 0x5DE410, "A == B" 0x5DE450
// (B first, then A; signed).
inline void logic_compare_greater( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    const std::int32_t b = logic_get_int( e, h, "B" );
    const std::int32_t a = logic_get_int( e, h, "A" );
    logic_ret_bool( out, a > b );
}

inline void logic_compare_less( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    const std::int32_t b = logic_get_int( e, h, "B" );
    const std::int32_t a = logic_get_int( e, h, "A" );
    logic_ret_bool( out, a < b );
}

inline void logic_compare_equal( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    const std::int32_t b = logic_get_int( e, h, "B" );
    const std::int32_t a = logic_get_int( e, h, "A" );
    logic_ret_bool( out, a == b );
}

// VuCompareStringsEntity "A == B" 0x5DE8F0 (B first, then A; an inlined strcmp).
inline void logic_compare_strings( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    const std::string b = logic_get_string( e, h, "B" );
    const std::string a = logic_get_string( e, h, "A" );
    logic_ret_bool( out, std::strcmp( a.c_str( ), b.c_str( ) ) == 0 );
}

// VuSetBooleanEntity "Trigger" 0x5E3150: a fresh VuParams + Value (+0x60) as a
// bool record (0x4051A0 copies the property's RAW byte - unlike ui/select.hxx's
// bool add, which takes a C++ bool), "Set" fired with it.
inline void logic_set_boolean( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    ui_params_t p{ };
    const std::uint32_t type = 3u;
    std::memcpy( p.m_bytes, &type, 4 );
    p.m_bytes[ 4 ] = static_cast< std::uint8_t >( e.words[ 0 ] & 0xFFu );
    p.m_size = 5;
    logic_fire( e, h, "Set", p );
    out.type = 0u;
}

// VuSetIntegerEntity "Trigger" 0x5E34D0: + Value as an int record (0x434E50).
inline void logic_set_integer( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    ui_params_t p{ };
    ui_params_add( p, 0u, e.words[ 0 ] );
    logic_fire( e, h, "Set", p );
    out.type = 0u;
}

// VuSetStringEntity "Trigger" 0x5E3860: + Value (+0x60 VuString) as a string
// record (inlined: dropped when strlen + 1 + 4 > 0x80).
inline void logic_set_string( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    ui_params_t p{ };
    p.add_string( e.text.c_str( ) );
    logic_fire( e, h, "Set", p );
    out.type = 0u;
}

// VuSequencerEntity "In" 0x5DF6A0: with +0x60 < 8 (signed): the output named
// sprintf( "%d", +0x60 + 1 ) - when the component has one - fired with the
// params, then +0x60 += 1. "Reset" 0x5DF0C0: +0x60 = 0.
inline bool logic_has_output( std::uint32_t vtable, const char* plug );

inline void logic_sequencer_in( logic_entity_t& e, logic_retval_t& out, const ui_params_t& in, const logic_hooks_t& h )
{
    if ( static_cast< std::int32_t >( e.words[ 0 ] ) < 8 )
    {
        const std::string name = std::to_string( static_cast< std::int32_t >( e.words[ 0 ] ) + 1 );
        if ( logic_has_output( e.vtable, name.c_str( ) ) )
            logic_fire( e, h, name.c_str( ), in );
        e.words[ 0 ] += 1u;
    }
    out.type = 0u;
}

inline void logic_sequencer_reset( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& )
{
    e.words[ 0 ] = 0u;
    out.type = 0u;
}

// VuTriggerArrayEntity "Trigger" 0x5E0C80: "1" .. "8" fired in order with the params.
inline void logic_trigger_array( logic_entity_t& e, logic_retval_t& out, const ui_params_t& in, const logic_hooks_t& h )
{
    for ( int i = 1; i <= 8; ++i )
    {
        const std::string name = std::to_string( i );
        if ( logic_has_output( e.vtable, name.c_str( ) ) )
            logic_fire( e, h, name.c_str( ), in );
    }
    out.type = 0u;
}

// VuRandomTriggerEntity "Trigger" 0x5E1690: sum = Weight 1 + .. + Weight 8
// (+0x60 .. +0x7C, addss in order); unless sum > 0 (comiss / jbe: NaN fails)
// nothing (type 0). Else x = VuRand (inlined, pfx_rand_t::next; VuMath_min
// 0x403260) * sum; the first i with Weight i >= x (comiss / jae) fires "i"
// with the params INTO the handler's own VuRetVal (both words: {0, 0} for these
// void plugs); x -= Weight i otherwise; none -> type 0.
inline void logic_random_trigger( logic_entity_t& e, logic_retval_t& out, const ui_params_t& in, const logic_hooks_t& h )
{
    float w[ 8 ];
    std::memcpy( w, e.words, 32 );
    float sum = w[ 0 ];
    for ( int i = 1; i < 8; ++i )
        sum = sum + w[ i ];
    if ( !( sum > 0.0f ) || !h.rand )
    {
        out.type = 0u;
        return;
    }
    float x = h.rand->next( ) * sum;
    for ( int i = 0; i < 8; ++i )
    {
        if ( w[ i ] >= x )
        {
            const std::string name = std::to_string( i + 1 );
            if ( logic_has_output( e.vtable, name.c_str( ) ) )
            {
                const logic_retval_t r = logic_fire( e, h, name.c_str( ), in );
                out.type = r.type;
                out.value = r.value;
                out.text = r.text;
                return;
            }
            out.type = 0u;
            return;
        }
        x = x - w[ i ];
    }
    out.type = 0u;
}

// VuPlatformFilterEntity "Trigger" 0x5E1CF0: "Metro" (this build's platform)
// fired with the params. VuBuildFilterEntity "Trigger" 0x5E2080: "Retail".
inline void logic_platform_filter( logic_entity_t& e, logic_retval_t& out, const ui_params_t& in, const logic_hooks_t& h )
{
    logic_fire( e, h, "Metro", in );
    out.type = 0u;
}

inline void logic_build_filter( logic_entity_t& e, logic_retval_t& out, const ui_params_t& in, const logic_hooks_t& h )
{
    logic_fire( e, h, "Retail", in );
    out.type = 0u;
}

// VuSwitchEntity: +0x60 Initial State, +0x64 the state (2 on, 1 off). "TurnOn"
// 0x5E24F0: unless on, "OnTurnedOn" with the params, state = 2; "TurnOff"
// 0x5E25B0: unless off, "OnTurnedOff", state = 1. Slot 6 0x5E24E0 (the game
// initialize): state = Initial State.
inline void logic_switch_on( logic_entity_t& e, logic_retval_t& out, const ui_params_t& in, const logic_hooks_t& h )
{
    if ( e.words[ 1 ] != 2u )
    {
        logic_fire( e, h, "OnTurnedOn", in );
        e.words[ 1 ] = 2u;
    }
    out.type = 0u;
}

inline void logic_switch_off( logic_entity_t& e, logic_retval_t& out, const ui_params_t& in, const logic_hooks_t& h )
{
    if ( e.words[ 1 ] != 1u )
    {
        logic_fire( e, h, "OnTurnedOff", in );
        e.words[ 1 ] = 1u;
    }
    out.type = 0u;
}

inline void logic_switch_init( logic_entity_t& e )
{
    e.words[ 1 ] = e.words[ 0 ];
}

// VuCounterEntity: +0x60 the count (Initial Count), +0x64 Target Count. "Inc"
// 0x5E4B50 / "Dec" 0x5E4B70 -> 0x5E5070( +1 / -1 ): count += delta (no clamp);
// equal to the target -> "Trigger" with the params; then "OnChanged" with the
// params, always.
inline void logic_counter_step( logic_entity_t& e, std::uint32_t delta, const ui_params_t& in, const logic_hooks_t& h )
{
    e.words[ 0 ] += delta;
    if ( e.words[ 0 ] == e.words[ 1 ] )
        logic_fire( e, h, "Trigger", in );
    logic_fire( e, h, "OnChanged", in );
}

inline void logic_counter_inc( logic_entity_t& e, logic_retval_t& out, const ui_params_t& in, const logic_hooks_t& h )
{
    logic_counter_step( e, 1u, in, h );
    out.type = 0u;
}

inline void logic_counter_dec( logic_entity_t& e, logic_retval_t& out, const ui_params_t& in, const logic_hooks_t& h )
{
    logic_counter_step( e, 0xFFFFFFFFu, in, h );
    out.type = 0u;
}

// VuStringEntity: +0x60 Value (VuString). The setter 0x5E55F0: unless equal
// (VuString_equals 0x404990), assign and fire "OnChanged" with nothing.
// "Set" 0x5E55B0: the params' string (readString 0x505260, ui/flow.hxx;
// "" unless a string record); "Clear" 0x5E5190: ""; "Get" 0x5E51B0: {4, text}.
inline void logic_string_assign( logic_entity_t& e, const char* s, const logic_hooks_t& h )
{
    if ( e.text == s )
        return;
    e.text = s;
    logic_fire_empty( e, h, "OnChanged" );
}

inline void logic_string_set( logic_entity_t& e, logic_retval_t& out, const ui_params_t& in, const logic_hooks_t& h )
{
    ui_params_reader_t r( in );
    const std::string s = r.read_string( );
    logic_string_assign( e, s.c_str( ), h );
    out.type = 0u;
}

inline void logic_string_clear( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    logic_string_assign( e, "", h );
    out.type = 0u;
}

inline void logic_string_get( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& )
{
    out.type = 4u;
    out.value = 0u;
    out.text = e.text.c_str( );
}

// VuIsTegraEntity "Trigger" 0x42DAA0: gameutil +0x50 ["Platform"]["Android"]
// ["IsTegra"] (0x4FBDF0 three times); a bool (type 3) whose byte is not 0 ->
// "True" with the params, anything else "False".
inline void logic_is_tegra( logic_entity_t& e, logic_retval_t& out, const ui_params_t& in, const logic_hooks_t& h )
{
    out.type = 0u;
    if ( !h.game || !h.game->game_data )
    {
        logic_unrecreated( h );
        return;
    }
    const value_t& v = member( member( member( *h.game->game_data, "Platform" ), "Android" ), "IsTegra" );
    const bool tegra = v.tag == 3u && static_cast< std::uint8_t >( v.integer ) != 0u;
    logic_fire( e, h, tegra ? "True" : "False", in );
}

// VuIsSkuEntity "Trigger" 0x42CFF0: the asset manager's SKU (vt+0x7C) against
// the entity's VuString +0x60 (0x404AC0 over the shorter length, then the
// lengths: equal strings) -> "True", else "False", with the params.
inline void logic_is_sku( logic_entity_t& e, logic_retval_t& out, const ui_params_t& in, const logic_hooks_t& h )
{
    out.type = 0u;
    if ( !h.game || !h.game->sku )
    {
        logic_unrecreated( h );
        return;
    }
    logic_fire( e, h, *h.game->sku == e.text ? "True" : "False", in );
}

// VuIsEventTypeEntity "Trigger" 0x42CB00: c = gameutil +0x50 ["GameData"];
// not an object -> the dev config g 0x75901C's getParam( "ActionGameData" )
// (vt+0x18 = 0x4FDB70: the static null 0x7593A0) or, without one, the null
// itself; c["EventType"] as a string (else the function-static empty string
// 0x819950) against Type (+0x60 VuString, ctor default "AttractEvent") ->
// "True", else "False", with the params.
inline void logic_is_event_type( logic_entity_t& e, logic_retval_t& out, const ui_params_t& in, const logic_hooks_t& h )
{
    out.type = 0u;
    if ( !h.game || !h.game->game_data )
    {
        logic_unrecreated( h );
        return;
    }
    static const value_t null{ };
    const value_t& data = member( *h.game->game_data, "GameData" );
    const value_t& t = member( data.tag == 6u ? data : null, "EventType" );
    const std::string type = t.tag == 4u ? t.text : std::string( );
    logic_fire( e, h, type == e.text ? "True" : "False", in );
}

// VuHumanFilterEntity "In" 0x42D370: with params size > 0 (signed) and a
// first record of type 11 (an entity), its hash (0x505370 = boat/nuisance.hxx
// vu_params_entity) -> the repository's entity; when its type chain holds
// VuBoatEntity 0x73628C and its driver (+0x220) answers vt+0x30 (isHuman:
// boat/manager.hxx boat_driver_kind) true -> "Out" with the params. Retail
// tests neither the entity nor the driver for null (a null faults): counted
// unrecreated here, as is a vtable outside the boat tables.
inline void logic_human_filter( logic_entity_t& e, logic_retval_t& out, const ui_params_t& in, const logic_hooks_t& h )
{
    out.type = 0u;
    if ( in.m_size <= 0 )
        return;
    std::uint32_t type;
    std::memcpy( &type, in.m_bytes, 4 );
    if ( type != 11u )
        return;
    if ( !h.game || !h.game->find || !h.game->word )
    {
        logic_unrecreated( h );
        return;
    }
    std::uint8_t view[ 132 ];
    std::memcpy( view, in.m_bytes, 128 );
    std::memcpy( view + 128, &in.m_size, 4 );
    const std::uint32_t entity = h.game->find( h.game->user, vu_params_entity( view ) );
    if ( !entity )
    {
        logic_unrecreated( h );
        return;
    }
    const std::uint32_t vtable = h.game->word( h.game->user, entity );
    if ( !boat_rtti_of( vtable ) )
    {
        logic_unrecreated( h );
        return;
    }
    if ( !boat_rtti_is( vtable, 0x73628Cu ) )
        return;
    const std::uint32_t driver = h.game->word( h.game->user, entity + 0x220u );
    boat_driver_kind_t kind;
    if ( !driver || !boat_driver_kind( h.game->word( h.game->user, driver ), kind ) )
    {
        logic_unrecreated( h );
        return;
    }
    if ( kind.human )
        logic_fire( e, h, "Out", in );
}

#include <workspace/core/game/scripted.hxx>

// ---------------------------------------------------------------------------
// THE TABLE the world's dispatcher calls by the entity's vtable (+0) and the
// input plug's name; g_logic_outputs gives each class's output plugs and their
// return types (VuScriptOutputPlug_init's second argument, plug_probe.json).
struct logic_plug_t
{
    std::uint32_t vtable;
    const char* cls;
    const char* plug;
    std::uint32_t retail;
    logic_handler_fn fn;
};

static const logic_plug_t g_logic_plugs[] = {
    { 0x75415Cu, "VuAddIntegerConstantEntity", "A + C", 0x5DCB20u, logic_add_constant },
    { 0x754128u, "VuAddIntegersEntity", "A + B", 0x5DCEC0u, logic_add_integers },
    { 0x7540F4u, "VuSubtractIntegersEntity", "A - B", 0x5DD2F0u, logic_subtract_integers },
    { 0x75420Cu, "VuIntegerEntity", "Set", 0x5DD9D0u, logic_integer_set },
    { 0x75420Cu, "VuIntegerEntity", "Get", 0x5DD680u, logic_get_word0 },
    { 0x754CA0u, "VuCompareBooleansEntity", "A == B", 0x5DE020u, logic_compare_booleans },
    { 0x754C6Cu, "VuCompareIntegersEntity", "A > B", 0x5DE3D0u, logic_compare_greater },
    { 0x754C6Cu, "VuCompareIntegersEntity", "A < B", 0x5DE410u, logic_compare_less },
    { 0x754C6Cu, "VuCompareIntegersEntity", "A == B", 0x5DE450u, logic_compare_equal },
    { 0x754C38u, "VuCompareStringsEntity", "A == B", 0x5DE8F0u, logic_compare_strings },
    { 0x754EE4u, "VuSetBooleanEntity", "Trigger", 0x5E3150u, logic_set_boolean },
    { 0x754EB0u, "VuSetIntegerEntity", "Trigger", 0x5E34D0u, logic_set_integer },
    { 0x754E7Cu, "VuSetStringEntity", "Trigger", 0x5E3860u, logic_set_string },
    { 0x754BD0u, "VuSequencerEntity", "In", 0x5DF6A0u, logic_sequencer_in },
    { 0x754BD0u, "VuSequencerEntity", "Reset", 0x5DF0C0u, logic_sequencer_reset },
    { 0x754B34u, "VuTriggerArrayEntity", "Trigger", 0x5E0C80u, logic_trigger_array },
    { 0x754ACCu, "VuRandomTriggerEntity", "Trigger", 0x5E1690u, logic_random_trigger },
    { 0x754A98u, "VuPlatformFilterEntity", "Trigger", 0x5E1CF0u, logic_platform_filter },
    { 0x754A64u, "VuBuildFilterEntity", "Trigger", 0x5E2080u, logic_build_filter },
    { 0x754A30u, "VuSwitchEntity", "TurnOn", 0x5E24F0u, logic_switch_on },
    { 0x754A30u, "VuSwitchEntity", "TurnOff", 0x5E25B0u, logic_switch_off },
    { 0x755224u, "VuCounterEntity", "Inc", 0x5E4B50u, logic_counter_inc },
    { 0x755224u, "VuCounterEntity", "Dec", 0x5E4B70u, logic_counter_dec },
    { 0x755224u, "VuCounterEntity", "GetCount", 0x5DD680u, logic_get_word0 },
    { 0x7552D0u, "VuStringEntity", "Set", 0x5E55B0u, logic_string_set },
    { 0x7552D0u, "VuStringEntity", "Clear", 0x5E5190u, logic_string_clear },
    { 0x7552D0u, "VuStringEntity", "Get", 0x5E51B0u, logic_string_get },
    { 0x7399E8u, "VuIsTegraEntity", "Trigger", 0x42DAA0u, logic_is_tegra },
    { 0x739A84u, "VuIsSkuEntity", "Trigger", 0x42CFF0u, logic_is_sku },
    { 0x739A50u, "VuHumanFilterEntity", "In", 0x42D370u, logic_human_filter },
    { 0x739AD0u, "VuIsEventTypeEntity", "Trigger", 0x42CB00u, logic_is_event_type },
    // boat batch 34: game/scripted.hxx, case game_plugs
    { 0x739A1Cu, "VuOncePerLapFilterEntity", "In", 0x42D6E0u, logic_once_per_lap },
    { 0x73755Cu, "VuSetTrackBranchProbabilities", "Trigger", 0x406690u, logic_track_branch },
    { 0x7398B0u, "VuAwardFlowEntity", "Next", 0x42EFF0u, logic_announce< g_event_award_next > },
    { 0x73EB08u, "VuStartActionGameUIAction", "Trigger", 0x470A80u, logic_start_action },
    { 0x73EAB8u, "VuEndActionGameUIAction", "Trigger", 0x470B80u, logic_announce< g_event_end_action > },
    { 0x73EA80u, "VuResumeActionGameUIAction", "Trigger", 0x470C70u, logic_announce< g_event_resume_action > },
    { 0x73EA48u, "VuRestartActionGameUIAction", "Trigger", 0x470D60u, logic_announce< g_event_restart_action > },
    { 0x73E968u, "VuShutdownAppUIAction", "Trigger", 0x471350u, logic_announce< g_event_shutdown > },
    { 0x73E9D8u, "VuDoesChampDataExistUIAction", "Trigger", 0x4710D0u, logic_champ_data },
    { 0x73E750u, "VuDoesChampionshipDataExistUIAction", "Trigger", 0x4723F0u, logic_championship_data },
    { 0x73E9A0u, "VuClearChampDataUIAction", "Trigger", 0x4712A0u, logic_champ_clear },
    { 0x73E718u, "VuClearChampionshipDataUIAction", "Trigger", 0x4712A0u, logic_champ_clear },
    { 0x73E788u, "VuClearGameResultsUIAction", "Trigger", 0x4721A0u, logic_clear_results },
    { 0x73E1E0u, "VuSetDefaultSettingsEntity", "Trigger", 0x46C210u, logic_default_settings },
    { 0x73E1ACu, "VuSaveSettingsEntity", "Trigger", 0x46C470u, logic_save_settings },
    { 0x739918u, "VuIsAchievementUnlockedEntity", "Trigger", 0x42E570u, logic_achievement_unlocked },
    { 0x755144u, "VuPersistentBooleanEntity", "Set", 0x5E4300u, logic_persist_set_bool },
    { 0x755144u, "VuPersistentBooleanEntity", "SetTrue", 0x5E3EC0u, logic_persist_true },
    { 0x755144u, "VuPersistentBooleanEntity", "SetFalse", 0x5E3EF0u, logic_persist_false },
    { 0x755144u, "VuPersistentBooleanEntity", "Get", 0x5E3F20u, logic_persist_get_bool },
    { 0x755110u, "VuPersistentIntegerEntity", "Set", 0x5E45B0u, logic_persist_set_int },
    { 0x755110u, "VuPersistentIntegerEntity", "Get", 0x5E4640u, logic_persist_get_int },
    { 0x7550DCu, "VuPersistentStringEntity", "Set", 0x5E48A0u, logic_persist_set_string },
    { 0x7550DCu, "VuPersistentStringEntity", "Get", 0x5E4920u, logic_persist_get_string },
};

struct logic_output_t
{
    std::uint32_t vtable;
    const char* plug;
    std::uint32_t type;   // 0 void (a broadcast), 1 int, 3 bool, 4 string
};

static const logic_output_t g_logic_outputs[] = {
    { 0x75415Cu, "A", 1u },
    { 0x754128u, "A", 1u }, { 0x754128u, "B", 1u },
    { 0x7540F4u, "A", 1u }, { 0x7540F4u, "B", 1u },
    { 0x75420Cu, "OnChanged", 0u },
    { 0x754CA0u, "A", 3u }, { 0x754CA0u, "B", 3u },
    { 0x754C6Cu, "A", 1u }, { 0x754C6Cu, "B", 1u },
    { 0x754C38u, "A", 4u }, { 0x754C38u, "B", 4u },
    { 0x754EE4u, "Set", 0u }, { 0x754EB0u, "Set", 0u }, { 0x754E7Cu, "Set", 0u },
    { 0x754BD0u, "1", 0u }, { 0x754BD0u, "2", 0u }, { 0x754BD0u, "3", 0u }, { 0x754BD0u, "4", 0u },
    { 0x754BD0u, "5", 0u }, { 0x754BD0u, "6", 0u }, { 0x754BD0u, "7", 0u }, { 0x754BD0u, "8", 0u },
    { 0x754B34u, "1", 0u }, { 0x754B34u, "2", 0u }, { 0x754B34u, "3", 0u }, { 0x754B34u, "4", 0u },
    { 0x754B34u, "5", 0u }, { 0x754B34u, "6", 0u }, { 0x754B34u, "7", 0u }, { 0x754B34u, "8", 0u },
    { 0x754ACCu, "1", 0u }, { 0x754ACCu, "2", 0u }, { 0x754ACCu, "3", 0u }, { 0x754ACCu, "4", 0u },
    { 0x754ACCu, "5", 0u }, { 0x754ACCu, "6", 0u }, { 0x754ACCu, "7", 0u }, { 0x754ACCu, "8", 0u },
    { 0x754A98u, "Win32", 0u }, { 0x754A98u, "Xbox360", 0u }, { 0x754A98u, "Android", 0u },
    { 0x754A98u, "Ios", 0u }, { 0x754A98u, "Metro", 0u }, { 0x754A98u, "BB10", 0u },
    { 0x754A64u, "Debug", 0u }, { 0x754A64u, "Release", 0u }, { 0x754A64u, "Retail", 0u },
    { 0x754A30u, "OnTurnedOn", 0u }, { 0x754A30u, "OnTurnedOff", 0u },
    { 0x755224u, "Trigger", 0u }, { 0x755224u, "OnChanged", 0u },
    { 0x7552D0u, "OnChanged", 0u },
    { 0x7399E8u, "True", 0u }, { 0x7399E8u, "False", 0u },
    { 0x739A84u, "True", 0u }, { 0x739A84u, "False", 0u },
    { 0x739A50u, "Out", 0u },
    { 0x739AD0u, "True", 0u }, { 0x739AD0u, "False", 0u },
    { 0x739A1Cu, "Out", 0u }, { 0x73755Cu, "Set", 0u }, { 0x7398B0u, "Busy", 0u }, { 0x7398B0u, "NotBusy", 0u },
    { 0x73E9D8u, "Yes", 0u }, { 0x73E9D8u, "No", 0u }, { 0x73E750u, "Yes", 0u }, { 0x73E750u, "No", 0u },
    { 0x739918u, "True", 0u }, { 0x739918u, "False", 0u },
};

inline const logic_output_t* logic_output( std::uint32_t vtable, const char* plug )
{
    for ( const logic_output_t& o : g_logic_outputs )
        if ( o.vtable == vtable && std::strcmp( o.plug, plug ) == 0 )
            return &o;
    return nullptr;
}

inline bool logic_has_output( std::uint32_t vtable, const char* plug ) { return logic_output( vtable, plug ) != nullptr; }

// The class's vtable by its type name (0 = not a logic class here).
inline std::uint32_t logic_vtable_of( const char* cls )
{
    for ( const logic_plug_t& p : g_logic_plugs )
        if ( std::strcmp( p.cls, cls ) == 0 )
            return p.vtable;
    return 0u;
}

inline const logic_plug_t* logic_plug( std::uint32_t vtable, const char* plug )
{
    for ( const logic_plug_t& p : g_logic_plugs )
        if ( p.vtable == vtable && std::strcmp( p.plug, plug ) == 0 )
            return &p;
    return nullptr;
}

// One input plug fired (VuScriptInputPlug slot 2 0x404BE0 -> the VuMethod1):
// false when (vtable, plug) is not in the table (the caller's other handlers).
inline bool logic_dispatch( logic_entity_t& e, const char* plug, logic_retval_t& out, const ui_params_t& in, const logic_hooks_t& h )
{
    const logic_plug_t* p = logic_plug( e.vtable, plug );
    if ( !p )
        return false;
    p->fn( e, out, in, h );
    return true;
}

// A new entity as its ctor leaves it (every property default 0, the VuString
// empty); the loader then applies the authored properties to words / text.
inline logic_entity_t logic_construct( std::uint32_t vtable )
{
    logic_entity_t e{ };
    e.vtable = vtable;
    // VuIsEventTypeEntity's ctor 0x42C850: Type = "AttractEvent" (0x67803C).
    if ( vtable == 0x739AD0u )
        e.text = "AttractEvent";
    // game/scripted.hxx's ctors: 0x42D530 +0x60 = -1; 0x4063F0 both bools true, both probabilities 1; VuStartActionGameUIAction's Type "AttractEvent".
    if ( vtable == 0x739A1Cu )
        e.words[ 0 ] = 0xFFFFFFFFu;
    if ( vtable == 0x73755Cu )
    {
        e.words[ 0 ] = 0x101u;
        e.words[ 1 ] = 0x3F800000u;
        e.words[ 2 ] = 0x3F800000u;
    }
    if ( vtable == 0x73EB08u )
        e.text = "AttractEvent";
    return e;
}

// The classes' getType slots (6 bytes: mov eax, descriptor; ret): the static
// VuRTTI descriptor (+0 the name, +4 the parent) and the chain it names - the
// logic classes above, the game filters (with VuIsEventTypeEntity 0x42C7D0),
// VuGroupEntity, VuControlEntity, VuControlKeyframeEntity (case logic_types).
struct logic_type_t
{
    std::uint32_t address;
    std::uint32_t descriptor;
    const char* chain;
};

inline const logic_type_t g_logic_types[] = {
    { 0x5DC890u, 0x736C58u, "VuAddIntegerConstantEntity|VuEntity" },
    { 0x5DCC30u, 0x736C60u, "VuAddIntegersEntity|VuEntity" },
    { 0x5DD060u, 0x736C68u, "VuSubtractIntegersEntity|VuEntity" },
    { 0x5DD670u, 0x736C70u, "VuIntegerEntity|VuEntity" },
    { 0x5DE010u, 0x736C80u, "VuCompareBooleansEntity|VuEntity" },
    { 0x5DE3C0u, 0x736C88u, "VuCompareIntegersEntity|VuEntity" },
    { 0x5DE8E0u, 0x736C90u, "VuCompareStringsEntity|VuEntity" },
    { 0x5E2F30u, 0x736CE8u, "VuSetBooleanEntity|VuEntity" },
    { 0x5E32B0u, 0x736CF0u, "VuSetIntegerEntity|VuEntity" },
    { 0x5E3630u, 0x736CF8u, "VuSetStringEntity|VuEntity" },
    { 0x5DF0B0u, 0x736CA0u, "VuSequencerEntity|VuEntity" },
    { 0x5E0740u, 0x736CB8u, "VuTriggerArrayEntity|VuEntity" },
    { 0x5E1050u, 0x736CC8u, "VuRandomTriggerEntity|VuEntity" },
    { 0x5E1890u, 0x736CD0u, "VuPlatformFilterEntity|VuEntity" },
    { 0x5E1D80u, 0x736CD8u, "VuBuildFilterEntity|VuEntity" },
    { 0x5E2130u, 0x736CE0u, "VuSwitchEntity|VuEntity" },
    { 0x5E4B40u, 0x736D20u, "VuCounterEntity|VuEntity" },
    { 0x5E5180u, 0x736D28u, "VuStringEntity|VuEntity" },
    { 0x42D810u, 0x736338u, "VuIsTegraEntity|VuEntity" },
    { 0x42CCE0u, 0x736320u, "VuIsSkuEntity|VuEntity" },
    { 0x42D150u, 0x736328u, "VuHumanFilterEntity|VuEntity" },
    { 0x42C7D0u, 0x736318u, "VuIsEventTypeEntity|VuEntity" },
    { 0x5F05A0u, 0x736DB8u, "VuGroupEntity|VuEntity" },
    { 0x5CCA40u, 0x736B90u, "VuControlEntity|VuEntity" },
    { 0x5CC700u, 0x736B88u, "VuControlKeyframeEntity|VuEntity" },
    // batch 23: VuControlComponent's vtable 0x757EA0 slot 0 (slot 1 0x619F50 answers its name "Control" 0x6F6FC8, slot 2
    // 0x619F70 the deleting dtor over 0x619FA0: the records freed, VuComponent 0x738CE0, the +4 property list destroyed -
    // control_component_t's own destructor), the motion / audio / settings classes of sound.hxx / keymotion.hxx /
    // settings.hxx.
    { 0x619F60u, 0x736E48u, "VuControlComponent|VuComponent" },
    { 0x619FF0u, 0x736E50u, "VuMotionEntity|VuEntity" },
    { 0x5CE2C0u, 0x736BA8u, "VuKeyframeMotionEntity|VuMotionEntity|VuEntity" },
    { 0x5CE080u, 0x736BA0u, "VuKeyframeEntity|VuEntity" },
    { 0x5F3550u, 0x736DE8u, "VuAudioEmitterEntity|VuEntity" },
    { 0x5EF500u, 0x736D98u, "VuAmbientLightEntity|VuEntity" },
    { 0x5EEA00u, 0x736D90u, "VuDirectionalLightEntity|VuEntity" },
    { 0x5F4790u, 0x736E00u, "VuSubstituteAssetEntity|VuEntity" },
    // batch 24: VuLensWaterEmitterEntity's getType 0x5E64B0 (`mov eax, 0x736D40; ret`; lens.hxx). The rows above the inventory
    // missed (cited only as C++ literals): 0x619FF0 0x5CE2C0 0x5CE080 0x5EF500 0x5EEA00.
    { 0x5E64B0u, 0x736D40u, "VuLensWaterEmitterEntity|VuEntity" },
    // batch 28: the entity / component framework's getType slots (entity slot 1, VuComponent's own slot 0 0x423730 - the
    // base vtable 0x738CE0's; its descriptor names no parent).
    { 0x42E6B0u, 0x736360u, "VuControlMethodEntity|VuEntity" },
    { 0x5DDB60u, 0x736C78u, "VuIfEntity|VuEntity" },
    { 0x5DECB0u, 0x736C98u, "VuOneShotFilterEntity|VuEntity" },
    { 0x5DFCF0u, 0x736CB0u, "VuMultiDelayEntity|VuEntity" },
    { 0x5E0DA0u, 0x736CC0u, "VuTriggerForwarderEntity|VuEntity" },
    { 0x5EFFF0u, 0x736DB0u, "VuGenericEventEntity|VuEntity" },
    { 0x5F0780u, 0x736DC0u, "VuSceneEntity|VuEntity" },
    { 0x5F5840u, 0x736E10u, "VuPlayAnimationEntity|VuEntity" },
    { 0x5EF840u, 0x736DA0u, "VuOnProjectInitializedEntity|VuEntity" },
    { 0x423730u, 0x736920u, "VuComponent" },
    // the framework components' slot 0 (0x736800 is the descriptor script.hxx keys VuScriptComponent by), VuDelayEntity,
    // VuBooleanEntity, VuStaticPfxEntity
    { 0x4238E0u, 0x736928u, "Vu2dLayoutComponent|VuComponent" },
    { 0x503A10u, 0x736800u, "VuScriptComponent|VuComponent" },
    { 0x50B8C0u, 0x736808u, "Vu3dLayoutComponent|VuComponent" },
    { 0x531D80u, 0x7368E0u, "VuInstigatorComponent|VuComponent" },
    { 0x534170u, 0x7368E8u, "VuTransformComponent|VuComponent" },
    { 0x5DF7A0u, 0x736CA8u, "VuDelayEntity|VuEntity" },
    { 0x5E5780u, 0x736D30u, "VuBooleanEntity|VuEntity" },
    { 0x5EA930u, 0x736D70u, "VuStaticPfxEntity|VuEntity" },
};

// VuControlComponent's slot 1 0x619F50 (mov eax, 0x6F6FC8; ret): the component's name.
inline const char* control_component_name( )
{
    return "Control";
}

// The components' slot 1 (mov eax, <C string>; ret): the name a component is keyed by (batch 28: the framework's).
struct component_name_t
{
    std::uint32_t address;
    const char* name;
};

inline const component_name_t g_component_names[] = {
    { 0x619F50u, "Control" },      // VuControlComponent
    { 0x4238D0u, "2dLayout" },     // Vu2dLayoutComponent (0x6F365C)
    { 0x50B8B0u, "3dLayout" },     // Vu3dLayoutComponent (0x6F630C)
    { 0x531D70u, "Instigator" },   // VuInstigatorComponent (0x6F633C)
    { 0x534160u, "Transform" },    // VuTransformComponent (0x6F6348)
    { 0x503A00u, "Script" },       // VuScriptComponent (0x6F6304)
};
// VuTickManagerImpl's small slots (vtable 0x74BEBC; world/tick.hxx is the world agent's model of the manager - these
// read its fields, added here, not there). Verified: hydro checks/logic.hxx verify_tick_slots (case tick_slots).
//   slot 14 0x4F6EA0 isPaused: byte +0x20 != 0, or the pause count +0x24 > 0 (signed: a negative count is not paused).
//   slot 15 0x4F6EC0 getDeltaTime: fld +0x28 - this frame's capped dt (the race's g 0x759008 vt+0x3C).
//   slot 16 0x4F6ED0 getRealDeltaTime: fld +0x2C - the raw clock difference.
//   0x4F6D40 release: g 0x759008 = 0. Slot 0 0x4F7010 (deleting dtor): the phase list's nodes (0x4F77F0) and head
//   freed, the base vtable 0x736EE8, delete with flags & 1 = std::list's destructor here. The second vtable 0x74BF04's
//   0x4F74F0 is the dev keyboard callback (key 0x1A toggles +0x20 pause; 0x19 single step when paused, else toggles
//   +0x21 slow motion) - DEV, STOPPED.
#include <workspace/core/world/tick.hxx>

inline bool tick_is_paused( const tick_manager_t& tm )
{
    return tm.paused || tm.pause_count > 0;
}

inline float tick_delta( const tick_manager_t& tm )
{
    return tm.dt;
}

inline float tick_real_delta( const tick_manager_t& tm )
{
    return tm.raw;
}