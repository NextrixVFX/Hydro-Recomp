#pragma once

#include <workspace/core/world/video.hxx>

// c_world's front end - member definitions (world batch 37): VuFrontEndGameMode (ui/flow.hxx) driven from the loaded Screens/* projects,
// through the mode manager (world/mode.hxx). Nothing here is a retail function: it binds c_front_end's hooks to OUR objects:
//   m_load_project   0x4B4360 through the UI's world (ui_hud_load), a c_ui_binding per project
//   m_init_project   0x4C0550's init: 0x500B10 + "OnProjectInitialized" (project_initialize)
//   m_release_project  0x500B80 (project_release), the deletion
//   m_screen_tick    the root's slot 12 0x629B90 (binding.tick)
//   m_screen_draw    the root's slot 13 0x629D60 (ui_screen_frame, into the UI pass)
// The binding's broadcasts reach the mode's event map 0x5156E0 (c_front_end::handle): OnPushScreen, OnSetScreen, OnStartActionGame ...
// Hooks that answer nothing, named: the title voice-over, analytics, the save check, 0x530BB0, VuProject's reference counts.

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
    boxes_release( m_game_screen_state );
    m_game_screen_state = screen_state_t{ };
    screen_state_release( m_pause_screen_state );
    m_action.reset( );
}

inline void c_world::action_enter( )
{
    m_has_chosen = true;
    m_chosen = member( m_menu_util, "GameData" );
    load_scene( "", nullptr, nullptr );
    m_has_chosen = false;
}

