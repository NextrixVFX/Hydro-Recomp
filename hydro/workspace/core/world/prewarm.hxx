#pragma once

#if defined( _WIN32 )
#include <psapi.h>
#endif

// c_world's prewarm. NOT RETAIL'S PATH: ours, a platform-layer cost (frame spikes fix 1). What a race would otherwise read and
// create at first use is made at the end of the level load instead. Same assets, earlier.
//
// The list comes from the data: the level's VuAssetListAsset, the level's project, every racer's Breakable Mesh Asset, and
// the pfx assets those and BoatDB name. Every string in them that is the exact name of a texture, cube or model in the pack is made;
// then the race HUD's textures, the Basic/* and PostProcess/* programs and the water stage.
// The documents are only read and walked: nothing enters the pfx runtime, and VuRandom is not touched.
// D3D11's Create calls are complete, so no shader gets an offscreen draw here. WebGL will need one (SPIKES.md).

inline void prewarm_strings( const value_t& value, std::set< std::string >& out )
{
    if ( value.kind == value_t::kind_t::text )
        out.insert( value.text );
    for ( const value_t& item : value.list )
        prewarm_strings( item, out );
    for ( const auto& item : value.object )
        prewarm_strings( item.second, out );
}

// BoatDB's debris mesh, all racers: the one boat effect that is a model named outside a pfx asset (read off BoatDB's key paths).
inline void prewarm_key( const value_t& value, const char* key, std::set< std::string >& out )
{
    for ( const value_t& item : value.list )
        prewarm_key( item, key, out );
    for ( const auto& item : value.object )
    {
        if ( item.first == key && item.second.kind == value_t::kind_t::text )
            out.insert( item.second.text );
        prewarm_key( item.second, key, out );
    }
}

inline void c_world::prewarm( const std::string& project )
{
    if ( !m_retail || c_engine::option( "no-prewarm" ) != nullptr )
        return;
    // The pfx assets: the first path segment of each string in the level and in BoatDB, as load_named takes it.
    std::set< std::string > names, assets;
    std::vector< std::uint8_t > level;
    if ( m_archive.read( project.c_str( ), level ) )
        prewarm_strings( c_reader( level ).parse( ), names );
    const std::set< std::string > level_names = names;
    prewarm_strings( m_boat_db, names );
    for ( const std::string& name : names )
    {
        const std::string first = name.substr( 0, name.find( '/' ) );
        if ( m_archive.has( "VuPfxAsset/" + first ) )
            assets.insert( first );
    }
    std::set< std::string > parts = level_names;
    // Retail's list for the level. Its other list, PreloadDB, is not used: every boat's skins and debris cost 150 to 280 MB resident
    // (measured) and the races reached nothing in it that this list misses.
    std::vector< std::uint8_t > listed;
    std::string list;   // the project is Games/<level><mode>
    for ( const std::string& name : m_archive.names( "VuAssetListAsset/" ) )
        if ( name != "PreloadDB" && project.find( name ) != std::string::npos )
            list = name;
    if ( !list.empty( ) && m_archive.read( ( "VuAssetListAsset/" + list ).c_str( ), listed ) )
        prewarm_strings( c_reader( listed ).parse( ), parts );
    LOG_INFO( "prewarm: %s, level list '%s', %d names", project.c_str( ), list.c_str( ), static_cast< int >( parts.size( ) ) );
    prewarm_key( m_boat_db, "Breakable Mesh Asset", parts );
    for ( const std::string& asset : assets )
    {
        std::vector< std::uint8_t > blob;
        value_t document;
        if ( m_archive.read( ( "VuPfxAsset/" + asset ).c_str( ), blob ) && pfx_asset_document( blob, document ) )
            prewarm_strings( document, parts );
    }
    int textures = 0, models = 0, programs = 0;
#if HYDRO_UI_PASS
    // The race HUD's textures and the level's map: the code names these, no document does, so they go by the pack's own names.
    if ( m_ui_pass )
    {
        for ( const std::string& name : m_archive.names( "VuTextureAsset/UI/HUD_" ) )
        {
            m_ui_pass->texture_asset( "UI/HUD_" + name );
            ++textures;
        }
        const std::string map = "Maps/" + list;
        if ( m_archive.has( "VuTextureAsset/" + map ) )
            m_ui_pass->texture_asset( map );
    }
#endif
    for ( const std::string& part : parts )
    {
        if ( m_archive.has( "VuTextureAsset/" + part ) )
        {
            texture_for( part );
            m_retail->texture( 0u, part );
            ++textures;
        }
        if ( m_archive.has( "VuCubeTextureAsset/" + part ) )
        {
            cube_for( part );
            m_retail->texture( 1u, part );
        }
        if ( m_archive.has( "VuStaticModelAsset/" + part ) || m_archive.has( "VuAnimatedModelAsset/" + part ) )
        {
            pfx_parts( part );
            m_retail->model( part );
            ++models;
        }
    }
    for ( const std::string prefix : { std::string( "VuCompiledShaderAsset/Basic/" ), std::string( "VuCompiledShaderAsset/PostProcess/" ) } )
        for ( const std::string& name : m_archive.names( prefix ) )
        {
            m_retail->program_prewarm( prefix + name );
            ++programs;
        }
    m_retail->corona_ready( );
    m_retail->post_ready( );
    water_stage_attach( );
    LOG_INFO( "prewarm: %d pfx assets walked, %d textures, %d models, %d programs", static_cast< int >( assets.size( ) ), textures, models, programs );
}

// The load's cost in the log: the measure's before / after.
inline void c_world::prewarm_report( double start ) const
{
    double peak_set = 0.0, peak_private = 0.0, now_set = 0.0, now_private = 0.0;
#if defined( _WIN32 )
    PROCESS_MEMORY_COUNTERS counters{ };
    if ( GetProcessMemoryInfo( GetCurrentProcess( ), &counters, sizeof( counters ) ) )
    {
        peak_set = static_cast< double >( counters.PeakWorkingSetSize ) / 1048576.0;
        peak_private = static_cast< double >( counters.PeakPagefileUsage ) / 1048576.0;
        now_set = static_cast< double >( counters.WorkingSetSize ) / 1048576.0;
        now_private = static_cast< double >( counters.PagefileUsage ) / 1048576.0;
    }
#endif
    LOG_INFO( "level load: %.1f ms, peak working set %.1f MB, peak private %.1f MB, after it %.1f / %.1f MB", trace::now( ) - start, peak_set, peak_private, now_set, now_private );
}
