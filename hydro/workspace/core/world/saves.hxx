#pragma once

// c_world's saves (world batch 38), the wiring of engine save/ per the boat agent's list: nothing here is a retail function.
//   Session start: the profile's load 0x5547E0, the input manager's 0x510E10 on each manager as it is made, the game save's 0x4ABD20.
//   The commit retail makes at LoadLevel 0x4BA396 and the front end's enter 0x4BF96D: profile 0x5547F0, game save 0x4ABD60.
//   App exit 0x4DAA28: the profile.
// Where: save_seam's root. `--save-dir <folder>` (ours, a test driver) redirects it. Any other test driver (--bench, --background,
// --exit-after, --input-script, --keys, --menu-keys, --fixed-step) leaves the saves OFF: only a plain run touches the player's folder.
// "OnSaveProfile"'s other listeners, VuSettingsManager 0x4B4800 and VuCloudManager 0x5C5D00: game/settings.hxx, agent/boat's: not on this
// tree yet; their point is saves_profile_listeners.

inline void c_world::saves_start( )
{
    if ( m_saves_started )
        return;
    m_saves_started = true;
    if ( const char* dir = c_engine::option( "save-dir" ) )
        save_seam::redirect( dir );
    else
        for ( const char* driver : { "bench", "background", "exit-after", "input-script", "keys", "menu-keys", "fixed-step" } )
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

// "OnSaveProfile": VuInputManagerImpl 0x5111B0 on the live manager (the race's, else the menus').
inline void c_world::saves_profile_listeners( )
{
    platform_input_t* in = m_race.input ? m_race.input.get( ) : m_menu_input.get( );
    if ( in )
        in->manager.save_profile( m_profile.data );
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
    m_race.world.save_game = [ this ] { saves_game( ); };
    m_race.world.input_profile = [ this ]( platform_input_t& in ) { saves_input( in ); };
}
