#pragma once

// c_world's front end - member definitions (world batch 37): VuFrontEndGameMode (ui/flow.hxx) driven from the loaded Screens/* projects,
// through the mode manager (world/mode.hxx). Nothing here is a retail function: it binds c_front_end's hooks to OUR objects:
//   m_load_project   0x4B4360 through the UI's world (ui_hud_load), a c_ui_binding per project
//   m_init_project   0x4C0550's init: 0x500B10 + "OnProjectInitialized" (project_initialize)
//   m_release_project  0x500B80 (project_release), the deletion
//   m_screen_tick    the root's slot 12 0x629B90 (binding.tick)
//   m_screen_draw    the root's slot 13 0x629D60 (ui_screen_frame, into the UI pass)
// The binding's broadcasts reach the mode's event map 0x5156E0 (c_front_end::handle): OnPushScreen, OnSetScreen, OnStartActionGame ...
// Hooks that answer nothing, named: the audio ones, the title voice-over, analytics, the save check, 0x530BB0, VuProject's reference counts.

class c_menu_mode final : public game_mode_t
{
public:
    explicit c_menu_mode( c_world& w ) : m_world( w ) { }
    bool enter( const std::string& ) override { m_world.menu_enter( ); return true; }
    void exit( ) override { m_world.menu_exit( ); }
    const std::string& tick( float dt ) override { return m_world.menu_tick( dt ); }
    void draw( ) override { m_world.m_menu->draw( ); }

private:
    c_world& m_world;
};

// The action mode stands behind c_world's own flow (scene_frame ticks it in the race's Decision phase): the manager holds it for slots 2,
// 3, and 5. slot 2's body is load_scene, which runs action_start.
class c_action_slot final : public game_mode_t
{
public:
    explicit c_action_slot( c_world& w ) : m_world( w ) { }
    bool enter( const std::string& ) override { m_world.action_enter( ); return true; }
    void exit( ) override { m_world.action_exit( ); }
    const std::string& tick( float ) override { static const std::string none; return none; }
    void draw( ) override
    {
        if ( m_world.m_action )
            m_world.m_action->draw_state( );
    }

private:
    c_world& m_world;
};

#include <workspace/core/world/title.hxx>

inline void c_world::modes_bind( )
{
    m_modes.m_factory = [ this ]( const std::string& name ) -> std::unique_ptr< game_mode_t > {
        if ( name == "FrontEnd" )
            return std::make_unique< c_menu_mode >( *this );
        if ( name == "Action" )
            return std::make_unique< c_action_slot >( *this );
        if ( name == "TitleSequence" )
            return std::make_unique< c_title_mode >( *this );
        LOG_WARNING( "game mode %s: its ctor in 0x4CC460 is NOT recreated - no mode (UNKNOWN)", name.c_str( ) );
        return nullptr;
    };
}

// Slot 3 0x4B9550's game half: the game deleted (VuGame's release, the bodies); gameutil +0x50 survives it, with GameResults.
inline void c_world::action_exit( )
{
    if ( m_menu_util.tag == 0 )
        m_menu_util = m_race.util;   // a race from the command line: no front end built the root
    else
        ui_json_child( m_menu_util, "GameResults" ) = member( m_race.util, "GameResults" );
    m_pause_menu.reset( );
    m_pause_binding.reset( );
    m_pause_root.reset( );
    if ( m_race.active )
        race_session_end( m_race );
    m_action.reset( );
}

inline void c_world::action_enter( )
{
    m_has_chosen = true;
    m_chosen = member( m_menu_util, "GameData" );
    load_scene( "", nullptr, nullptr );
    m_has_chosen = false;
}

inline float g_menu_dt = 0.0f;   // the tick manager's delta the screen tick sees (UI batch 32; c_world's member when world.hxx takes it)

