#pragma once

#include <workspace/core/ui/config.hxx>
#include <workspace/core/ui/flow.hxx>

#include <cstdint>
#include <cstring>

// The front end's selectors: the entities that WRITE what the GameData
// builders (config.hxx) read from gameutil +80 - BoatConfig.CurPlayerIndex,
// BoatConfig.PlayerBoats[i], GameConfig.<option> - transliterated from the
// disassembly.
//
// VuSetBoatConfigPlayerEntity (ctor 0x44ECF0 over VuEntity 0x500710, vtable
// 0x73C654; no properties of its own): one VuScriptComponent (order 150,
// enabled) with two input plugs bound through 0x44F400 (the output
// initialiser, then VuScriptInputPlug 0x737138 and a VuMethod1 0x73C634):
//   "SetSinglePlayer" -> 0x44EF10
//   "SetIndex"        -> 0x44EEA0
// No output plugs, no broadcast; both return VuRetVal 0 (its type word only:
// the second dword is never written).

// VuParams' int read 0x4237B0 (thiscall on the reader { cursor, remaining }):
// nothing left -> 0; else the record's type word (4 bytes, memcpy_s) and, only
// when it is 0 (an int), the type and the 4-byte value are stepped over and
// the value returned; any other type -> 0, nothing consumed. The reads stay
// inside VuParams' 128 bytes + its size word (+128) for every record the
// buffer can hold; `m_at` past that is not reached by any VuParams (size <=
// 128).
inline int ui_params_read_int( ui_params_reader_t& r )
{
    if ( r.m_remaining <= 0 )
        return 0;
    std::uint32_t type = 0xFFFFFFFFu;
    std::memcpy( &type, r.m_at, 4 );
    if ( type != 0 )
        return 0;
    r.m_at += 4;
    r.m_remaining -= 4;
    int value = 0;
    std::memcpy( &value, r.m_at, 4 );
    r.m_at += 4;
    r.m_remaining -= 4;
    return value;
}

// An int member: 0x4FBB10 clear, tag 1, the value (+8). Batch 25: json.hxx.
inline void ui_json_set_int( value_t& v, int value ) { json_set_int( v, value ); }

// A bool member: 0x4FBB10 clear, tag 3, the byte (+8).
inline void ui_json_set_bool( value_t& v, bool value )
{
    ui_json_clear( v );
    v.tag = 3;
    v.kind = value_t::kind_t::integer;
    v.integer = value ? 1 : 0;
}

// "SetIndex" 0x44EEA0 (stdcall VuRetVal* out, const VuParams& p): the first
// record read as an int (0x4237B0 over { p, p +128 }) -> gameutil +80
// (`root`) ["BoatConfig"] ["CurPlayerIndex"] (0x4FC090 twice: each made a map
// if it is not) = that int.
inline std::uint32_t ui_set_boat_player_index( value_t& root, const ui_params_t& p )
{
    // 0x4237B0's reader takes VuParams' bytes and its size word together.
    std::uint8_t view[ 132 ];
    std::memcpy( view, p.m_bytes, 128 );
    std::memcpy( view + 128, &p.m_size, 4 );
    ui_params_reader_t r( p );
    r.m_at = view;
    const int index = ui_params_read_int( r );
    ui_json_set_int( ui_json_child( ui_json_child( root, "BoatConfig" ), "CurPlayerIndex" ), index );
    return 0;
}

// "SetSinglePlayer" 0x44EF10 (stdcall VuRetVal* out, const VuParams&; the
// params are never read): root ["BoatConfig"] ["CurPlayerIndex"] = int 0;
// then for k = 0 .. 3 (the loop steps a byte offset 0, 16, 32, 48 < 64): root
// made a map (inline 0x4FBB10 + tag 6 + a fresh map when it is not one),
// ["BoatConfig"] (0x4FC970) made a map, ["PlayerBoats"] made an array (tag 5,
// a fresh vector), grown to k + 1 null elements when k >= its size
// (0x4FC880), element k made a map, its ["Active"] = bool (k == 0). Other
// members of the elements, and elements past the fourth, are kept.
inline std::uint32_t ui_set_single_player( value_t& root )
{
    ui_json_set_int( ui_json_child( ui_json_child( root, "BoatConfig" ), "CurPlayerIndex" ), 0 );
    for ( int k = 0; k < 4; ++k )
    {
        value_t& boats = ui_json_child( ui_json_child( root, "BoatConfig" ), "PlayerBoats" );
        value_t& entry = ui_json_at( boats, static_cast< std::size_t >( k ) );
        ui_json_set_bool( ui_json_child( entry, "Active" ), k == 0 );
    }
    return 0;
}

// VuSetBoatConfigPlayerEntity's SetIndex 0x44EEA0: BoatConfig.CurPlayerIndex (cleared, then an int) = the first record as an int (0x4237B0; else 0).
inline void ui_boat_player_index( value_t& root, const ui_params_t& params )
{
    ui_params_reader_t r( params );
    ui_json_set_int( ui_json_child( ui_json_child( root, "BoatConfig" ), "CurPlayerIndex" ), ui_params_read_int( r ) );
}

