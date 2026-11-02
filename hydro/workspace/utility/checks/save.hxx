#pragma once

#include <workspace/core/save/profile.hxx>
#include <workspace/core/save/seam.hxx>
#include <workspace/core/save/writer.hxx>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

// The save path against retail's own code (boat batch 32). No check here can touch the real save folder: the self-test
// flavour's seam has no default root, and save_check_dir_t points it at a fresh temporary directory, removed after.
struct save_check_dir_t
{
    std::filesystem::path path;

    save_check_dir_t( )
    {
        const auto stamp = std::chrono::steady_clock::now( ).time_since_epoch( ).count( );
        path = std::filesystem::temp_directory_path( ) / ( "hydro_save_check_" + std::to_string( stamp ) );
        std::filesystem::create_directories( path );
        save_seam::redirect( path );
    }

    ~save_check_dir_t( )
    {
        save_seam::redirect( { } );
        std::error_code ignored;
        std::filesystem::remove_all( path, ignored );
    }

    void reset( ) const
    {
        std::error_code ignored;
        std::filesystem::remove_all( path, ignored );
        std::filesystem::create_directories( path );
    }

    bool get( const char* name, std::string& out ) const
    {
        std::ifstream file( path / name, std::ios::binary );
        if ( !file )
            return false;
        out.assign( std::istreambuf_iterator< char >( file ), { } );
        return true;
    }

    void put( const char* name, const std::string& bytes ) const
    {
        std::ofstream file( path / name, std::ios::binary );
        file.write( bytes.data( ), static_cast< std::streamsize >( bytes.size( ) ) );
    }
};

// One case's flat word stream (package.hxx is included before logic.hxx's reader).
struct save_words_t
{
    const unsigned* s;

    unsigned next( ) { return *s++; }

    std::string str( )
    {
        const unsigned n = next( );
        std::string out( reinterpret_cast< const char* >( s ), n );
        s += ( n + 3 ) / 4;
        return out;
    }
};

// writer.hxx against retail's 0x55BDA0 and 0x55BEA0 (case save_vujb) on documents c_reader read: the size, then per
// budget the answer, the bytes left and the bytes written.
inline bool verify_save_vujb( )
{
    save_words_t c{ g_golden_save_vujb_words };
    const unsigned n = c.next( );
    int bad = 0, runs = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        const std::string doc = c.str( );
        const std::vector< std::uint8_t > blob( doc.begin( ), doc.end( ) );
        value_t root;
        c_reader::read_at_start( blob, root );
        const unsigned size = c.next( ), count = c.next( );
        if ( vujb_size( root ) != size && bad++ < 6 )
            LOG_ERROR( "save_vujb document %u: size %u, retail %u", v, vujb_size( root ), size );
        for ( unsigned r = 0; r < count; ++r, ++runs )
        {
            const unsigned budget = c.next( ), ok = c.next( ), left = c.next( ), hash = c.next( );
            const std::string want = c.str( );
            std::vector< std::uint8_t > out( size + 16u, 0x55 );
            vujb_cursor_t w{ out.data( ), budget };
            const bool got = vujb_write( w, root );
            const std::uint32_t used = budget - w.left;
            const bool same_bytes = want.empty( ) || ( want.size( ) == used && std::memcmp( want.data( ), out.data( ), used ) == 0 );
            if ( ( static_cast< unsigned >( got ) != ok || w.left != left || save_hash( out.data( ), used ) != hash || !same_bytes ) && bad++ < 6 )
                LOG_ERROR( "save_vujb document %u budget %u: ok %d / %u, left %u / %u", v, budget, got, ok, w.left, left );
        }
    }
    if ( bad == 0 )
        LOG_SUCCESS( "vujb_write / vujb_size match retail's writer 0x55BEA0 / 0x55BDA0: %u documents, %d runs", n, runs );
    else
        LOG_ERROR( "save_vujb FAILED %d", bad );
    return bad == 0;
}

// profile.hxx through the real seam, on a temporary directory, against retail's 0x554CD0 0x554B50 0x554850 0x554A30
// 0x5547F0 on a fake file system (case save_profile): the answer, the document, the file's bytes. Retail's short-write
// vectors have no way in through a real file: skipped and counted.
inline bool verify_save_profile( )
{
    save_words_t c{ g_golden_save_profile_words };
    const unsigned n = c.next( );
    save_check_dir_t dir;
    int bad = 0, skipped = 0;
    const auto parsed = []( const std::string& bytes ) {
        value_t root;
        c_reader::read_at_start( std::vector< std::uint8_t >( bytes.begin( ), bytes.end( ) ), root );
        return root;
    };
    // The write / store vectors' documents: the file retail wrote holds them after its 16-byte header.
    for ( unsigned v = 0; v < n; ++v )
    {
        const unsigned op = c.next( ), ret = c.next( ), events = c.next( );
        const bool had = c.next( ) != 0;
        const std::string before = c.str( );
        const bool has = c.next( ) != 0;
        const std::string after = c.str( ), dump = c.str( );
        dir.reset( );
        if ( had )
            dir.put( "profile", before );
        std::string got_dump;
        unsigned got = ret, got_events = events;
        if ( op == 2 )
        {
            ++skipped;
            continue;
        }
        value_t document;
        if ( op == 0 || op == 1 || op == 5 || op == 6 )
        {
            // The document comes from the dump's own source: for 1 the failed open needs only the answer.
            if ( has )
                document = parsed( after.substr( 16 ) );
            else
            {
                // A directory named "profile": the open for write fails, as the fake's deny did.
                std::filesystem::create_directories( dir.path / "profile" );
                document = value_t{ };
            }
        }
        if ( op == 0 || op == 1 )
            got = profile_write( "profile", document ) ? 1u : 0u;
        else if ( op == 3 )
        {
            json_set_int( document, 5 );
            got = static_cast< unsigned >( profile_read( "profile", document ) );
        }
        else if ( op == 4 )
        {
            c_profile_manager manager;
            json_set_int( manager.data, 5 );
            manager.status = 7;
            manager.load( );
            got = static_cast< unsigned >( manager.status );
            document = manager.data;
        }
        else if ( op == 5 || op == 6 )
        {
            c_profile_manager manager;
            manager.data = document;
            got_events = 0;
            if ( op == 5 )
                manager.store( );
            else
                manager.save( [ & ]( ) { ++got_events; } );
        }
        std::string got_file;
        const bool got_has = !std::filesystem::is_directory( dir.path / "profile" ) && dir.get( "profile", got_file );
        vujb_dump( document, got_dump );
        const bool same_dump = op < 3 || op > 4 || got_dump == dump;
        if ( ( got != ret || got_events != events || got_has != has || got_file != after || !same_dump ) && bad++ < 6 )
            LOG_ERROR( "save_profile vector %u op %u: answer %u / %u, file %zu / %zu bytes, dump %s", v, op, got, ret, got_file.size( ),
                       after.size( ), same_dump ? "ok" : "differs" );
    }
    if ( bad == 0 )
        LOG_SUCCESS( "profile file matches retail's 0x554CD0 0x554B50 0x554850 0x554A30 0x5547F0 through the seam: %u vectors, %d short-write vectors not reachable", n, skipped );
    else
        LOG_ERROR( "save_profile FAILED %d", bad );
    return bad == 0;
}

inline bool verify_save_all( )
{
    bool ok = verify_save_vujb( );
    ok = verify_save_profile( ) && ok;
    return ok;
}
