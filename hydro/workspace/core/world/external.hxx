#pragma once

// What the front end's screens reach outside the UI (world batch 40). Nothing here is a retail function but the handlers it names: it binds the
// recreated pieces to the managers c_world holds.
//   menu_profile    ui_profile_t on the saved state: the unlock manager's slots 23 .. 26, the progress manager's 12, 22, 31, 33.
//   logic_services  logic_services_t, the managers game/scripted.hxx's handlers reach.
//   screen_external the inputs of a screen's entities that no UI binding owns: VuSetFrontEndCameraEntity, VuAudioEventEntity, the logic table.
// FMOD: m_sound's calls (nothing behind them until an audio device exists).

#include <workspace/core/game/docked.hxx>
#include <workspace/core/world/profile.hxx>

inline ui_profile_t c_world::menu_profile( ) { return ui_profile_saved( m_unlock, m_cloud.data ); }

// One event by name to whatever is live: the front end's screen, else the race's graph.
inline void c_world::logic_broadcast_event( const char* event, const ui_params_t& p )
{
    if ( m_logic_binding )
        m_logic_binding->post( fsm_hash( event ), p );
    else if ( m_menu )
    {
        const auto at = m_menu_projects.find( m_menu->m_project );
        if ( at != m_menu_projects.end( ) )
            at->second.binding->post( fsm_hash( event ), p );
        else
            m_menu->handle( fsm_hash( event ), p );
    }
    else
        race_broadcast( event, &p );
}

inline const logic_services_t& c_world::logic_services( )
{
    logic_services_t& s = m_logic_services;
    if ( !s.broadcast )
    {
        s.broadcast = [ this ]( const char* event, const ui_params_t& p ) { logic_broadcast_event( event, p ); };
        s.champ_data = [ this ] { return ui_progress_champ_active( m_cloud.data ); };
        s.champ_clear = [ this ] { ui_progress_champ_save( m_cloud.data, value_t{ } ); };
        s.defaults = [ this ]( const char* which ) {
            const value_t& metro = member( member( member( m_constant_db, "VuDBAsset" ), "DefaultSettings" ), "Metro" );
            settings_t& t = m_settings;
            if ( std::strcmp( which, "audio" ) == 0 )
                settings_defaults_audio( t, metro, m_settings_hooks );
            else if ( std::strcmp( which, "pad" ) == 0 )   // 0x4B4E20
            {
                t.auto_throttle_pad = settings_bool( member( metro, "AutoThrottleGamePad" ) );
                t.ui.m_vibration = settings_bool( member( metro, "Vibration" ) );
            }
            else if ( std::strcmp( which, "keys" ) == 0 )  // 0x4B4EA0
                t.auto_throttle_keys = settings_bool( member( metro, "AutoThrottleKeyboard" ) );
            else if ( std::strcmp( which, "tilt" ) == 0 )  // 0x4B4F00
            {
                t.ui.m_tilt = settings_bool( member( metro, "TiltSteering" ) );
                t.ui.m_steering = 0.0f;
                settings_float( member( metro, "SteeringSensitivity" ), t.ui.m_steering );
            }
            else
                settings_defaults_display( t, metro, m_settings_hooks, m_settings_platform );
        };
        s.input_defaults = [ this ]( int method ) {
            platform_input_t* in = m_race.input ? m_race.input.get( ) : m_menu_input.get( );
            if ( in )
                in->manager.load_default_mapping( 0, method );
        };
        s.save_profile = [ this ] {
            if ( m_saves_on )
                m_profile.save( [ this ] { saves_profile_listeners( ); } );
        };
        s.cloud_data = &m_cloud.data;
        s.profile_saved = &m_profile.data;
        s.profile_temp = &m_profile_temp;
    }
    s.game_data = m_race.active ? &m_race.util : &m_menu_util;
    if ( !m_logic_game.sku )   // VuIsSkuEntity's answer: the pack's
    {
        m_logic_sku = m_archive.pack_sku( );
        m_logic_game.sku = &m_logic_sku;
    }
    return s;
}