// ---------------------------------------------------------------------------
// VuBaseConfigEntity (ctor 0x4471C0, vtable 0x73C4D4) - the base of the game
// and boat selectors (VuGameConfigEntity ctor 0x45CC80 / vtable 0x73D1DC,
// VuBoatConfigEntity 0x44C9F0 / 0x73C69C). Its options document (+0x2A0,
// built by slot 4 0x448120 -> 0x449640 from the "OptionType" OptionDB entry
// through 0x449B20 / 0x44A4C0: NOT recreated - an input here) is an array of
// options { Name, ComboBoxID, Choices: [ { Value, Options? } ] }. THE
// GameConfig WRITER is the option walk 0x44B0D0: GameConfig[option Name] =
// the current choice's Value, for every option, recursing into that choice's
// own Options - so "Track", "Course", "Championship" are written here, by name
// (no literal "Track" store exists). The current choice is 0x44BD50: the first
// choice whose Value equals GameConfig[Name] (0x4FC230), else the class's
// slot 22 (+88) fallback - 0x4471B0 = 0 for VuGameConfigEntity: before any
// selection the FIRST choice. (Before any selection gameutil +80 itself is
// null: VuGameUtil's ctor 0x4D2810 zeroes +0x50.)

// An array element (the inline test every caller makes: tag 5, 0 <= i < size)
// else the static null container 0x7593A0.
inline const value_t& ui_json_element( const value_t& v, int i )
{
    static const value_t null{ };
    if ( v.tag == 5 && i >= 0 && static_cast< std::size_t >( i ) < v.list.size( ) )
        return v.list[ static_cast< std::size_t >( i ) ];
    return null;
}

// 0x4FBC40: an array's size, 0 for anything else.
inline int ui_json_size( const value_t& v )
{
    return v.tag == 5 ? static_cast< int >( v.list.size( ) ) : 0;
}

// 0x4FCAB0 (a map's member by a VuString key, the whole string; inserted null
// when absent) after 0x4FC6B0.
inline value_t& ui_json_child_s( value_t& v, const std::string& key ) { return json_member( v, key ); }   // forwards (batch 26)

// 0x4FC450 asInt (1 the int, 2 cvttss2si, 7 the low dword, else 0).
inline int ui_json_as_int( const value_t& v )
{
    if ( v.tag == 1 )
        return v.integer;
    if ( v.tag == 2 )
        return static_cast< int >( v.real );
    if ( v.tag == 7 )
        return static_cast< int >( static_cast< std::uint32_t >( static_cast< std::uint64_t >( v.int64 ) ) );
    return 0;
}

// 0x4FC230 (thiscall a, b): different tags -> false; 0 true; 1 asInt ==; 2 the
// floats by ucomiss (NaN unequal); 3 the bytes; 4 the strings (0x407790:
// compare of the shorter length, then the lengths); 5 same size, then every
// element pair (a_i, b_i); 6 same size, then for each i the i-th keys in
// std::map order (0x407400: any difference fails) and a[key] == b[key]
// (0x4FBDA0 lookups by a's key); 7 the int64s (0x4FC520). Tags above 7 false.
inline bool ui_json_equal( const value_t& a, const value_t& b )
{
    if ( a.tag != b.tag )
        return false;
    switch ( a.tag )
    {
    case 0:
        return true;
    case 1:
        return ui_json_as_int( a ) == ui_json_as_int( b );
    case 2:
        return a.real == b.real;
    case 3:
        return ( a.integer & 0xFF ) == ( b.integer & 0xFF );
    case 4:
        return a.text == b.text;
    case 5:
        if ( a.list.size( ) != b.list.size( ) )
            return false;
        for ( std::size_t i = 0; i < a.list.size( ); ++i )
            if ( !ui_json_equal( a.list[ i ], b.list[ i ] ) )
                return false;
        return true;
    case 6:
    {
        if ( a.object.size( ) != b.object.size( ) )
            return false;
        const std::vector< std::string > ka = ui_json_keys( a ), kb = ui_json_keys( b );
        for ( std::size_t i = 0; i < ka.size( ); ++i )
        {
            if ( ka[ i ] != kb[ i ] )
                return false;
            if ( !ui_json_equal( ui_json_find( a, ka[ i ] ), ui_json_find( b, ka[ i ] ) ) )
                return false;
        }
        return true;
    }
    case 7:
        return a.int64 == b.int64;
    default:
        return false;
    }
}

// What the selectors consult that is not theirs. All HOOKS (profile side /
// platform): `unlock` = g_vuunlockmanagerimpl 0x758FE8 slot +92 / +96 (two
// strings: c unused) and +100 / +104 (three); `license` = the license
// manager 0x758FAC slot +0x18; `next_choice` = VuGameConfigEntity's slot 26
// (+104) 0x45E300 - recreated (ui_game_config_next below); set it only to
// answer that slot from outside: true with the choice text to select.
// The boat selector's further reads, HOOKS too: `progress` = the progress
// manager 0x758FD8 slot +132 (one string; `has_progress` false = the global is
// null), `store_beta` = 0x4D3170 (the asset factory's slot +124 build name
// compared with "StoreBeta": build configuration), `has_unlock` = whether
// g_vuunlockmanagerimpl is set (0x44B780 / 0x449050 / 0x4491C0 test it; the
// cases always set it - the null branches are NOT exercised).
struct ui_profile_t
{
    std::function< bool( int slot, const std::string& a, const std::string& b, const std::string& c ) > unlock;
    std::function< bool( ) > license;
    std::function< bool( const value_t& option, std::string& out ) > next_choice;
    std::function< bool( const std::string& value ) > progress;
    std::function< bool( ) > store_beta;
    std::function< bool( const std::string& asset ) > package;  // the asset factory 0x759004 slot +0x90
    // The progress manager's best placing (rank out; it starts at 0x7FFFFFFF):
    // slot +88 (championship name) and slot +48 (mode, track, course, 0, ...).
    std::function< int( const std::string& championship ) > champ_rank;
    std::function< int( const std::string& mode, const std::string& track, const std::string& course ) > race_rank;
    // The same progress slots' boolean answers (VuGameConfigEntity slot 17
    // 0x45D720 reads only those): +48 (mode, track, course, 0, ...), +88
    // (championship), +124 (the Ghost track).
    std::function< bool( const std::string& mode, const std::string& track, const std::string& course ) > race_done;
    std::function< bool( const std::string& championship ) > champ_done;
    std::function< bool( const std::string& track ) > ghost_done;
    bool has_progress = true;
    bool has_unlock = true;
};

