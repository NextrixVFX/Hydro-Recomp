#pragma once

// OURS, NOT RETAIL (the user's request, 11-02): the Benchmark mode. Nothing here is a retail function; it drives retail's own paths.
//   The menu: Screens/Main's parsed document is changed in memory at load (never a game file): retail's "Xbox Store Button" goes, retail's
//     "Windows Store Button" entity takes its row and becomes Benchmark. Its Pressed still reaches ShowWebPage01, whose input menu_hooks' m_external answers.
//   The run: one Race per playable TrackDB row, through the normal GameData builder 0x4C0A50 and VuActionGameMode's LoadingScreen; the next
//     level by "OnRestartActionGame" 0x4B9B90 (next = "Action"), the last back by 0x4B9B70.
//   The race: no human. GameData's Boats[0] is "AI", so every boat is a VuAiDriver in the AI manager, at the hardest tier; the boat manager's viewport
//     index follows the leader (+0x4CC 0), holding each cut k_benchmark_hold seconds; the HUD reads that boat; the first boat over the line (+0x32F) ends the level.
//   The time scale: race_session_t::time_scale on the session's dt only; Present and the frame rate shown are untouched.
//   The HUD: two clones of retail's "Time" VuHUDElementEntity in HUDs/RaceGameHUD's document, Types 100 / 101 (ui/hud.hxx), drawn by the UI pass.
//   The saves: while a run is active nothing commits and the race has no save document (saves.hxx); no results screen is reached, so no awards.

#include <algorithm>

inline constexpr const char* k_benchmark_boat = "Vector";      // OptionDB BoatConfig's first choice, its first skin: what the Boat screen opens on
inline constexpr const char* k_benchmark_skin = "VectorSkin01";
inline constexpr float k_benchmark_hold = 4.0f;                 // seconds between cuts

template < typename F >
inline void benchmark_walk( value_t& v, const F& f )
{
    if ( v.kind == value_t::kind_t::object )
    {
        f( v );
        for ( auto& kv : v.object )
            benchmark_walk( kv.second, f );
    }
    else if ( v.kind == value_t::kind_t::list )
        for ( value_t& child : v.list )
            benchmark_walk( child, f );
}

inline bool benchmark_named( const value_t& v, const char* name )
{
    const value_t* n = find( v, "name" );
    return n && n->text == name;
}

inline void benchmark_retarget( value_t& v, const char* from, const char* to )
{
    benchmark_walk( v, [ & ]( value_t& o ) {
        for ( auto& kv : o.object )
            if ( kv.first == "EntityName" && kv.second.text == from )
                kv.second.text = to;
    } );
}

inline value_t& benchmark_properties( value_t& entity ) { return json_member( json_member( entity, "data" ), "Properties" ); }

inline void benchmark_number( value_t& v, int n )
{
    v.integer = n;
    v.real = static_cast< float >( n );
}

// Screens/Main: the two store buttons become one, at the upper one's row, between Help & Options and the wrap to Single Player.
inline void benchmark_menu_patch( value_t& document )
{
    static const char* const xbox = "Main/Buttons/Xbox Store Button";
    static const char* const help = "Main/Buttons/HelpOptionsButton";
    static const char* const store = "Main/Buttons/Windows Store Button";
    value_t row;
    benchmark_walk( document, [ & ]( value_t& o ) {
        if ( benchmark_named( o, "Xbox Store Button" ) )
            row = json_member( benchmark_properties( o ), "Y" );
    } );
    benchmark_walk( document, [ & ]( value_t& o ) {
        if ( benchmark_named( o, "HelpOptionsButton" ) )
            benchmark_retarget( o, xbox, store );
        else if ( benchmark_named( o, "Windows Store Button" ) )
        {
            benchmark_retarget( o, xbox, help );
            json_member( benchmark_properties( o ), "Y" ) = row;
            json_member( benchmark_properties( o ), "String ID" ).text = "Benchmark";
        }
        else if ( benchmark_named( o, "SetString01" ) )
            json_member( benchmark_properties( o ), "Value" ).text = "Benchmark";
        else if ( benchmark_named( o, "SetInfoString Windows 8" ) )
            json_member( benchmark_properties( o ), "Info String ID" ).text = "Benchmark_Info";
    } );
    const auto gone = [ ]( const value_t& e ) { return benchmark_named( e, "Xbox Store Button" ); };
    const auto prune = [ & ]( const auto& self, value_t& v ) -> void {
        if ( v.kind == value_t::kind_t::list )
            v.list.erase( std::remove_if( v.list.begin( ), v.list.end( ), gone ), v.list.end( ) );
        for ( value_t& child : v.list )
            self( self, child );
        for ( auto& kv : v.object )
            self( self, kv.second );
    };
    prune( prune, document );
}