// A screen's logic entity on its first input: logic_construct + the properties of game/scripted.hxx's and world/race.hxx's list that screens author, from
// the UI's loaded slots (a screen's tree is the UI's, so its state is not a logic_state_t). Null: not a logic class.
inline logic_entity_t* c_world::screen_logic( screen_state_t& state, entity_t& e )
{
    const std::uint32_t vt = logic_vtable_of( e.m_type.c_str( ) );
    if ( vt == 0u )
        return nullptr;
    const auto at = state.logic.find( &e );
    if ( at != state.logic.end( ) )
        return &at->second;
    logic_entity_t l = logic_construct( vt );
    const auto text = [ &e ]( const char* name, std::string& out ) {
        if ( const ui_slot_t* s = ui_bind_slot( e, name ) )
            out = s->m_text;
    };
    const auto word = [ &e ]( const char* name ) {
        const ui_slot_t* s = ui_bind_slot( e, name );
        return s ? s->m_bits[ 0 ] : 0u;
    };
    const std::string& c = e.m_type;
    if ( c == "VuPersistentBooleanEntity" || c == "VuPersistentIntegerEntity" || c == "VuPersistentStringEntity" )
    {
        l.head[ 0 ] = static_cast< std::uint8_t >( word( "Save" ) );
        text( "Name", l.text );
    }
    else if ( c == "VuIsSkuEntity" )
        text( "SKU", l.text );
    else if ( c == "VuIsAchievementUnlockedEntity" )
        text( "Achievement", l.text );
    else if ( c == "VuSetDefaultSettingsEntity" )
    {
        l.head[ 0 ] = static_cast< std::uint8_t >( word( "Audio" ) );
        l.head[ 1 ] = static_cast< std::uint8_t >( word( "GamePadControls" ) );
        l.head[ 2 ] = static_cast< std::uint8_t >( word( "KeyboardControls" ) );
        l.head[ 3 ] = static_cast< std::uint8_t >( word( "TouchTiltControls" ) );
        l.words[ 0 ] = word( "Graphics" ) & 0xFFu;
    }
    else if ( c == "VuIntegerEntity" || c == "VuSetIntegerEntity" )
        l.words[ 0 ] = word( "Value" );
    else if ( c == "VuSetBooleanEntity" )
        l.words[ 0 ] = word( "Value" ) & 0xFFu;
    else if ( c == "VuSetStringEntity" || c == "VuStringEntity" )
        text( "Value", l.text );
    return &state.logic.emplace( &e, std::move( l ) ).first->second;
}

// logic_dispatch's hooks on a screen: an output through the screen's binding, the one VuRandom handed in and back.
struct screen_logic_context_t
{
    c_ui_binding* binding;
    entity_t* entity;
    pfx_rand_t rand;
};

inline logic_retval_t screen_logic_fire( void* user, logic_entity_t&, const char* output, const ui_params_t& params )
{
    screen_logic_context_t& c = *static_cast< screen_logic_context_t* >( user );
    const ui_ret_t r = c.binding->fire_plug( *c.entity, output, params );
    logic_retval_t answer;
    answer.type = r.m_words[ 0 ];
    answer.value = r.m_words[ 1 ];
    return answer;
}