// A combo box record (VuBaseConfigEntity +0x2B0 .. +0x2B4, 52 bytes each,
// built by slot 4's 0x44A4C0 - an input here): +40 the focused byte, +41 /
// +42 two bytes the walk never writes, +44 the current choice, +48 the shown
// choice (float), +28 the choice entries (28 bytes: a VuString, +24 the
// locked byte).
struct ui_config_combo_t
{
    std::uint8_t m_focused = 0;
    std::uint8_t m_b41 = 0;
    std::uint8_t m_b42 = 0;
    int m_index = 0;
    float m_shown = 0.0f;
    std::vector< std::uint8_t > m_locked;
    std::string m_string_id;                  // +4
    std::vector< std::string > m_choice_ids;  // each entry's VuString
};

// The selector entity's fields the walk reads.
struct ui_config_entity_t
{
    bool m_single_player = false;  // +0x274 "Single Player"
    bool m_force_online = false;   // +0x275 "Force Online"
    bool m_refresh = false;        // +0x2CC, set around 0x448140's refresh
    int m_focus = 0;               // +0x2C8, the focused option's index
    std::vector< const value_t* > m_options;     // +0x2BC, the walk's option vector
    std::vector< ui_config_combo_t > m_combos;   // +0x2B0
    bool m_fault = false;          // a path where retail reads / writes outside a container
};

// The selector's class virtuals the base reaches (VuBaseConfigEntity's vtable
// slots; VuGameConfigEntity 0x73D1DC and VuBoatConfigEntity 0x73C69C fill
// them). (name, value) are the option's Name and a choice's Value texts.
struct ui_config_class_t
{
    std::function< bool( const std::string& name, const std::string& value ) > blocked;       // slot 17 +68
    std::function< bool( const std::string& name, const std::string& value ) > locked;        // slot 18 +72
    std::function< bool( const std::string& name, const std::string& value ) > locked_trial;  // slot 19 +76
    std::function< bool( const std::string& name, const std::string& value ) > unavailable;   // slot 20 +80
    std::function< int( const value_t& option, const std::string& current ) > fallback;       // slot 22 +88
    std::function< void( std::string& out ) > info;                                           // slot 23 +92
    std::function< std::string( const std::string& name ) > last;                             // slot 24 +96
    std::function< void( const std::string& name, const std::string& value ) > remember;      // slot 25 +100
    std::function< bool( const value_t& option, std::string& out ) > next_choice;             // slot 26 +104
    std::function< bool( ) > changed;                                                         // slot 12 +48
};

// What the selectors post. All but `fire` are HOOKS of the platform /
// engine: VuEventManager_broadcast 0x505690 (immediate), the touch register
// 0x53FB90 (g_vumetrotouch +4 push of { this + 92, 0 }: platform input), the
// FMOD event start (audio device). `fire` is an output plug's
// VuScriptOutputPlug_fire 0x5023A0 on the plug VuScriptComponent_findPlug
// 0x504770 finds by name - false when the entity declares no such plug
// (retail calls through the null it gets).
struct ui_config_hooks_t
{
    std::function< void( std::uint32_t hash, const ui_params_t& p ) > broadcast;
    std::function< void( ) > touch;
    std::function< bool( const std::string& plug, const ui_params_t& p ) > fire;
    std::function< void( const char* event ) > sound;
    // VuGameConfigEntity: the trophy model set 0x520080 (a model asset property:
    // the asset factory) and the 3D draw manager g 0x7590B8 slot +40 add.
    std::function< void( const char* model ) > trophy;
    std::function< void( ) > draw_add;
    // Batch 26 (slot 7): the touch removal 0x53FA60 (g_vumetrotouch, this + 92)
    // and the 3D draw manager g 0x7590B8 slot +44 remove. Unset: nothing.
    std::function< void( ) > touch_remove;
    std::function< void( ) > draw_remove;
};

// VuParams' bool add 0x4051A0: with size + 5 <= 128, type 3 then one byte.
inline void ui_params_add_bool( ui_params_t& p, bool value )
{
    if ( p.m_size + 5 > 128 )
        return;
    const std::uint32_t type = 3;
    std::memcpy( p.m_bytes + p.m_size, &type, 4 );
    p.m_bytes[ p.m_size + 4 ] = value ? 1 : 0;
    p.m_size += 5;
}

// VuEventManager's named broadcast: FNV-1a of the name (inlined at each site).
inline void ui_broadcast( const ui_config_hooks_t& h, const char* name, const ui_params_t& p ) { h.broadcast( fsm_hash( name ), p ); }

