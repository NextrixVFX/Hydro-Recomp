#pragma once

// The game tier's small script entities (boat batch 34), included by world/logic.hxx above its table: each input plug read from its handler,
// checked by case game_plugs (cases/gameplug.py: every plug fired through 0x404BE0 by retail's own code).
// Shared shape as world/logic.hxx: the VuRetVal's type word only (0), unless named. The outputs "with nothing" are a fresh empty VuParams.
//
// The services (logic_services_t) are the managers these reach: missing -> counted unrecreated, nothing happens. Platform: none here; the
// VuPersistent* store is the profile's document (save/profile.hxx), saved by the same file as settings.

#include <workspace/core/entity/json.hxx>
#include <workspace/core/game/unlock.hxx>

// 0x42D6E0 VuOncePerLapFilterEntity "In" (+0x60 = -1 at the ctor 0x42D530): a first record of type 11 (signed size > 0): the repository's entity
// (null: retail faults; counted). When its type chain holds VuBoatEntity 0x73628C and its +0x4AC (the lap, signed) passes +0x60: store it, "Out" with
// the params. The entity's vtable outside the boat tables: counted.
inline void logic_once_per_lap( logic_entity_t& e, logic_retval_t& out, const ui_params_t& in, const logic_hooks_t& h )
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
    const std::uint32_t vtable = entity ? h.game->word( h.game->user, entity ) : 0u;
    if ( !boat_rtti_of( vtable ) )
    {
        logic_unrecreated( h );
        return;
    }
    if ( !boat_rtti_is( vtable, 0x73628Cu ) )
        return;
    const std::int32_t lap = static_cast< std::int32_t >( h.game->word( h.game->user, entity + 0x4ACu ) );
    if ( lap > static_cast< std::int32_t >( e.words[ 0 ] ) )
    {
        e.words[ 0 ] = static_cast< std::uint32_t >( lap );
        logic_fire( e, h, "Out", in );
    }
}

// VuParams' adds 0x4051A0 (type 3, one byte) and 0x405120 (type 2, the float's four bytes).
inline void scripted_add_bool( ui_params_t& p, std::uint8_t v )
{
    if ( p.m_size + 5 > 0x80 )
        return;
    const std::uint32_t type = 3u;
    std::memcpy( p.m_bytes + p.m_size, &type, 4 );
    p.m_bytes[ p.m_size + 4 ] = v;
    p.m_size += 5;
}

inline void scripted_add_float( ui_params_t& p, std::uint32_t bits )
{
    if ( p.m_size + 8 > 0x80 )
        return;
    const std::uint32_t type = 2u;
    std::memcpy( p.m_bytes + p.m_size, &type, 4 );
    std::memcpy( p.m_bytes + p.m_size + 4, &bits, 4 );
    p.m_size += 8;
}

// 0x406690 VuSetTrackBranchProbabilities "Trigger": "Set" with ( +0x60 bool, +0x61 bool, +0x64 float, +0x68 float ).
inline void logic_track_branch( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    ui_params_t p;
    scripted_add_bool( p, static_cast< std::uint8_t >( e.words[ 0 ] & 0xFFu ) );
    scripted_add_bool( p, static_cast< std::uint8_t >( ( e.words[ 0 ] >> 8 ) & 0xFFu ) );
    scripted_add_float( p, e.words[ 1 ] );
    scripted_add_float( p, e.words[ 2 ] );
    logic_fire( e, h, "Set", p );
    out.type = 0u;
}

inline void logic_broadcast( const char* event, const ui_params_t& p, const logic_hooks_t& h )
{
    if ( h.services && h.services->broadcast )
        h.services->broadcast( event, p );
    else
        logic_unrecreated( h );
}

// The classes whose one plug broadcasts its name with an empty VuParams: 0x42EFF0 VuAwardFlowEntity "Next", 0x470B80 "OnEndActionGame",
// 0x470C70 "OnResumeActionGame", 0x470D60 "OnRestartActionGame", 0x471350 VuShutdownAppUIAction.
template < const char* const& event >
inline void logic_announce( logic_entity_t&, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    logic_broadcast( event, ui_params_t{ }, h );
    out.type = 0u;
}

inline const char* const g_event_award_next = "OnAwardFlowNext";
inline const char* const g_event_end_action = "OnEndActionGame";
inline const char* const g_event_resume_action = "OnResumeActionGame";
inline const char* const g_event_restart_action = "OnRestartActionGame";
inline const char* const g_event_shutdown = "OnShutdownApp";

