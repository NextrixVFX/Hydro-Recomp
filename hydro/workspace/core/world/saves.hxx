#pragma once

// c_world's saves (world batch 38), the wiring of engine save/ per the boat agent's list: nothing here is a retail function.
//   Session start: the profile's load 0x5547E0, the input manager's 0x510E10 on each manager as it is made, the game save's 0x4ABD20.
//   The commit retail makes at LoadLevel 0x4BA396 and the front end's enter 0x4BF96D: profile 0x5547F0, game save 0x4ABD60.
//   App exit 0x4DAA28: the profile.
// Where: save_seam's root. `--save-dir <folder>` (ours, a test driver) redirects it. Any other test driver (--bench, --background,
// --exit-after, --input-script, --keys, --menu-keys, --fixed-step, --autopilot) leaves the saves OFF: only a plain run touches the player's folder.
// The settings, the cloud manager's profile half and the unlock manager (boat's game/settings.hxx, game/unlock.hxx) load here, and stay
// up with the saves off: a test run reads ConstantDB's defaults, as a first launch does.
//   Hooks, the platform's: the category volumes (no audio device), the tilt device g 0x759000 vt+0x54 / +0x58 (the bytes 0x758FCF, 0x758FFC:
//   no sensor), "StoreBeta" 0x4D3170 (no), g_vudevconfig's "UnlockAll", "DeviceType", "DisplayScale" (a shipped game has no dev config).
//   The flip interval is held in the settings; the swap chain does not take it yet: FLAGGED. VuWater's four fidelity fields and the lens are
//   written into m_settings_platform only: FLAGGED, the water agent's.

inline void c_world::saves_start( )
{
    if ( m_saves_started )
        return;
    m_saves_started = true;
    struct finish_t
    {
        c_world& w;
        ~finish_t( ) { w.settings_start( ); }
    } finish{ *this };   // every way out: the saves off is still a game with settings
    if ( const char* dir = c_engine::option( "save-dir" ) )
        save_seam::redirect( dir );
    else
        for ( const char* driver : { "bench", "background", "exit-after", "input-script", "keys", "menu-keys", "fixed-step", "autopilot" } )
            if ( c_engine::option( driver ) != nullptr )
            {
                LOG_INFO( "saves: off (--%s, no --save-dir): a test run never touches the player's folder", driver );
                return;
            }
    if ( save_seam::root( ).empty( ) )
    {
        LOG_WARNING( "saves: no root (%%APPDATA%% unset) - off" );
        return;
    }
    m_saves_on = true;
    m_profile.load( );
    m_cloud.hooks.corrupt_box = [ ] { LOG_WARNING( "saves: the game save is corrupt (SaveGame_Corrupt_Heading's box: the UI's, not shown)" ); };
    m_cloud.hooks.broadcast = [ this ]( const char* name ) {
        if ( std::strcmp( name, "OnCloudSavePreSave" ) == 0 )                // 0x4B6A90
            m_unlock.save( json_member( m_cloud.data, "Unlock" ) );
        else if ( std::strcmp( name, "OnCloudSaveDataChanged" ) == 0 )       // 0x4B6AC0
            m_unlock.load( member( m_cloud.data, "Unlock" ) );
    };
    m_cloud.init( );
    {
        std::vector< std::uint8_t > bytes;
        if ( m_archive.read( "VuDBAsset/HiddenPackagesDB", bytes ) )   // VuHiddenPackageManagerImpl slot 5 0x4B0270
            m_hidden_db = c_reader( bytes ).parse( );
        else
            LOG_WARNING( "saves: VuDBAsset/HiddenPackagesDB not in the pack" );
    }
    m_hidden.init( &m_hidden_db );
    m_hidden.save = [ this ] { saves_game( ); };
    m_hidden.progress = [ ]( const char*, float ) { };   // the "Collector" achievement 0x4AA1A0: a platform service, a hook
    const std::int32_t packages = c_hidden_packages::found_total( m_cloud.data );
    LOG_SUCCESS( "saves: %s - profile status %d, game save status %d: %d races, %d time trials, %d slaloms, %d packages", save_seam::root( ).string( ).c_str( ), m_profile.status, m_cloud.status,
                 ui_progress_count( m_cloud.data, "Race" ), ui_progress_count( m_cloud.data, "TimeTrial" ), ui_progress_count( m_cloud.data, "Slalom" ), packages );
}