// gameutil (g 0x758FF8): +0x50 the game-data root, +0x60 a byte that stops
// both of the walk's profile branches, +0x64 the last screen operation.
struct ui_config_env_t
{
    value_t* m_root = nullptr;
    bool m_busy = false;
    std::string m_op;
};

// VuGameConfigEntity slot 24 (+96) 0x45E210: root ["LastGameConfigSelection"]
// [name] (lookups, nothing created) as a string, else "".
inline std::string ui_game_config_last( const value_t& root, const std::string& name )
{
    return ui_json_text( ui_json_find( member( root, "LastGameConfigSelection" ), name ) );
}

// VuGameConfigEntity slot 25 (+100) 0x45E2B0: root ["LastGameConfigSelection"]
// (0x4FC090, made a map) [name] (0x4FCAB0) = the text up to its first NUL.
inline void ui_game_config_remember( value_t& root, const std::string& name, const std::string& value )
{
    ui_json_set_string( ui_json_child_s( ui_json_child( root, "LastGameConfigSelection" ), name ), value.c_str( ) );
}

// VuGameConfigEntity slot 18 (+72) 0x45D0C0: whether a choice is locked. Not
// with Force Online, nor when root ["Online"] is anything but null. Mode =
// GameConfig.Mode's text. "Track": Race / TimeTrial -> !unlock+100(mode,
// value, ""); Slalom -> none of unlock+100(mode, value, CourseA / CourseB /
// CourseC) (in that order, stopping at the first true); RubberDucky / Ghost ->
// !unlock+100("Race", value, ""); any other mode false. "Course": only for
// Slalom, !unlock+100(mode, GameConfig.Track's text, value). "Championship":
// !unlock+92(name, value). Anything else false.
inline bool ui_game_config_locked( const ui_config_entity_t& e, const value_t& root, const std::string& name, const std::string& value,
                                   const ui_profile_t& p )
{
    if ( e.m_force_online || member( root, "Online" ).tag != 0 )
        return false;
    const std::string mode = ui_json_text( member( member( root, "GameConfig" ), "Mode" ) );
    if ( name == "Track" )
    {
        if ( mode == "Race" || mode == "TimeTrial" )
            return !p.unlock( 100, mode, value, "" );
        if ( mode == "Slalom" )
            return !( p.unlock( 100, mode, value, "CourseA" ) || p.unlock( 100, mode, value, "CourseB" ) || p.unlock( 100, mode, value, "CourseC" ) );
        if ( mode == "RubberDucky" || mode == "Ghost" )
            return !p.unlock( 100, "Race", value, "" );
        return false;
    }
    if ( name == "Course" )
        return mode == "Slalom" && !p.unlock( 100, mode, ui_json_text( member( member( root, "GameConfig" ), "Track" ) ), value );
    if ( name == "Championship" )
        return !p.unlock( 92, name, value, "" );
    return false;
}

// VuGameConfigEntity slot 19 (+76) 0x45D4C0 (asked only when the license hook
// answers true): "Track": Race / TimeTrial -> !unlock+104(mode, value, ""),
// other modes -> mode == "Ghost"; "Course": Slalom only, !unlock+104(mode,
// Track's text, value); "Championship": !unlock+96(name, value); else false.
inline bool ui_game_config_locked_trial( const value_t& root, const std::string& name, const std::string& value, const ui_profile_t& p )
{
    const std::string mode = ui_json_text( member( member( root, "GameConfig" ), "Mode" ) );
    if ( name == "Track" )
    {
        if ( mode != "Race" && mode != "TimeTrial" )
            return mode == "Ghost";
        return !p.unlock( 104, mode, value, "" );
    }
    if ( name == "Course" )
        return mode == "Slalom" && !p.unlock( 104, mode, ui_json_text( member( member( root, "GameConfig" ), "Track" ) ), value );
    if ( name == "Championship" )
        return !p.unlock( 96, name, value, "" );
    return false;
}

// VuGameConfigEntity slot 17 (+68) 0x45D720 (stdcall name, value): whether a
// choice is already done (the progress manager's answers; no progress manager
// 0x758FD8 -> true). Mode = GameConfig.Mode's text. "Track": Race / TimeTrial
// -> +48(mode, value, "None"); Slalom -> +48(mode, value, CourseA) || CourseB
// || CourseC (in order, stopping at the first true); Ghost -> +124(value);
// other modes true. "Championship" -> +88(value). "Course" with mode Slalom
// -> +48(mode, GameConfig.Track's text (0x4FC4B0), value). Anything else true.
inline bool ui_game_config_done( const value_t& root, const std::string& name, const std::string& value, const ui_profile_t& p )
{
    if ( !p.has_progress )
        return true;
    const std::string mode = ui_json_text( member( member( root, "GameConfig" ), "Mode" ) );
    if ( name == "Track" )
    {
        if ( mode == "Race" || mode == "TimeTrial" )
            return p.race_done( mode, value, "None" );
        if ( mode == "Slalom" )
            return p.race_done( mode, value, "CourseA" ) || p.race_done( mode, value, "CourseB" ) || p.race_done( mode, value, "CourseC" );
        if ( mode == "Ghost" )
            return p.ghost_done( value );
        return true;
    }
    if ( name == "Championship" )
        return p.champ_done( value );
    if ( name == "Course" && mode == "Slalom" )
        return p.race_done( mode, ui_json_text( member( member( root, "GameConfig" ), "Track" ) ), value );
    return true;
}