// HUDs/RaceGameHUD: the frame rate and the level at the bottom left.
inline constexpr int k_benchmark_hud_x = 24, k_benchmark_hud_y = 650;

inline void benchmark_hud_patch( value_t& document )
{
    const auto add = [ & ]( const auto& self, value_t& v ) -> void {
        if ( v.kind == value_t::kind_t::list )
        {
            value_t time, lap;
            for ( const value_t& e : v.list )
            {
                if ( benchmark_named( e, "Time" ) )
                    time = e;
                if ( benchmark_named( e, "LapCount" ) )
                    lap = e;
            }
            if ( time.kind == value_t::kind_t::object && lap.kind == value_t::kind_t::object )
            {
                for ( const char* key : { "Horizontal Ratio" } )
                    benchmark_number( json_member( benchmark_properties( time ), key ), 0 );
                json_member( benchmark_properties( time ), "Horizontal Alignment" ).text = "left";
                value_t fps = time, level = time;
                json_member( fps, "name" ).text = "BenchmarkFps";
                benchmark_number( json_member( benchmark_properties( fps ), "X" ), k_benchmark_hud_x );
                benchmark_number( json_member( benchmark_properties( fps ), "Y" ), k_benchmark_hud_y );
                json_member( level, "name" ).text = "BenchmarkLevel";
                benchmark_number( json_member( benchmark_properties( level ), "X" ), k_benchmark_hud_x );
                benchmark_number( json_member( benchmark_properties( level ), "Y" ), k_benchmark_hud_y + 50 );
                v.list.push_back( std::move( fps ) );
                v.list.push_back( std::move( level ) );
                return;
            }
        }
        for ( value_t& child : v.list )
            self( self, child );
        for ( auto& kv : v.object )
            self( self, kv.second );
    };
    add( add, document );
}

// The TrackDB rows whose level is in the pack.
inline void c_world::benchmark_start( )
{
    if ( m_benchmark.active || !m_menu )
        return;
    m_benchmark = benchmark_t{ };
    // OptionDB Track[0]'s Choices: the Select Track screen's order.
    const race_session_dbs_t dbs = race_dbs( );
    const value_t& options = member( member( m_option_db, "VuDBAsset" ), "Track" );
    static const value_t none;
    const value_t& choices = options.tag == 5 && !options.list.empty( ) ? member( options.list[ 0 ], "Choices" ) : none;
    for ( const value_t& choice : choices.list )
    {
        const std::string track = ui_json_text( member( choice, "Value" ) );
        const std::string level = ui_json_text( member( ui_json_find( member( *dbs.track_db, "VuDBAsset" ), track ), "ProjectAsset" ) );
        // 0x4D3480: "Games/" + the part after the last slash + the game type.
        const std::vector< std::string > game = m_archive.names( "VuProjectAsset/Games/" + level.substr( level.rfind( '/' ) + 1 ) + "Race" );
        if ( std::find( game.begin( ), game.end( ), std::string( ) ) != game.end( ) )
            m_benchmark.tracks.push_back( track );
    }
    LOG_INFO( "benchmark: pressed, %zu levels in the pack", m_benchmark.tracks.size( ) );
    if ( m_benchmark.tracks.empty( ) )
        return;
    m_benchmark.game_config = member( m_menu_util, "GameConfig" );
    m_benchmark.boat_config = member( m_menu_util, "BoatConfig" );
    m_benchmark.active = true;
    value_t& properties = ui_json_child( ui_json_at( ui_json_child( ui_json_child( m_menu_util, "BoatConfig" ), "PlayerBoats" ), 0 ), "Properties" );
    ui_json_set_string( ui_json_child( properties, "Boat" ), k_benchmark_boat );
    ui_json_set_string( ui_json_child( properties, "Skin" ), k_benchmark_skin );
    ui_json_set_string( ui_json_child( ui_json_child( m_menu_util, "GameConfig" ), "Mode" ), "Race" );
    benchmark_track( );
    LOG_INFO( "benchmark: %zu levels in the %s", m_benchmark.tracks.size( ), k_benchmark_boat );
    m_menu->handle( fsm_hash( "OnStartActionGame" ), ui_params_t{ } );
}

inline void c_world::benchmark_track( )
{
    const std::string& track = m_benchmark.tracks[ m_benchmark.index ];
    ui_json_set_string( ui_json_child( ui_json_child( m_menu_util, "GameConfig" ), "Track" ), track.c_str( ) );
    g_benchmark_hud[ 0 ] = "--- FPS";
    g_benchmark_hud[ 1 ] = track;
    m_benchmark.timing = false;
    m_benchmark.cut_set = false;
    m_benchmark.frames.clear( );
}