inline ui_bind_hooks_t c_world::menu_hooks( )
{
    ui_bind_hooks_t hooks;
    hooks.m_root = &m_menu_util;
    hooks.m_broadcast_out = [ this ]( std::uint32_t hash, const ui_params_t& p ) {
        if ( m_menu )
            m_menu->handle( hash, p );
    };
    string_db_ready( );
    hooks.m_string_db = [ this ]( const char* key ) { return string_db_get( m_string_db, key ? key : "" ); };
    // The Screens' logic entities (world/logic.hxx, case logic_plugs): the race's dispatcher on a per-entity state built from the loaded properties
    // (a Screens project's m_state is the UI's). The services are the saves'; the settings' defaults stay the world's (null: counted, nothing runs).
    hooks.m_external = [ this ]( entity_t& e, const char* plug, const ui_params_t& params ) {
        const std::uint32_t vt = logic_vtable_of( e.m_type.c_str( ) );
        if ( vt == 0u )
        {
            LOG_VERBOSE( "front end: the external input %s . %s (a platform hook) - not run", e.m_type.c_str( ), plug );
            return ui_ret_t{ };
        }
        static std::unordered_map< const entity_t*, logic_entity_t > states;
        static value_t temp;   // the profile's +0x18: this run's
        static logic_services_t services;
        static std::string sku;
        sku = m_archive.pack_sku( );
        m_logic_game.sku = &sku;
        m_logic_game.game_data = &m_menu_util;
        m_logic_game.unrecreated = &m_logic_unrecreated;
        services.broadcast = [ this ]( const char* event, const ui_params_t& p ) {
            if ( m_menu )
                m_menu->handle( fsm_hash( event ), p );
        };
        services.champ_data = [ this ] { return ui_progress_champ_active( m_cloud.data ); };
        services.champ_clear = [ this ] { ui_progress_champ_save( m_cloud.data, value_t{ } ); };
        services.save_profile = [ this ] {
            if ( m_saves_on )
                m_profile.save( [ this ] { saves_profile_listeners( ); } );
        };
        services.game_data = &m_menu_util;
        services.cloud_data = &m_cloud.data;
        services.profile_saved = &m_profile.data;
        services.profile_temp = &temp;
        auto at = states.find( &e );
        if ( at == states.end( ) )
        {
            logic_entity_t s = logic_construct( vt );
            for ( const char* name : { "SKU", "Name", "Achievement" } )
                if ( const ui_slot_t* slot = ui_property_value( e, name ) )
                    s.text = slot->m_text;
            if ( const ui_slot_t* slot = ui_property_value( e, "Save" ) )
                s.head[ 0 ] = slot->m_bits[ 0 ] & 0xFFu;
            at = states.emplace( &e, std::move( s ) ).first;
        }
        c_ui_binding* binding = nullptr;
        for ( auto& p : m_menu_projects )
            if ( p.second.binding && p.second.binding->bound_index( e ) >= 0 )
                binding = p.second.binding.get( );
        struct context_t
        {
            c_ui_binding* binding;
            entity_t* entity;
        } ctx{ binding, &e };
        logic_hooks_t h;
        h.user = &ctx;
        h.game = &m_logic_game;
        h.services = &services;
        h.fire = []( void* user, logic_entity_t&, const char* out_plug, const ui_params_t& p ) -> logic_retval_t {
            context_t& c = *static_cast< context_t* >( user );
            logic_retval_t out;
            if ( c.binding )
            {
                const ui_ret_t r = c.binding->fire_plug( *c.entity, out_plug, p );
                out.type = r.m_words[ 0 ];
                out.value = r.m_words[ 1 ];
            }
            return out;
        };
        logic_retval_t out{ };
        ui_ret_t ret;
        if ( !logic_dispatch( at->second, plug, out, params, h ) )
        {
            LOG_VERBOSE( "front end: the input %s . %s is not in the logic table - not run", e.m_type.c_str( ), plug );
            return ret;
        }
        ret.m_words[ 0 ] = out.type;
        ret.m_words[ 1 ] = out.value;
        return ret;
    };
    hooks.m_input = [ this ]( int pad, const char* name ) { return m_menu_input && m_menu_input->manager.pressed( pad, name ); };
    // A PLATFORM ANSWER, not read from anything: the license manager's "trial" is no (the owner's copy; the retail shot's Main shows Windows 8 Store, the full licence's text).
    hooks.m_trial = [ ] { return false; };
#if HYDRO_UI_PASS && HYDRO_UI_TEXT
    // The ticker's: VuStringDB slot 7, 0x626B80 on its Font, 0x5373D0 on the font with the entity's FontDB params.
    hooks.m_info.m_string = [ this ]( const std::string& id ) { return string_db_get( m_string_db, id ); };
    hooks.m_font = [ this ]( const entity_t& e ) { return m_ui_pass ? ui_screen_font( e, "Font", ui_screen_environment( ) ) : 0u; };
    hooks.m_measure_of = [ this ]( const entity_t& e, const std::string& text, float aspect ) {
        const ui_slot_t* name = ui_property_value( e, "Font" );
        std::uint8_t params[ 60 ] = { };
        std::string asset;
        ui_font_params( m_font_db, name ? name->m_text : std::string( ), params, asset );
        const ui_font_t* font = m_ui_pass ? m_ui_pass->font_of( ui_screen_font( e, "Font", ui_screen_environment( ) ) ) : nullptr;
        return font ? ui_font_text_width( *font, text.c_str( ), params, aspect ) : 0.0f;
    };
#endif
    // The selectors (UI batch 32): one world per process, so these documents stay here until c_world holds them (world.hxx is the world agent's).
    static value_t option_db, champ_db, font_db, skin_db;
    const auto db = [ this ]( const char* asset, value_t& out ) -> const value_t* {
        if ( out.tag == 0 )
        {
            std::vector< std::uint8_t > bytes;
            if ( !m_archive.read( asset, bytes ) )
                return nullptr;
            out = c_reader( bytes ).parse( );
        }
        return &out;
    };
    hooks.m_option_db = db( "VuDBAsset/OptionDB", option_db );
    hooks.m_champ_db = db( "VuDBAsset/ChampDB", champ_db );
    hooks.m_font_db = db( "VuDBAsset/FontDB", font_db );
    static value_t page_db;
    hooks.m_page_db = db( "VuDBAsset/PageLayoutDB", page_db );
#if HYDRO_UI_PASS && HYDRO_UI_TEXT
    if ( m_ui_pass )
    {
        hooks.m_page_hooks = ui_screen_environment( ).m_page;
        hooks.m_page_env.m_default_font = m_ui_pass->font_asset( "Dev" );
        hooks.m_page_env.m_default = hooks.m_page_env.m_default_font != 0u;
    }
#endif
    hooks.m_skin_db = db( "VuDBAsset/SkinDB", skin_db );
    hooks.m_boat_db = &m_boat_db;
    hooks.m_track_db = &m_track_db;
    hooks.m_platform = ui_platform_of( ui_profile_all_unlocked( ) );   // the declared scaffold profile, as --front-end-defaults (the save-backed one: the world's)
    hooks.m_delta = [ this ] { return g_menu_dt; };
    hooks.m_touch_device = &m_touch;
    hooks.m_settings = &m_settings.ui;
    hooks.m_settings_platform = &m_settings_platform;
    hooks.m_champ = &m_champ_env;
    return hooks;
}