// VuGameConfigEntity slot 26 (+104) 0x45E300 (thiscall this, const
// VuJsonContainer& option, VuString* out): the first choice (the array's size
// re-read every step) that is neither done (slot 17) nor locked (slot 18) ->
// out = its Value text, true. Otherwise, only with GameConfig.Mode exactly
// "Slalom" and the option's Name exactly "Track": the first choice with some
// course (CourseA, B, C in order) that the progress manager +48 ("Slalom",
// value, course) has NOT done and the unlock manager +100 ("Slalom", value,
// course) unlocks -> out = its Value, true. Else false (out untouched).
inline bool ui_game_config_next( const ui_config_entity_t& e, const value_t& root, const value_t& option, const ui_profile_t& p, std::string& out )
{
    const std::string& name = ui_json_text( member( option, "Name" ) );
    const value_t& choices = member( option, "Choices" );
    for ( int i = 0; i < ui_json_size( choices ); ++i )
    {
        const std::string& value = ui_json_text( member( ui_json_element( choices, i ), "Value" ) );
        if ( !ui_game_config_done( root, name, value, p ) && !ui_game_config_locked( e, root, name, value, p ) )
        {
            out = value;
            return true;
        }
    }
    if ( ui_json_text( member( member( root, "GameConfig" ), "Mode" ) ) != "Slalom" || name != "Track" )
        return false;
    static const char* const courses[ ] = { "CourseA", "CourseB", "CourseC" };
    for ( int i = 0; i < ui_json_size( choices ); ++i )
    {
        const std::string& value = ui_json_text( member( ui_json_element( choices, i ), "Value" ) );
        for ( const char* course : courses )
            if ( !p.race_done( "Slalom", value, course ) && p.unlock( 100, "Slalom", value, course ) )
            {
                out = value;
                return true;
            }
    }
    return false;
}

// 0x44BD50 (thiscall this, option): the index of the first choice whose
// "Value" equals root ["GameConfig"] [Name] (lookups only), else slot 22
// (+88) - VuGameConfigEntity's 0x4471B0 returns 0.
inline int ui_config_choice_index( const value_t& option, const value_t& root )
{
    const std::string& name = ui_json_text( member( option, "Name" ) );
    const value_t& current = ui_json_find( member( root, "GameConfig" ), name );
    const value_t& choices = member( option, "Choices" );
    for ( int i = 0; i < ui_json_size( choices ); ++i )
        if ( ui_json_equal( member( ui_json_element( choices, i ), "Value" ), current ) )
            return i;
    return 0;
}

// 0x44BD50 with the class's slot 22: the fallback gets the current value's
// TEXT (the boat's 0x44D970 reads only that, before it writes into the root -
// the reference itself would not survive the write here).
inline int ui_config_choice_index( const value_t& option, const value_t& root, const ui_config_class_t& cls )
{
    const std::string& name = ui_json_text( member( option, "Name" ) );
    const value_t& current = ui_json_find( member( root, "GameConfig" ), name );
    const value_t& choices = member( option, "Choices" );
    for ( int i = 0; i < ui_json_size( choices ); ++i )
        if ( ui_json_equal( member( ui_json_element( choices, i ), "Value" ), current ) )
            return i;
    const std::string text = ui_json_text( current );
    return cls.fallback( option, text );
}

// 0x44BE90: the option's current choice (0x44BD50), element-or-null.
inline const value_t& ui_config_current_choice( const value_t& option, const value_t& root, const ui_config_class_t& cls )
{
    const int index = ui_config_choice_index( option, root, cls );
    return ui_json_element( member( option, "Choices" ), index );
}

// 0x44B0D0 (thiscall this, const VuJsonContainer& options), for
// VuGameConfigEntity's virtuals. For each element of `options` (an array):
//   the option is pushed onto the entity's option vector (+0x2BC);
//   with the refresh flag and gameutil +0x60 clear: if Single Player and
//     gameutil +0x64 == "push", next_choice (+104) -> remember (+100) its
//     answer; then GameConfig[Name] (0x4FC090, 0x4FC6B0, 0x4FCAB0) = the
//     remembered selection (+96);
//   the current choice (0x44BD50) and a copy of its Value's text;
//   without the refresh flag and with gameutil +0x60 clear: unless locked
//     (+72), and (license hook answering true) unless locked (+76) - the
//     license is asked every time - remember (+100) the Value;
//   root made a map, GameConfig (0x4FC970) made a map, [Name] = the Value's
//     text up to its first NUL;
//   the walk again over that choice's "Options" (same entity, same vector).
inline void ui_config_walk( const ui_config_entity_t& e, ui_config_env_t& env, const value_t& options, std::vector< const value_t* >& pushed,
                            const ui_config_class_t& cls, const ui_profile_t& p )
{
    value_t& root = *env.m_root;
    for ( int i = 0; i < ui_json_size( options ); ++i )
    {
        const value_t& option = ui_json_element( options, i );
        pushed.push_back( &option );
        const std::string& name = ui_json_text( member( option, "Name" ) );
        if ( e.m_refresh && !env.m_busy )
        {
            if ( e.m_single_player && env.m_op == "push" )
            {
                std::string out;
                if ( cls.next_choice( option, out ) )
                    cls.remember( name, out );
            }
            const std::string last = cls.last( name );
            ui_json_set_string( ui_json_child_s( ui_json_child( root, "GameConfig" ), name ), last.c_str( ) );
        }
        const int index = ui_config_choice_index( option, root, cls );
        const value_t& choice = ui_json_element( member( option, "Choices" ), index );
        const std::string value = ui_json_text( member( choice, "Value" ) );
        if ( !e.m_refresh && !env.m_busy )
        {
            const bool open = !cls.locked( name, value );
            const bool trial = p.license( ) ? !cls.locked_trial( name, value ) : true;
            if ( open && trial )
                cls.remember( name, value );
        }
        ui_json_set_string( ui_json_child_s( ui_json_child( root, "GameConfig" ), name ), value.c_str( ) );
        ui_config_walk( e, env, member( choice, "Options" ), pushed, cls, p );
    }
}