inline ui_bind_hooks_t c_world::menu_hooks( int id )
{
    ui_bind_hooks_t hooks;
    hooks.m_root = &m_menu_util;
    hooks.m_leaderboard = &m_menu_board;
    hooks.m_trophy_model = [ this, id ]( const entity_t& e, const char* model ) { menu_trophy_model( id, e, model ); };
    hooks.m_trophy_draw = [ this, id ]( const entity_t&, bool added, const bool* shown ) {
        const auto it = m_menu_projects.find( id );
        if ( it != m_menu_projects.end( ) )
            it->second.trophy_shown = added ? shown : nullptr;
    };
    hooks.m_broadcast_out = [ this ]( std::uint32_t hash, const ui_params_t& p ) {
        menu_poster( hash, p );
        menu_dock( hash, p );
        if ( m_menu )
            m_menu->handle( hash, p );
    };
    string_db_ready( );
    hooks.m_string_db = [ this ]( const char* key ) { return string_db_get( m_string_db, key ? key : "" ); };
    hooks.m_external = [ this, id ]( entity_t& e, const char* plug, const ui_params_t& p ) {
        menu_project_t& project = m_menu_projects[ id ];
        return screen_external( project.state, *project.binding, e, plug, p );
    };
    hooks.m_input = [ this ]( int pad, const char* name ) { return m_menu_input && m_menu_input->manager.pressed( pad, name ); };
    // A PLATFORM ANSWER, not read from anything: the license manager's "trial" is no (the owner's copy; the retail shot's Main shows Windows 8 Store, the full licence's text).
    hooks.m_trial = [ ] { return false; };   // the same answer as ui_profile_saved's p.license
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
    hooks.m_option_db = db( "VuDBAsset/OptionDB", m_option_db );
    hooks.m_champ_db = db( "VuDBAsset/ChampDB", m_champ_db );
    hooks.m_font_db = db( "VuDBAsset/FontDB", m_font_db );
    hooks.m_page_db = db( "VuDBAsset/PageLayoutDB", m_page_db );
#if HYDRO_UI_PASS && HYDRO_UI_TEXT
    if ( m_ui_pass )
    {
        hooks.m_page_hooks = ui_screen_environment( ).m_page;
        hooks.m_page_env.m_default_font = m_ui_pass->font_asset( "Dev" );
        hooks.m_page_env.m_default = hooks.m_page_env.m_default_font != 0u;
    }
#endif
#if HYDRO_UI_PASS
    if ( m_ui_pass )
    {
        hooks.m_crop = &m_ui_pass->m_impl.m_crop;
        hooks.m_inverse = m_ui_pass->m_impl.m_inverse;
    }
#endif
    hooks.m_skin_db = db( "VuDBAsset/SkinDB", m_skin_db );
    hooks.m_boat_db = &m_boat_db;
    hooks.m_track_db = &m_track_db;
    hooks.m_platform = ui_platform_of( menu_profile( ) );   // the saved state (world/external.hxx); --front-end-defaults keeps its test profile
    hooks.m_delta = [ this ] { return m_menu_dt; };
    hooks.m_sound = [ this ]( const char* name ) { ui_play( name ); };
    hooks.m_category = m_settings_hooks.category;
    hooks.m_touch_device = &m_touch;
    hooks.m_settings = &m_settings.ui;
    hooks.m_settings_platform = &m_settings_platform;
    hooks.m_champ = &m_champ_env;
    hooks.m_texture_exists = [ this ]( const char* name ) { return !m_archive.names( std::string( "VuTextureAsset/" ) + name ).empty( ); };
    // The remapping rows' capture (remap.hxx): the box 0x461940, the device, the input manager's slots 13 / 14 / 15.
    hooks.m_remap_hooks.m_open_box = [ this ]( const std::string& heading, bool can_auto, std::function< void( std::int32_t ) > done ) {
        m_remap_box = boxes_show( heading, "Control_Mapping_MB_Body", can_auto ? "MessageBox_Auto" : "", "MessageBox_Cancel" );
        if ( m_remap_box )
            m_remap_box->m_callback = [ done ]( msgbox_t& box ) { done( box.m_result ); };
    };
    hooks.m_remap_hooks.m_close_box = [ this ] {
        if ( m_remap_box )
            m_remap_box->m_destroyed = true;
        m_remap_box.reset( );
    };
    hooks.m_remap_hooks.m_channels = [ ] { return 6; };
    hooks.m_remap_hooks.m_axis = [ this ]( int i ) { return m_menu_input ? m_menu_input->device.controller( 0 ).axes[ i ] : 0.0f; };
    hooks.m_remap_hooks.m_button_bits = [ this ] { return m_menu_input ? m_menu_input->device.controller( 0 ).buttons : 0u; };
    hooks.m_remap_hooks.m_key = [ this ]( int k ) { return m_menu_input && m_menu_input->device.pressed( )[ k ] != 0; };
    hooks.m_remap_hooks.m_bind = [ this ]( int slot, std::int32_t config, const std::string& channel, int kind, int index ) {
        if ( !m_menu_input )
            return;
        input_binding_t b;
        b.type = kind;
        b.index = index;
        if ( slot == 15 )
            m_menu_input->manager.set_button( 0, config, channel.c_str( ), b );
        else if ( slot == 13 )
            m_menu_input->manager.set_positive( 0, config, channel.c_str( ), b );
        else
            m_menu_input->manager.set_negative( 0, config, channel.c_str( ), b );
    };
    hooks.m_remap_auto = [ this ]( std::int32_t config, std::uint8_t value ) {
        if ( config == 0 )
            m_settings.auto_throttle_pad = value != 0;
        else if ( config == 1 )
            m_settings.auto_throttle_keys = value != 0;
    };
    // Screens/Loading*: the leaves' slot 6 over the GameData the menus built. The unlock manager's reward slots, game/unlock.hxx.
    hooks.m_loading = [ this ]( std::uint32_t vt, std::int32_t place, std::string& out ) {
        ui_loading_env_t env;
        env.d = m_difficulty_env;
        env.d.m_root = &m_menu_util;
        env.m_constant_db = &m_constant_db;
        env.m_track_db = &m_track_db;
        env.m_string6 = env.m_string7 = [ this ]( const std::string& id ) { return string_db_get( m_string_db, id ); };
        env.m_reward = [ this ]( const char* mode, const std::string& key, std::int32_t tier ) {
            return std::strcmp( mode, "RubberDucky" ) == 0 ? m_unlock.reward_ducky( tier ) : m_unlock.reward( mode, key, tier );
        };
        ui_loading_text( env, vt, place, out );
    };
    // 0x440400: TrackDB [ level ].LoadingBackground, then its "_1" .. "_7" that exist; one by VuRandom's global draw (ui_champ_pick's arithmetic), which the draw shows.
    hooks.m_loading_image = [ this ]( ) {
        ui_loading_env_t env;
        env.d.m_root = &m_menu_util;
        const std::string base = ui_json_text( member( ui_db_entry( &m_track_db, ui_loading_level( env ) ), "LoadingBackground" ) );
        std::vector< std::string > names;
        const auto exists = [ this ]( const std::string& n ) { return !m_archive.names( "VuTextureAsset/" + n ).empty( ); };
        if ( exists( base ) )
            names.push_back( base );
        for ( int k = 1; k < 8; ++k )
            if ( const std::string n = base + "_" + std::to_string( k ); exists( n ) )
                names.push_back( n );
        if ( names.empty( ) || !m_champ_env.m_random )
            return std::string( );
        return names[ static_cast< std::size_t >( ui_champ_pick( *m_champ_env.m_random, static_cast< int >( names.size( ) ) ) ) ];
    };
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
    m_menu_engine.m_sound = [ this ]( const char* name ) { ui_play( name ); };
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
    // The leaderboard manager g 0x758FA8 with no online service: the read answers at once from the save (progress.hxx ui_progress_board_local).
    m_menu_board = ui_mini_board_hooks_t{ };
    m_menu_board.m_read = [ this ]( const std::string&, int, int ) { m_menu_board_row = ui_progress_board_local( m_cloud.data, m_menu_util, m_menu_board_score ); };
    m_menu_board.m_status = [ ] { return 1; };
    m_menu_board.m_count = [ this ] { return m_menu_board_row ? 1 : 0; };
    m_menu_board.m_row = [ this ]( int, std::string& name, std::int32_t& score ) {
        name = platform_gamer_tag( );
        score = m_menu_board_score;
    };
    m_menu_engine.m_fade = action_fade_t::created( );
    m_menu_engine.m_display = [ this ]( int& w, int& h ) { w = m_race.world.display_w; h = m_race.world.display_h; };
    m_menu_engine.m_load_project = [ this ]( const std::string& name ) -> int {
        std::unique_ptr< entity_t > root = menu_load_root( name );
        if ( !root )
            return 0;
        const int id = ++m_menu_serial;
        menu_project_t& p = m_menu_projects[ id ];
        p.root = std::move( root );
        p.binding = std::make_unique< c_ui_binding >( *p.root, menu_hooks( id ) );
        LOG_INFO( "front end: screen %s loaded (project %d)", name.c_str( ), id );
        return id;
    };
    m_menu_engine.m_init_project = [ this ]( int id ) {
        m_menu_projects[ id ].binding->config_setup( );
        m_menu_projects[ id ].binding->project_initialize( );
    };
    m_menu_engine.m_release_project = [ this ]( int id ) {
        menu_project_t& p = m_menu_projects[ id ];
        p.binding->project_release( );
        screen_state_release( p.state );
        m_menu_projects.erase( id );
    };
    m_menu_engine.m_is_screen = [ this ]( int id ) { return ui_common_is_screen( *m_menu_projects[ id ].root ); };
    m_menu_engine.m_screen_tick = [ this ]( int id, float dt, bool input ) {
        m_menu_dt = dt;
        m_menu_projects[ id ].binding->tick( dt, input );
    };
    m_menu_engine.m_screen_draw = [ this ]( int id, float alpha ) {
#if HYDRO_UI_PASS
        if ( m_ui_pass )
        {
            menu_project_t& p = m_menu_projects[ id ];
            const ui_screen_env_t screen = ui_screen_environment( );
            ui_frame_inputs_t in = ui_screen_inputs( *p.binding, screen );
            menu_board_frame( in, *p.binding, screen );
            ui_screen_frame( *p.root, m_ui_pass->m_impl, m_ui_pass->m_sort, alpha, 4, &in );
        }
#endif
    };
}

