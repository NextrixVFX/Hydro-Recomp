#pragma once

#if defined( _WIN32 )
#include <psapi.h>
#endif

// c_world's prewarm. NOT RETAIL'S PATH: ours, a platform-layer cost (frame spikes fix 1). What a race would otherwise read and
// create at first use is made at the end of the level load instead. Same assets, earlier.
//
// The list comes from the data: the level and BoatDB (all racers, in or out of this race) name pfx assets; every string in those
// that is also the exact name of a texture or a model in the pack is made; then the Basic/* and PostProcess/* programs and the
// water stage's shaders, textures and cubes. BoatDB's hulls are not listed: only this race's grid can draw one, and it has loaded.
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

inline void c_world::prewarm( const std::string& project )
{
    if ( !m_retail || c_engine::option( "no-prewarm" ) != nullptr )
        return;
    // The pfx assets: the first path segment of each string in the level and in BoatDB, as load_named takes it.
    std::set< std::string > names, assets;
    std::vector< std::uint8_t > level;
    if ( m_archive.read( project.c_str( ), level ) )
        prewarm_strings( c_reader( level ).parse( ), names );
    prewarm_strings( m_boat_db, names );
    for ( const std::string& name : names )
    {
        const std::string first = name.substr( 0, name.find( '/' ) );
        if ( m_archive.has( "VuPfxAsset/" + first ) )
            assets.insert( first );
    }
    std::set< std::string > parts;
    for ( const std::string& asset : assets )
    {
        std::vector< std::uint8_t > blob;
        value_t document;
        if ( m_archive.read( ( "VuPfxAsset/" + asset ).c_str( ), blob ) && pfx_asset_document( blob, document ) )
            prewarm_strings( document, parts );
    }
    int textures = 0, models = 0, programs = 0;
    for ( const std::string& part : parts )
    {
        if ( m_archive.has( "VuTextureAsset/" + part ) )
        {
            texture_for( part );
            m_retail->texture( 0u, part );
            ++textures;
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
    double peak_set = 0.0, peak_private = 0.0;
#if defined( _WIN32 )
    PROCESS_MEMORY_COUNTERS counters{ };
    if ( GetProcessMemoryInfo( GetCurrentProcess( ), &counters, sizeof( counters ) ) )
    {
        peak_set = static_cast< double >( counters.PeakWorkingSetSize ) / 1048576.0;
        peak_private = static_cast< double >( counters.PeakPagefileUsage ) / 1048576.0;
    }
#endif
    LOG_INFO( "level load: %.1f ms, peak working set %.1f MB, peak private %.1f MB", trace::now( ) - start, peak_set, peak_private );
}