// VuGameConfigEntity's virtuals for the walk (slot 22 0x4471B0 = 0; slots 17
// / 20 0x4471A0 / 23 / 12 are not reached by it).
inline ui_config_class_t ui_game_config_class( const ui_config_entity_t& e, value_t& root, const ui_profile_t& p )
{
    ui_config_class_t c;
    c.locked = [ &e, &root, &p ]( const std::string& name, const std::string& value ) { return ui_game_config_locked( e, root, name, value, p ); };
    c.locked_trial = [ &root, &p ]( const std::string& name, const std::string& value ) { return ui_game_config_locked_trial( root, name, value, p ); };
    c.fallback = []( const value_t&, const std::string& ) { return 0; };
    c.last = [ &root ]( const std::string& name ) { return ui_game_config_last( root, name ); };
    c.remember = [ &root ]( const std::string& name, const std::string& value ) { ui_game_config_remember( root, name, value ); };
    // Slot 17 0x45D720 (reached by 0x4494C0 - the NotUsedSP plug - and 0x45E300).
    c.blocked = [ &root, &p ]( const std::string& name, const std::string& value ) { return ui_game_config_done( root, name, value, p ); };
    // Slot 26: 0x45E300 recreated above, unless the profile answers it itself
    // (the checks whose retail runs hooked 0x45E300 set p.next_choice).
    if ( p.next_choice )
        c.next_choice = p.next_choice;
    else
        c.next_choice = [ &e, &root, &p ]( const value_t& option, std::string& out ) { return ui_game_config_next( e, root, option, p, out ); };
    return c;
}

// The walk for VuGameConfigEntity (verify_ui_config_options' entry).
inline void ui_config_apply_options( const ui_config_entity_t& e, ui_config_env_t& env, const value_t& options,
                                     std::vector< const value_t* >& pushed, const ui_profile_t& p )
{
    const ui_config_class_t cls = ui_game_config_class( e, *env.m_root, p );
    ui_config_walk( e, env, options, pushed, cls, p );
}

// 0x44B780 (thiscall this): for each option k of the vector, its combo box
// record (+0x2B0 + 52 x asInt(ComboBoxID) - unchecked in retail: false here
// when outside) gets +40 = (focus +0x2C8 == k), +44 = the current choice
// (0x44BD50), +48 = (float) that when not focused or refreshing; then for each
// choice j its entry's +24 = locked (slot 18) - 0 without the unlock manager
// (also unchecked in retail: false when the record holds fewer entries).
inline bool ui_config_combo_state( ui_config_entity_t& e, const value_t& root, const ui_config_class_t& cls, const ui_profile_t& p )
{
    for ( std::size_t k = 0; k < e.m_options.size( ); ++k )
    {
        const value_t& option = *e.m_options[ k ];
        const value_t& choices = member( option, "Choices" );
        const int index = ui_config_choice_index( option, root, cls );
        const bool focused = e.m_focus == static_cast< int >( k );
        const int id = ui_json_as_int( member( option, "ComboBoxID" ) );
        if ( id < 0 || static_cast< std::size_t >( id ) >= e.m_combos.size( ) )
            return false;
        ui_config_combo_t& combo = e.m_combos[ static_cast< std::size_t >( id ) ];
        combo.m_focused = focused ? 1 : 0;
        combo.m_index = index;
        if ( !focused || e.m_refresh )
            combo.m_shown = static_cast< float >( index );
        for ( int j = 0; j < ui_json_size( choices ); ++j )
        {
            bool locked = false;
            if ( p.has_unlock )
            {
                const std::string value = ui_json_text( member( ui_json_element( choices, j ), "Value" ) );
                locked = cls.locked( ui_json_text( member( option, "Name" ) ), value );
            }
            if ( static_cast< std::size_t >( j ) >= combo.m_locked.size( ) )
                return false;
            combo.m_locked[ static_cast< std::size_t >( j ) ] = locked ? 1 : 0;
        }
    }
    return true;
}

// The three option scans: for each option of the vector, the class test on
// (Name, the current choice's Value) - the first true answer returns false.
// 0x449050 slot 18 (true without the unlock manager), 0x4491C0 slot 19 (true
// unless the license hook answers true and the unlock manager is set),
// 0x449350 slot 20.
inline bool ui_config_scan( const ui_config_entity_t& e, const value_t& root, const ui_config_class_t& cls,
                            const std::function< bool( const std::string&, const std::string& ) >& test )
{
    for ( const value_t* option : e.m_options )
    {
        const value_t& choice = ui_config_current_choice( *option, root, cls );
        const std::string value = ui_json_text( member( choice, "Value" ) );
        if ( test( ui_json_text( member( *option, "Name" ) ), value ) )
            return false;
    }
    return true;
}