// 0x42F050, VuAwardFlowEntity's listener (the event its ctor 0x42ED30 registers; the caller's): a first bool record's byte (no record: false):
// not zero "Busy", else "NotBusy".
inline void logic_award_busy( logic_entity_t& e, const ui_params_t& in, const logic_hooks_t& h )
{
    std::uint8_t busy = 0u;
    if ( in.m_size > 0 )
    {
        std::uint32_t type;
        std::memcpy( &type, in.m_bytes, 4 );
        if ( type == 3u )
            busy = in.m_bytes[ 4 ];
    }
    logic_fire_empty( e, h, busy ? "Busy" : "NotBusy" );
}

// 0x470A80 VuStartActionGameUIAction "Trigger": the Type (the VuString +0x5C) as a string record when strlen + 5 <= 0x80 (else nothing),
// "OnStartActionGame".
inline void logic_start_action( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    ui_params_t p;
    p.add_string( e.text.c_str( ) );
    logic_broadcast( "OnStartActionGame", p, h );
    out.type = 0u;
}

// 0x4710D0 VuDoesChampDataExistUIAction: the progress manager's vt+0x74 true -> "No", false -> "Yes". 0x4723F0 VuDoesChampionshipDataExistUIAction: the
// other way round.
inline void logic_champ_answer( logic_entity_t& e, logic_retval_t& out, const logic_hooks_t& h, const char* when_true, const char* when_false )
{
    out.type = 0u;
    if ( !h.services || !h.services->champ_data )
    {
        logic_unrecreated( h );
        return;
    }
    logic_fire_empty( e, h, h.services->champ_data( ) ? when_true : when_false );
}

inline void logic_champ_data( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h ) { logic_champ_answer( e, out, h, "No", "Yes" ); }

inline void logic_championship_data( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h ) { logic_champ_answer( e, out, h, "Yes", "No" ); }

// 0x4712A0 (both VuClearChampDataUIAction and VuClearChampionshipDataUIAction): the progress manager's vt+0x68 ( the null container ).
inline void logic_champ_clear( logic_entity_t&, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    out.type = 0u;
    if ( h.services && h.services->champ_clear )
        h.services->champ_clear( );
    else
        logic_unrecreated( h );
}

// 0x4721A0 VuClearGameResultsUIAction: gameutil +0x50 [ "GameResults" ] (made when missing, 0x4FC090) cleared.
inline void logic_clear_results( logic_entity_t&, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    out.type = 0u;
    if ( h.services && h.services->game_data )
        json_clear( json_member( *h.services->game_data, "GameResults" ) );
    else
        logic_unrecreated( h );
}

// 0x46C210 VuSetDefaultSettingsEntity "Trigger": +0x5C Audio 0x4B4C60; +0x5D GamePadControls 0x4B4E20 then input vt+0x40 ( 0, 0 ); +0x5E KeyboardControls
// 0x4B4EA0 then input vt+0x40 ( 0, 1 ); +0x5F TouchTiltControls 0x4B4F00; +0x60 Graphics 0x4B4FC0. Each a settings.hxx slot, through `defaults`.
inline void logic_default_settings( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    out.type = 0u;
    if ( !h.services || !h.services->defaults || !h.services->input_defaults )
    {
        logic_unrecreated( h );
        return;
    }
    const logic_services_t& s = *h.services;
    if ( e.head[ 0 ] )
        s.defaults( "audio" );
    if ( e.head[ 1 ] )
    {
        s.defaults( "pad" );
        s.input_defaults( 0 );
    }
    if ( e.head[ 2 ] )
    {
        s.defaults( "keys" );
        s.input_defaults( 1 );
    }
    if ( e.head[ 3 ] )
        s.defaults( "tilt" );
    if ( e.words[ 0 ] & 0xFFu )
        s.defaults( "graphics" );
}

// 0x46C470 VuSaveSettingsEntity "Trigger": 0x5547F0.
inline void logic_save_settings( logic_entity_t&, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    out.type = 0u;
    if ( h.services && h.services->save_profile )
        h.services->save_profile( );
    else
        logic_unrecreated( h );
}

// 0x42E570 VuIsAchievementUnlockedEntity "Trigger": the game save's Achievements[ name ][ "LocalUnlocked" ] a true bool -> "True", else "False", with the
// params. Local: read the same with online out.
inline void logic_achievement_unlocked( logic_entity_t& e, logic_retval_t& out, const ui_params_t& in, const logic_hooks_t& h )
{
    out.type = 0u;
    if ( !h.services || !h.services->cloud_data )
    {
        logic_unrecreated( h );
        return;
    }
    const value_t& v = member( member( member( *h.services->cloud_data, "Achievements" ), e.text.c_str( ) ), "LocalUnlocked" );
    logic_fire( e, h, v.tag == 3u && static_cast< std::uint8_t >( v.integer ) != 0u ? "True" : "False", in );
}

