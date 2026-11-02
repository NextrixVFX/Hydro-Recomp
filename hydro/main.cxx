#include <impl/includes.hxx>

// Ours (a measurement build only, off by default): operator new counted for --spike-log (utility/spikes.hxx).
#if defined( HYDRO_SPIKE_ALLOC )
#include <new>
static inline void spike_alloc( std::size_t n )
{
    spikes::state_t& s = spikes::g_state;
    if ( !s.on )
        return;
    ++s.now.count[ spikes::c_allocs ];
    s.now.count[ spikes::c_alloc_bytes ] += static_cast< std::uint32_t >( n );
    if ( n >= 65536u )
        ++s.now.count[ spikes::c_alloc_big ];
    if ( s.current < spikes::k_slots )
        ++s.now.allocs[ s.current ];
}
void* operator new( std::size_t n ) { spike_alloc( n ); void* p = std::malloc( n ? n : 1 ); if ( !p ) throw std::bad_alloc( ); return p; }
void* operator new[]( std::size_t n ) { spike_alloc( n ); void* p = std::malloc( n ? n : 1 ); if ( !p ) throw std::bad_alloc( ); return p; }
void operator delete( void* p ) noexcept { std::free( p ); }
void operator delete[]( void* p ) noexcept { std::free( p ); }
void operator delete( void* p, std::size_t ) noexcept { std::free( p ); }
void operator delete[]( void* p, std::size_t ) noexcept { std::free( p ); }
#endif

int main( int argc, char** argv )
{
    c_engine::arguments( argc, argv );
    // --verbose turns on LOG_VERBOSE. Nothing set this before, so every
    // verbose diagnostic in the tree - the entity census, the authored gfx
    // settings dump, the rotator and attach reports - was unreachable.
    g_verbose = c_engine::option( "verbose" ) != nullptr;
    // --selftest checks our maths against vectors captured by running the
    // retail functions, then exits. No window, no device, no assets.
    //
    // Both halves run before either verdict is used: short-circuiting would
    // hide a game-side failure behind an engine-side one, and a self-test that
    // reports less than it measured is worse than none.
    if ( c_engine::option( "selftest" ) != nullptr )
    {
#if HYDRO_GOLDEN
        const bool engine_ok = verify_all( );
        const bool game_ok = verify_game( );
        return engine_ok && game_ok ? 0 : 1;
#else
        LOG_ERROR( "--selftest needs the oracle's golden vectors (hydro-tools/oracle/golden.py); this build has none" );
        return 2;
#endif
    }
    c_engine::guard( );
    c_game game{};
    c_engine::attach( game );
    return c_engine::run( "Hydro" );
}