// The app init's 0x4DA570 .. 0x4024E0's 0x402700: the platform's defaults, slot 5 0x4B4800, then the managers' loads over the profile. "High",
// 1.0, 1; 0x540C50's tier from GetFeatureLevel (0x9100 or under 0, up to 0x9300 1, else 2): 0: 0.75 and 2; 1: flip 2.
inline void c_world::settings_start( )
{
    title_db( );
    float display = 1.0f;
    std::int32_t flip = 1;
    void* device = nullptr;
    void* context = nullptr;
    c_engine::api( device, context );
    const int level = device ? static_cast< int >( static_cast< ID3D11Device* >( device )->GetFeatureLevel( ) ) : 0xB000;
    const int tier = level <= 0x9100 ? 0 : level > 0x9300 ? 2 : 1;
    if ( tier <= 0 )
    {
        display = 0.75f;
        flip = 2;
    }
    else if ( tier <= 1 )
        flip = 2;
    settings_configure( m_settings, display, "High", flip );
    m_settings_platform.m_sys_54 = [ ] { return false; };
    m_settings_platform.m_sys_58 = [ ] { return false; };
    m_settings_platform.m_flip = [ this ]( int v ) { m_settings.ui.m_flip_interval = v; };
    m_settings_hooks.category = [ ]( const char*, float ) { };
    m_settings_hooks.flip_interval = m_settings_platform.m_flip;
    m_settings_hooks.has_accelerometer = m_settings_platform.m_sys_54;
    m_settings_hooks.has_tilt = m_settings_platform.m_sys_58;
    settings_load( m_settings, m_constant_db, m_profile.data, m_settings_hooks, m_settings_platform );
    cloud_profile_load( m_cloud_profile, m_profile.data );
    {
        std::vector< std::uint8_t > bytes;
        if ( m_archive.read( "VuDBAsset/UnlockDB", bytes ) )
            m_unlock_db = c_reader( bytes ).parse( );
        else
            LOG_WARNING( "saves: VuDBAsset/UnlockDB not in the pack" );
    }
    m_unlock.store_beta = [ ] { return false; };
    m_unlock.init( m_unlock_db.tag ? &m_unlock_db : nullptr, value_t{ }, m_cloud.data );   // getParam( "UnlockAll" ): 0x4FDB70 answers null
    unlock_bind( );
    {
        float fov = 0.0f;
        ui_json_float( member( member( member( m_constant_db, "VuDBAsset" ), "Camera" ), "VerticalFOV" ), fov );
        LOG_INFO( "settings: ConstantDB Camera VerticalFOV %.3f degrees (engine viewport's fov_y 1.05 rad = 60.16 degrees)", fov );
    }
    LOG_INFO( "settings: fidelity %s, display %.2f, flip %d, auto throttle pad %d keys %d, steering %.2f, vibration %d; %d credits", m_settings.ui.m_fidelity.c_str( ), m_settings.ui.m_display,
              m_settings.ui.m_flip_interval, m_settings.auto_throttle_pad ? 1 : 0, m_settings.auto_throttle_keys ? 1 : 0, m_settings.ui.m_steering, m_settings.ui.m_vibration ? 1 : 0, m_unlock.total );
}