inline bool ui_config_all_unlocked( const ui_config_entity_t& e, const value_t& root, const ui_config_class_t& cls, const ui_profile_t& p )
{
    return !p.has_unlock || ui_config_scan( e, root, cls, cls.locked );
}

inline bool ui_config_all_licensed( const ui_config_entity_t& e, const value_t& root, const ui_config_class_t& cls, const ui_profile_t& p )
{
    if ( !p.license( ) || !p.has_unlock )
        return true;
    return ui_config_scan( e, root, cls, cls.locked_trial );
}

inline bool ui_config_all_available( const ui_config_entity_t& e, const value_t& root, const ui_config_class_t& cls )
{
    return ui_config_scan( e, root, cls, cls.unavailable );
}

// 0x4494C0 (thiscall this; the base's, disassembled): true without the unlock
// manager g 0x758FE8; else for each option of the vector slot 17 (+68) on
// (Name, the current choice's Value) - the first FALSE answer returns false;
// true when every one answers true (or the vector is empty). The inverse
// sense of the three scans above.
inline bool ui_config_all_done( const ui_config_entity_t& e, const value_t& root, const ui_config_class_t& cls, const ui_profile_t& p )
{
    if ( !p.has_unlock )
        return true;
    for ( const value_t* option : e.m_options )
    {
        const value_t& choice = ui_config_current_choice( *option, root, cls );
        const std::string value = ui_json_text( member( choice, "Value" ) );
        if ( !cls.blocked( ui_json_text( member( *option, "Name" ) ), value ) )
            return false;
    }
    return true;
}

// VuBaseConfigEntity's four input plugs (base ctor 0x4471C0: plug class 0x44C2E0 =
// a VuRetVal plug over a member, vtable 0x737138 / delegate 0x73C490; each
// writes VuRetVal { 3, al } - the byte +4 only):
//   "Unlocked"        0x4483D0 = 0x449040 (ui_config_all_unlocked)
//   "Available"       0x4483F0 = 0x4491C0 (ui_config_all_licensed)
//   "InstalledOnHost" 0x448410 = 0x449350 (ui_config_all_available)
//   "NotUsedSP"       0x448430 = 0x449040 && !0x4494C0 (0x4494C0 asked only
//                     when 0x449040 answered true)
// -1 for any other name.
inline int ui_config_plug( const char* plug, const ui_config_entity_t& e, const value_t& root, const ui_config_class_t& cls, const ui_profile_t& p )
{
    if ( std::strcmp( plug, "Unlocked" ) == 0 )
        return ui_config_all_unlocked( e, root, cls, p ) ? 1 : 0;
    if ( std::strcmp( plug, "Available" ) == 0 )
        return ui_config_all_licensed( e, root, cls, p ) ? 1 : 0;
    if ( std::strcmp( plug, "InstalledOnHost" ) == 0 )
        return ui_config_all_available( e, root, cls ) ? 1 : 0;
    if ( std::strcmp( plug, "NotUsedSP" ) == 0 )
        return ui_config_all_unlocked( e, root, cls, p ) && !ui_config_all_done( e, root, cls, p ) ? 1 : 0;
    return -1;
}

// 0x44B9C0 (thiscall this): the info string. Not all unlocked: "Common_
// StoreBetaLocked" when the build is the store beta, else the class's slot
// 23 text; an empty text posts nothing, else "OnSetShortUnlockStringID" (text
// + "_Short") and then "OnSetInfoStringID" (text). Not all licensed: "Common_
// PurchaseInfo"; not all available: "Common_HostDoesNotHaveDLC". Otherwise
// the focused option's (focus < count, signed: a negative focus reads before
// the vector in retail - false here) current choice's InfoStringID, else the
// option's own; still empty, the LAST non-empty current-choice InfoStringID
// over every option. A non-empty text posts "OnSetInfoStringID" with it.
inline bool ui_config_info( const ui_config_entity_t& e, const value_t& root, const ui_config_class_t& cls, const ui_profile_t& p,
                            const ui_config_hooks_t& h )
{
    std::string text;
    if ( !ui_config_all_unlocked( e, root, cls, p ) )
    {
        if ( p.store_beta( ) )
            text = "Common_StoreBetaLocked";
        else
            cls.info( text );
        if ( text.empty( ) )
            return true;
        ui_params_t shorter;
        shorter.add_string( ( text + "_Short" ).c_str( ) );
        ui_broadcast( h, "OnSetShortUnlockStringID", shorter );
    }
    else if ( !ui_config_all_licensed( e, root, cls, p ) )
        text = "Common_PurchaseInfo";
    else if ( !ui_config_all_available( e, root, cls ) )
        text = "Common_HostDoesNotHaveDLC";
    else
    {
        const int count = static_cast< int >( e.m_options.size( ) );
        if ( e.m_focus < count )
        {
            if ( e.m_focus < 0 )
                return false;
            const value_t& option = *e.m_options[ static_cast< std::size_t >( e.m_focus ) ];
            text = ui_json_text( member( ui_config_current_choice( option, root, cls ), "InfoStringID" ) );
            if ( text.empty( ) )
                text = ui_json_text( member( option, "InfoStringID" ) );
        }
        if ( text.empty( ) )
            for ( const value_t* option : e.m_options )
            {
                const std::string& t = ui_json_text( member( ui_config_current_choice( *option, root, cls ), "InfoStringID" ) );
                if ( !t.empty( ) )
                    text = t;
            }
    }
    if ( text.empty( ) )
        return true;
    ui_params_t p2;
    p2.add_string( text.c_str( ) );
    ui_broadcast( h, "OnSetInfoStringID", p2 );
    return true;
}