inline ui_ret_t c_world::screen_external( screen_state_t& state, c_ui_binding& binding, entity_t& e, const char* plug, const ui_params_t& p )
{
    ui_ret_t out;
    const std::string& cls = e.m_type;
    // 0x408F90: the four broadcasts' one subscriber is the rig's event map.
    if ( cls == "VuSetFrontEndCameraEntity" && std::strcmp( plug, "Trigger" ) == 0 )
    {
        front_end_trigger_t t;
        const auto bits = [ &e ]( const char* name, void* to, std::size_t bytes ) {
            if ( const ui_slot_t* s = ui_bind_slot( e, name ) )
                std::memcpy( to, s->m_bits, bytes );
        };
        bits( "Interpolation Time", &t.interpolation, 4 );
        bits( "Modify Eye Position", &t.modify[ 0 ], 1 );
        bits( "Modify Target Position", &t.modify[ 1 ], 1 );
        bits( "Modify Far Plane", &t.modify[ 2 ], 1 );
        bits( "Eye Position", t.eye, 16 );
        bits( "Target Position", t.target, 16 );
        bits( "Far Plane", &t.far_plane, 4 );
        front_end_trigger( t, [ this ]( std::uint32_t hash, const std::uint8_t* params ) { background_event( m_background, hash, params ); } );
        return out;
    }
    // VuAudioEventEntity (vtable 0x756F30): "Start" 0x5F42D0 as the emitter's without the 3D attributes, "Stop" 0x5F4310 the emitter's. +0x78 "Stop When Destroyed".
    if ( cls == "VuAudioEventEntity" )
    {
        sound_event_t& holder = state.sounds[ &e ];
        if ( const ui_slot_t* s = ui_bind_slot( e, "Stop When Destroyed" ) )
            holder.image[ 0 ] = static_cast< std::uint8_t >( s->m_bits[ 0 ] );
        audio_emitter_t emitter;
        emitter.holder = holder;
        if ( std::strcmp( plug, "Start" ) == 0 )
        {
            const ui_slot_t* name = ui_bind_slot( e, "Event Name" );
            if ( sound_event_create( emitter.holder, name ? name->m_text : std::string( ), m_sound ) && m_sound.start )
                m_sound.start( m_sound.user, emitter.holder.event( ) );
        }
        else if ( std::strcmp( plug, "Stop" ) == 0 )
            audio_emitter_stop( emitter, m_sound );
        holder = emitter.holder;
        return out;
    }
    // VuBoatConfigEntity "PlayAnnouncerAudio" 0x44E140: BoatDB [ GameConfig.Boat ] .Audio.Announcer, getEvent mode 1 into a local, start.
    if ( cls == "VuBoatConfigEntity" && std::strcmp( plug, "PlayAnnouncerAudio" ) == 0 )
    {
        const std::string& boat = ui_json_text( member( member( m_menu_util, "GameConfig" ), "Boat" ) );
        ui_play( ui_json_text( member( member( ui_db_entry( &m_boat_db, boat ), "Audio" ), "Announcer" ) ).c_str( ) );
        return out;
    }
    if ( boxes_entity( state, binding, e, plug ) )
        return out;
    if ( logic_entity_t* l = screen_logic( state, e ) )
    {
        screen_logic_context_t ctx{ &binding, &e, { } };
        std::memcpy( &ctx.rand, &m_vurandom, sizeof( ctx.rand ) );
        logic_hooks_t hooks;
        hooks.user = &ctx;
        hooks.rand = &ctx.rand;
        hooks.services = &logic_services( );
        if ( m_menu )   // gameutil +0x50 in the front end (load_scene points it at the race's)
            m_logic_game.game_data = &m_menu_util;
        m_logic_game.unrecreated = &m_logic_unrecreated;
        hooks.game = &m_logic_game;
        hooks.fire = screen_logic_fire;
        c_ui_binding* const held = m_logic_binding;
        m_logic_binding = &binding;
        logic_retval_t answer;
        const bool ran = logic_dispatch( *l, plug, answer, p, hooks );
        m_logic_binding = held;
        if ( ran )
        {
            std::memcpy( &m_vurandom, &ctx.rand, sizeof( ctx.rand ) );
            out.m_words[ 0 ] = answer.type;
            out.m_words[ 1 ] = answer.value;
            return out;
        }
    }
    LOG_VERBOSE( "screen: the external input %s . %s - not recreated", cls.c_str( ), plug );
    return out;
}

// VuAwardFlowEntity's "OnAwardScreenBusy" listener (0x42F050, registered by its ctor 0x42EDB0) on the award project.
inline void c_world::award_busy( const ui_params_t& p )
{
    ui_game_award_t* award = m_race.screens.m_award.get( );
    if ( !award || !award->m_root || !award->m_binding )
        return;
    const auto walk = [ & ]( const auto& self, entity_t& e ) -> void {
        if ( e.m_type == "VuAwardFlowEntity" )
            if ( logic_entity_t* l = screen_logic( m_game_screen_state, e ) )
            {
                screen_logic_context_t ctx{ award->m_binding.get( ), &e, { } };
                logic_hooks_t hooks;
                hooks.user = &ctx;
                hooks.fire = screen_logic_fire;
                logic_award_busy( *l, p, hooks );
            }
        for ( auto& child : e.m_children )
            self( self, *child );
    };
    walk( walk, *award->m_root );
}


// VuPosterEntity's two listeners (ctor 0x43A8F0: 0x43AC50, 0x43ADA0) on gameutil +0x50; the reload 0x43AF50 and the draw 0x43AEA0: world/dock.hxx.
inline bool c_world::menu_poster( std::uint32_t hash, const ui_params_t& p )
{
    static const std::uint32_t change = fsm_hash( "OnChangePoster" ), reset = fsm_hash( "OnResetPoster" );
    if ( hash != change && hash != reset )
        return false;
    poster_hooks_t h;
    h.game_data = &m_menu_util;
    h.has_asset = [ this ]( const std::string& name ) { return !m_archive.names( "VuStaticModelAsset/" + name ).empty( ); };   // 0x43B080: the factory's vt+0x28 ( the type, name )
    h.reload = [ this ] { poster_reload( ); };
    if ( hash == change )
        poster_change( p, h );
    else
        poster_reset( h );
    return true;
}