// The award screen's and the championship's slots over the managers (ui/award.hxx, ui/standings.hxx). The unlock row (0x47F790's VuJsonContainer) reaches
// the screen open at the time. g 0x758FE8's rewards slots 6 .. 11: boat's reward( ), not on this tree: see unlock_value (opening.hxx).
inline void c_world::unlock_bind( )
{
    static const auto queue = [ ]( race_session_t& race ) {
        return c_unlock_manager::found_t( [ &race ]( const value_t& row ) {
            if ( !race.screens.m_award )
                return;
            ui_award_unlock_t u;
            u.m_string_id = unlock_text( row, "StringID" );
            u.m_image = unlock_text( row, "Image" );
            u.m_type = unlock_text( row, "Type" );
            u.m_mode = unlock_text( row, "Mode" );
            race.screens.m_award->m_screen->on_unlock( std::move( u ) );
        } );
    };
    ui_award_hooks_t& a = ui_game_award_setup( ).m_hooks;
    a.m_credits = [ this ] { return m_unlock.total; };
    a.m_credits_room = [ this ]( std::int32_t& room ) { return m_unlock.next_credits( room ); };
    a.m_credits_add = [ this ]( std::int32_t delta, std::int32_t flag ) {
        const c_unlock_manager::found_t found = queue( m_race );
        m_unlock.add_credits( delta, flag, &found );
    };
    a.m_unlock = [ this ]( const std::string& name ) {
        const c_unlock_manager::found_t found = queue( m_race );
        m_unlock.add_feat( name, &found );
    };
    a.m_screen_busy = [ this ]( bool busy ) {
        ui_params_t p;
        ui_params_add_bool( p, busy );
        race_broadcast( "OnAwardScreenBusy", &p );
    };
    a.m_input = [ this ]( const char* name ) { return m_race.input && m_race.input->manager.pressed( 0, name ); };
    a.m_touches = [ ] { return 0x7FFF; };   // g 0x759070 +0x1C: no finger down
    a.m_play = [ this ]( const char* event ) {
        std::uint32_t id = 0;
        if ( m_sound.get_event && m_sound.get_event( m_sound.user, event, 1u, &id, nullptr ) == 0 && m_sound.start )
            m_sound.start( m_sound.user, id );
    };
    ui_champ_env_t& c = m_champ_env;
    c.m_root = &m_menu_util;
    c.m_boat_db = &m_boat_db;
    {
        std::vector< std::uint8_t > bytes;
        if ( m_archive.read( "VuDBAsset/ChampDB", bytes ) )
            m_champ_db = c_reader( bytes ).parse( );
    }
    c.m_champ_db = &m_champ_db;
    c.m_random = &m_vurandom;
    c.m_string = [ this ]( const char* id ) { return string_db_get( m_string_db, id ? id : "" ); };
    c.m_broadcast = [ this ]( const char* event, const ui_params_t& p ) {
        if ( m_menu )
            m_menu->handle( fsm_hash( event ), p );
    };
    c.m_plug = [ this ]( const char* plug, const ui_params_t& p ) {
        if ( !m_menu || !m_menu->m_project )
            return;
        menu_project_t& project = m_menu_projects[ m_menu->m_project ];
        const auto walk = [ & ]( const auto& self, entity_t& e ) -> void {
            if ( e.m_ctor && e.m_ctor->m_vtable == k_ui_champ_vtable )
                project.binding->fire_plug( e, plug, p );
            for ( auto& child : e.m_children )
                self( self, *child );
        };
        walk( walk, *project.root );
    };
    c.m_names = [ this ]( std::int32_t value, std::vector< std::string >& names ) { return m_unlock.unlock_count_for( value, names ) != 0; };
    ui_champ_save_bind( c, m_cloud );
    ui_champ_award_bind( c, m_race.screens );
}

// "OnSaveProfile": VuInputManagerImpl 0x5111B0 on the live manager (the race's, else the menus'), VuSettingsManager 0x4B5240, VuCloudManager 0x5C63B0.
inline void c_world::saves_profile_listeners( )
{
    platform_input_t* in = m_race.input ? m_race.input.get( ) : m_menu_input.get( );
    if ( in )
        in->manager.save_profile( m_profile.data );
    settings_save( m_settings, m_profile.data, m_settings_hooks );
    cloud_profile_save( m_cloud_profile, m_profile.data );
}

inline void c_world::saves_commit( )
{
    if ( !m_saves_on )
        return;
    m_profile.save( [ this ] { saves_profile_listeners( ); } );
    m_cloud.save( );
    LOG_INFO( "saves: committed" );
}

// App exit 0x4DAA28: the profile alone.
inline void c_world::saves_exit( )
{
    if ( m_saves_on )
        m_profile.save( [ this ] { saves_profile_listeners( ); } );
}

inline void c_world::saves_game( )
{
    if ( m_saves_on )
        m_cloud.save( );
}

inline void c_world::saves_input( platform_input_t& in )
{
    if ( m_saves_on )
        in.manager.load_profile( m_profile.data );
}

inline void c_world::saves_bind( )
{
    m_race.world.save_doc = m_saves_on ? &m_cloud.data : nullptr;
    m_race.world.hidden = m_saves_on ? &m_hidden : nullptr;
    m_race.world.settings = &m_settings;
    m_race.world.unlock = &m_unlock;
    m_race.world.save_game = [ this ] { saves_game( ); };
    m_race.world.input_profile = [ this ]( platform_input_t& in ) { saves_input( in ); };
}