// 0x4B4360 through the UI's world: a project by name, its root.
inline std::unique_ptr< entity_t > c_world::menu_load_root( const std::string& name )
{
    const std::string asset = "VuProjectAsset/" + name;
    std::vector< std::uint8_t > bytes;
    if ( !m_archive.read( asset.c_str( ), bytes ) )
    {
        LOG_WARNING( "front end: %s not in the pack", asset.c_str( ) );
        return nullptr;
    }
    value_t document = c_reader( bytes ).parse( );
    if ( name == "Screens/SettingsGraphics" )   // ours, not retail: world/video.hxx
    {
        video_screen_patch( document );
        video_strings( m_string_db );
    }
    const value_t* project = find( document, "VuProject" );
    if ( !project )
        return nullptr;
    const entity_registry_t registry = build_entity_registry( m_archive.names( "VuTemplateAsset/" ) );
    return ui_hud_load( *project, race_project_file( m_archive, asset.c_str( ) ), registry, [ this ]( const std::string& n ) { return template_document( n ); } );
}

// Slot 3: 0x4BF9B0, its data step over gameutil +0x50; the GameData the Action mode's load takes.
inline void c_world::menu_exit( )
{
    const race_session_dbs_t dbs = race_dbs( );
    ui_game_env_t env;
    env.boat_db = dbs.boat_db;
    env.track_db = dbs.track_db;
    env.random = &m_vurandom;
    env.player_name = platform_gamer_tag( );
    env.championship_now = [ this ] { return &ui_progress_champ_state( m_cloud.data ); };   // 0x4C2300's progress vt+0x6C, after the screen's unload saved it
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
    m_menu_engine.m_message_box = m_boxes.drawing( );
    // Decision, in registration order: the mode manager 0x4CBFD0 (boot) before the screen's 0x423650 (its slot 6), so a key that sets the next
    // screen is not read again by that screen in the same frame.
    const std::string& next = m_menu->tick( dt );
    menu_any_key( );
    // The screen's entities registered on "Decision" (the remapping rows' 0x4623D0, the mini boards' 0x47E170, the ladders' 0x47B640): a screen under a box keeps them.
    if ( m_menu )
    {
        const auto at = m_menu_projects.find( m_menu->m_project );
        if ( at != m_menu_projects.end( ) )
            at->second.binding->decision( );
    }
    m_menu_engine.m_fade.tick( dt, nullptr );
    return next;
}