inline void c_world::menu_enter( )
{
    load_background( );   // the 3D behind the menus: VuBackground, as --front-end has it
    menu_bind( );
    {
        std::vector< std::uint8_t > bytes;
        if ( !m_menu_input && m_archive.read( "VuDBAsset/InputDB", bytes ) )
        {
            m_menu_input_db = c_reader( bytes ).parse( );
            m_menu_input = std::make_shared< platform_input_t >( );
            platform_input_init( *m_menu_input, m_menu_input_db );
            saves_input( *m_menu_input );
        }
    }
    m_menu_engine.m_platform = [ this ]( const char* what, int ) {
        if ( std::strcmp( what, "cloud" ) == 0 )   // 0x4BF96D 0x5547F0, 0x4BF978 0x4ABD60: the pair
            saves_commit( );
    };
    m_menu = std::make_unique< c_front_end >( m_menu_engine, m_constant_db );
    m_menu->enter( m_menu_util );
    LOG_SUCCESS( "front end: VuFrontEndGameMode, the first screen %s", m_menu->m_current.c_str( ) );
}

// The engine's project hooks, the front end's and the title sequence's.
inline void c_world::menu_bind( )
{
    const race_session_dbs_t dbs = race_dbs( );
    m_menu_engine = front_end_engine_t{ };
    m_menu_engine.m_fade = action_fade_t::created( );
    m_menu_engine.m_display = [ ]( int& w, int& h ) { w = 1280; h = 720; };
    m_menu_engine.m_load_project = [ this ]( const std::string& name ) -> int {
        const std::string asset = "VuProjectAsset/" + name;
        std::vector< std::uint8_t > bytes;
        if ( !m_archive.read( asset.c_str( ), bytes ) )
        {
            LOG_WARNING( "front end: %s not in the pack", asset.c_str( ) );
            return 0;
        }
        const value_t document = c_reader( bytes ).parse( );
        const value_t* project = find( document, "VuProject" );
        if ( !project )
            return 0;
        const entity_registry_t registry = build_entity_registry( m_archive.names( "VuTemplateAsset/" ) );
        std::unique_ptr< entity_t > root = ui_hud_load( *project, race_project_file( m_archive, asset.c_str( ) ), registry,
                                                        [ this ]( const std::string& n ) { return template_document( n ); } );
        if ( !root )
            return 0;
        const int id = ++m_menu_serial;
        menu_project_t& p = m_menu_projects[ id ];
        p.root = std::move( root );
        p.binding = std::make_unique< c_ui_binding >( *p.root, menu_hooks( ) );
        LOG_INFO( "front end: screen %s loaded (project %d)", name.c_str( ), id );
        return id;
    };
    m_menu_engine.m_init_project = [ this ]( int id ) {
        m_menu_projects[ id ].binding->config_setup( );
        m_menu_projects[ id ].binding->project_initialize( );
    };
    m_menu_engine.m_release_project = [ this ]( int id ) {
        m_menu_projects[ id ].binding->project_release( );
        m_menu_projects.erase( id );
    };
    m_menu_engine.m_is_screen = [ this ]( int id ) { return ui_common_is_screen( *m_menu_projects[ id ].root ); };
    m_menu_engine.m_screen_tick = [ this ]( int id, float dt, bool input ) {
        g_menu_dt = dt;
        m_menu_projects[ id ].binding->tick( dt, input );
    };
    m_menu_engine.m_screen_draw = [ this ]( int id, float alpha ) {
#if HYDRO_UI_PASS
        if ( m_ui_pass )
        {
            menu_project_t& p = m_menu_projects[ id ];
            const ui_frame_inputs_t in = ui_screen_inputs( *p.binding, ui_screen_environment( ) );
            ui_screen_frame( *p.root, m_ui_pass->m_impl, m_ui_pass->m_sort, alpha, 4, &in );
        }
#endif
    };
}