// The refresh 0x448140 (VuBaseConfigEntity slot 6; VuBoatConfigEntity's
// onGameInitialize calls it directly): the touch register (HOOK), +0x2CC = 1,
// the option vector emptied, the walk over the options document (+0x2A0), the
// combo state 0x44B780, slot 12 (+48), the info string 0x44B9C0, +0x2CC = 0.
inline bool ui_config_refresh( ui_config_entity_t& e, ui_config_env_t& env, const value_t& options, const ui_config_class_t& cls,
                               const ui_profile_t& p, const ui_config_hooks_t& h )
{
    h.touch( );
    e.m_refresh = true;
    e.m_options.clear( );
    ui_config_walk( e, env, options, e.m_options, cls, p );
    bool ok = ui_config_combo_state( e, *env.m_root, cls, p );
    ok = ok && cls.changed( );
    ok = ok && ui_config_info( e, *env.m_root, cls, p, h );
    e.m_refresh = false;
    return ok && !e.m_fault;
}

// ---------------------------------------------------------------------------
// VuBoatConfigEntity (ctor 0x44C9F0 over VuBaseConfigEntity, vtable 0x73C69C):
// the boat selector edits GameConfig.Boat / .Skin through the option walk and
// hands them to BoatConfig.PlayerBoats[player].Properties. Its slot 6
// (onGameInitialize) 0x44CD10: player (+0x2E4) = BoatConfig.CurPlayerIndex
// (1 the int, 2 cvttss2si, 7 the low dword, else 0); 0x44E010 on
// PlayerBoats[player].Properties (lookups); the refresh 0x448140 (NOT
// recreated for this class: its walk runs the boat virtuals 0x44D150 /
// 0x44D2C0 / 0x44D970 / 0x44EA00 / 0x44EAD0 / 0x44EB20, unread); 0x44E090;
// PlayerBoats[player].Properties = that; the OnChangeDockedBoat /
// OnPlayerBoatConfig broadcasts and the stat plugs 0x44E510 (not recreated).
// Recreated here: 0x44E010, 0x44E090 and slot 7 (onGameRelease) 0x44D090.

// 0x44E010 (stdcall const VuJsonContainer& props): root ["GameConfig"]
// ["Boat"] = a copy of props ["Boat"] (0x4FB9B0: any type), then ["Skin"] the
// same. (Both read before the writes here: props lives in root's BoatConfig,
// which the GameConfig writes never touch.)
inline void ui_boat_config_take( value_t& root, const value_t& props )
{
    const value_t boat = member( props, "Boat" );
    const value_t skin = member( props, "Skin" );
    ui_json_child( ui_json_child( root, "GameConfig" ), "Boat" ) = boat;
    ui_json_child( ui_json_child( root, "GameConfig" ), "Skin" ) = skin;
}

// 0x44E090 (thiscall this, VuJsonContainer* out): out ["Boat"] = a copy of
// root GameConfig.Boat (lookups); with at most one option in the entity's
// option vector (+0x2BC .. +0x2C0) out ["Skin"] = "", else a copy of
// GameConfig.Skin.
inline void ui_boat_config_give( value_t& out, const value_t& root, std::size_t options )
{
    ui_json_child( out, "Boat" ) = member( member( root, "GameConfig" ), "Boat" );
    if ( options <= 1 )
        ui_json_set_string( ui_json_child( out, "Skin" ), "" );
    else
        ui_json_child( out, "Skin" ) = member( member( root, "GameConfig" ), "Skin" );
}

// Slot 7 0x44D090 (onGameRelease): a fresh container from 0x44E090, then root
// ["BoatConfig"] ["PlayerBoats"] (0x4FBC60: made an array, grown to player + 1
// when player >= size - a SIGNED compare) [player] ["Properties"] = a copy of
// it; then 0x53FA60(this + 92), the touch callback's removal (HOOK: platform
// input). A negative player indexes before the array in retail (its write
// lands outside it): false, nothing written.
inline bool ui_boat_config_release( value_t& root, int player, std::size_t options )
{
    if ( player < 0 )
        return false;
    value_t props;
    ui_boat_config_give( props, root, options );
    value_t& boats = ui_json_child( ui_json_child( root, "BoatConfig" ), "PlayerBoats" );
    ui_json_child( ui_json_at( boats, static_cast< std::size_t >( player ) ), "Properties" ) = props;
    return true;
}

// Slot 7 0x44D090 whole (disassembled, batch 26): the write above, then the
// touch removal (h.touch_remove) - after the write, and only when it ran here
// (a negative player is retail's out-of-array write: false, nothing after).
inline bool ui_boat_config_game_release( value_t& root, int player, std::size_t options, const ui_config_hooks_t& h )
{
    if ( !ui_boat_config_release( root, player, options ) )
        return false;
    if ( h.touch_remove )
        h.touch_remove( );
    return true;
}