// VuPersistentBaseEntity: +0x5C Save picks the profile's +8 document, else +0x18; +0x60 Name. 0x5E3DF0: its PersistentData[ Name ], else the null 0x7593A0.
inline const value_t* persistent_read( const logic_entity_t& e, const logic_hooks_t& h )
{
    const value_t* doc = h.services ? ( e.head[ 0 ] ? h.services->profile_saved : h.services->profile_temp ) : nullptr;
    if ( doc == nullptr )
    {
        logic_unrecreated( h );
        return nullptr;
    }
    return &member( member( *doc, "PersistentData" ), e.text.c_str( ) );
}

// 0x5E3E60: PersistentData an object (0x4FC6B0 makes it one), its slot made. The caller tests Name (+0x70 not 0): an empty name writes nothing.
inline value_t* persistent_slot( const logic_entity_t& e, const logic_hooks_t& h )
{
    value_t* doc = h.services ? ( e.head[ 0 ] ? h.services->profile_saved : h.services->profile_temp ) : nullptr;
    if ( doc == nullptr )
    {
        logic_unrecreated( h );
        return nullptr;
    }
    value_t& data = json_member( *doc, "PersistentData" );
    if ( data.tag != 6u )
    {
        json_clear( data );
        data.tag = 6u;
        data.kind = value_t::kind_t::object;
    }
    return &json_member( data, e.text );
}

inline void persistent_store_bool( logic_entity_t& e, std::uint8_t v, const logic_hooks_t& h )
{
    if ( e.text.empty( ) )
        return;
    if ( value_t* slot = persistent_slot( e, h ) )
    {
        json_clear( *slot );
        slot->tag = 3u;
        slot->kind = value_t::kind_t::integer;
        slot->integer = v;
    }
}

// VuPersistentBooleanEntity 0x5E3EC0 "SetTrue", 0x5E3EF0 "SetFalse", 0x5E4300 "Set" (a first bool record's byte, none: 0), 0x5E3F20 "Get".
inline void logic_persist_true( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    persistent_store_bool( e, 1u, h );
    out.type = 0u;
}

inline void logic_persist_false( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    persistent_store_bool( e, 0u, h );
    out.type = 0u;
}

inline void logic_persist_set_bool( logic_entity_t& e, logic_retval_t& out, const ui_params_t& in, const logic_hooks_t& h )
{
    std::uint8_t v = 0u;
    if ( in.m_size > 0 )
    {
        std::uint32_t type;
        std::memcpy( &type, in.m_bytes, 4 );
        if ( type == 3u )
            v = in.m_bytes[ 4 ];
    }
    persistent_store_bool( e, v, h );
    out.type = 0u;
}

inline void logic_persist_get_bool( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    const value_t* v = persistent_read( e, h );
    logic_ret_bool( out, v != nullptr && v->tag == 3u && static_cast< std::uint8_t >( v->integer ) != 0u );
    if ( v != nullptr && v->tag == 3u )
        out.value = ( out.value & 0xFFFFFF00u ) | static_cast< std::uint8_t >( v->integer );
}

// VuPersistentIntegerEntity 0x5E45B0 "Set" (0x4237B0), 0x5E4640 "Get": int, float (truncated), int64's low word, else 0.
inline void logic_persist_set_int( logic_entity_t& e, logic_retval_t& out, const ui_params_t& in, const logic_hooks_t& h )
{
    if ( !e.text.empty( ) )
    {
        if ( value_t* slot = persistent_slot( e, h ) )
        {
            ui_params_reader_t r( in );
            json_set_int( *slot, ui_params_read_int( r ) );
        }
    }
    out.type = 0u;
}

inline void logic_persist_get_int( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    const value_t* v = persistent_read( e, h );
    out.type = 1u;
    out.value = v != nullptr ? static_cast< std::uint32_t >( unlock_int_or_zero( *v ) ) : 0u;
}

// VuPersistentStringEntity 0x5E48A0 "Set" (0x505260, 0x4FC640), 0x5E4920 "Get": the text, else "".
inline void logic_persist_set_string( logic_entity_t& e, logic_retval_t& out, const ui_params_t& in, const logic_hooks_t& h )
{
    if ( !e.text.empty( ) )
    {
        if ( value_t* slot = persistent_slot( e, h ) )
        {
            ui_params_reader_t r( in );
            json_set_string( *slot, r.read_string( ) );
        }
    }
    out.type = 0u;
}

inline void logic_persist_get_string( logic_entity_t& e, logic_retval_t& out, const ui_params_t&, const logic_hooks_t& h )
{
    const value_t* v = persistent_read( e, h );
    out.type = 4u;
    out.text = v != nullptr && v->tag == 4u ? v->text.c_str( ) : "";
}