// Between levels (action_enter): GameData through the same builder the front end's exit runs.
inline void c_world::benchmark_next_data( )
{
    benchmark_track( );
    ui_game_config( "Action", m_menu_util, menu_game_env( ) );
}

// action_enter, every level: the hardest tier, and the human's boat a VuAiDriver like the rest.
inline void c_world::benchmark_enter( )
{
    if ( m_benchmark.index > 0 )
        benchmark_next_data( );
    value_t& data = ui_json_child( m_menu_util, "GameData" );
    ui_json_set_string( ui_json_child( data, "Difficulty" ), "Hard" );
    value_t& boat = ui_json_at( ui_json_child( data, "Boats" ), 0 );
    ui_json_set_string( ui_json_child( ui_json_child( boat, "Properties" ), "Driver Type" ), "AI" );
}

// Once a frame of a run's race, with real time: the smoothed rate twice a second, the level's samples, the level's end.
inline void c_world::benchmark_frame( )
{
    benchmark_t& b = m_benchmark;
    const auto now = std::chrono::steady_clock::now( );
    if ( m_pause_menu && m_pause_menu->paused( ) )   // the race stands still: no cut, no samples
    {
        b.timing = false;
        return;
    }
    if ( !b.timing )
    {
        b.timing = true;
        b.window = b.last = now;
        b.window_frames = 0;
    }
    else
    {
        b.frames.push_back( std::chrono::duration< float >( now - b.last ).count( ) );
        b.last = now;
        ++b.window_frames;
        const float span = std::chrono::duration< float >( now - b.window ).count( );
        if ( span >= 0.5f )
        {
            char text[ 32 ];
            std::snprintf( text, sizeof( text ), "%d FPS", static_cast< int >( static_cast< float >( b.window_frames ) / span + 0.5f ) );
            g_benchmark_hud[ 0 ] = text;
            b.window = now;
            b.window_frames = 0;
        }
    }
    if ( b.finishing || !m_action )
        return;
    auto& list = m_race.manager.lists;
    bool over = false;
    for ( std::size_t k = 0; k < list.all.boats.size( ); ++k )
    {
        const std::uint8_t* boat = static_cast< const std::uint8_t* >( list.all.boats[ k ] );
        over = over || boat[ 0x32F ] != 0;
        std::int32_t place;
        std::memcpy( &place, boat + 0x4CC, 4 );
        if ( place != 0 || static_cast< std::int32_t >( k ) == list.viewport )
            continue;
        if ( !b.cut_set || std::chrono::duration< float >( now - b.cut ).count( ) >= k_benchmark_hold )
        {
            if ( b.cut_set )
                LOG_INFO( "benchmark: the camera cuts to boat %zu", k );
            list.viewport = static_cast< std::int32_t >( k );
            b.cut = now;
            b.cut_set = true;
        }
    }
    if ( !over )
        return;
    b.finishing = true;
    std::vector< float > sorted = b.frames;
    std::sort( sorted.begin( ), sorted.end( ), std::greater< float >( ) );
    double all = 0.0, slow = 0.0;
    const std::size_t count = sorted.size( ), low = count / 100 ? count / 100 : 1;
    for ( std::size_t k = 0; k < count; ++k )
    {
        all += sorted[ k ];
        if ( k < low )
            slow += sorted[ k ];
    }
    LOG_SUCCESS( "benchmark: %s finished: %zu frames, average %.1f fps, 1%% low %.1f fps", b.tracks[ b.index ].c_str( ), count, all > 0.0 ? count / all : 0.0, slow > 0.0 ? low / slow : 0.0 );
    if ( ++b.index < b.tracks.size( ) )
        m_action->on_restart_action_game( );
    else
        m_action->on_end_action_game( );
}

// action_exit: a Quit ends the run; the last level's end too.
inline void c_world::benchmark_after_exit( )
{
    benchmark_t& b = m_benchmark;
    if ( !b.active )
        return;
    const bool more = b.finishing && b.index < b.tracks.size( );
    b.finishing = false;
    ui_json_erase( m_menu_util, "GameResults" );
    if ( more )
        return;
    LOG_INFO( "benchmark: run over at level %zu of %zu", b.index, b.tracks.size( ) );
    ui_json_child( m_menu_util, "GameConfig" ) = b.game_config;
    ui_json_child( m_menu_util, "BoatConfig" ) = b.boat_config;
    ui_json_set_string( ui_json_child( m_menu_util, "InitialScreen" ), "Screens/Main" );
    b.active = false;
}