// Slot 3: 0x4BF9B0, its data step over gameutil +0x50; the GameData the Action mode's load takes.
inline void c_world::menu_exit( )
{
    const race_session_dbs_t dbs = race_dbs( );
    ui_game_env_t env;
    env.boat_db = dbs.boat_db;
    env.track_db = dbs.track_db;
    env.random = &m_vurandom;
    env.string_db = [ this ]( const char* id ) { return string_db_get( m_string_db, id ? id : "" ); };
    const ui_game_config_t config = m_menu->exit( m_menu_util, env );
    LOG_INFO( "front end: exit to %s, GameData builder 0x%X %s", m_menu->m_mode.c_str( ), config.builder, config.built ? "built" : "not built" );
    m_menu.reset( );
    m_menu_projects.clear( );
}

// VuOnAnyButtonOrKeyEntity (vtable 0x7385E0): slot 6 0x423560 registers its Decision tick 0x423650 and a keyboard callback (+0x5C, slot 0
// 0x4233A0: +0x68 = 1). The tick: pad 0's button word (g 0x759040 vt+0x18( 0 ) +0xC) has a bit it did not have, or the callback ran ->
// plug "Trigger" (0x504770, its vt+8). Read, no case: the keyboard's callback is every key's down here, device.pressed( ). In the
// front end only, with the screen's own binding; ours: the walk over the tree stands for the tick registered per entity.
inline void c_world::menu_any_key( )
{
    if ( !m_menu_input || !m_menu || !m_menu->m_project )
        return;
    bool any = false;
    const std::uint8_t* pressed = m_menu_input->device.pressed( );
    for ( int k = 0; k < 81 && !any; ++k )   // m_pressed +0xFF holds 81 keys (0x5391C0)
        any = pressed[ k ] != 0;
    const std::uint32_t buttons = m_menu_input->device.controller( 0 ).buttons;
    any = any || ( buttons & ~m_menu_pad ) != 0u;
    m_menu_pad = buttons;
    if ( !any )
        return;
    menu_project_t& p = m_menu_projects[ m_menu->m_project ];
    const auto walk = [ & ]( const auto& self, entity_t& e ) -> void {
        if ( e.m_ctor && e.m_ctor->m_vtable == 0x7385E0u )
            p.binding->fire_plug( e, "Trigger", ui_params_t{ } );
        for ( auto& child : e.m_children )
            self( self, *child );
    };
    walk( walk, *p.root );
}

// Slot 4 0x4BFC10, with what ticks beside it in retail: the input phase, the fade's Final tick.
inline const std::string& c_world::menu_tick( float dt )
{
    if ( m_menu_input )
    {
        static const char* keys = c_engine::option( "menu-keys" );   // ours (a test driver): as --keys, for the front end
        if ( keys )
        {
            std::istringstream list( keys );
            std::string one;
            while ( std::getline( list, one, ',' ) )
            {
                const std::size_t a = one.find( ':' ), b = one.rfind( ':' );
                if ( a == std::string::npos || b <= a || std::atoi( one.c_str( ) ) != m_menu_frames )
                    continue;
                if ( one[ b + 1 ] == '1' )
                    m_menu_input->device.key_down( input_key_index( one.substr( a + 1, b - a - 1 ).c_str( ) ) );
                else
                    m_menu_input->device.key_up( input_key_index( one.substr( a + 1, b - a - 1 ).c_str( ) ) );
            }
        }
        platform_input_tick( *m_menu_input, dt, dt );
        m_menu_engine.m_control_method = static_cast< std::uint32_t >( m_menu_input->device.control_method( ) );
    }
    ++m_menu_frames;
    // Decision, in registration order: the mode manager 0x4CBFD0 (boot) before the screen's 0x423650 (its slot 6), so a key that sets the next
    // screen is not read again by that screen in the same frame.
    const std::string& next = m_menu->tick( dt );
    menu_any_key( );
    m_menu_engine.m_fade.tick( dt, nullptr );
    return next;
}
