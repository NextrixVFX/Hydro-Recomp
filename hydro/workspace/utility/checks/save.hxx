#pragma once

#include <workspace/core/entity/persist.hxx>
#include <workspace/core/save/cloud.hxx>
#include <workspace/core/save/ghost.hxx>
#include <workspace/core/save/profile.hxx>
#include <workspace/core/save/seam.hxx>
#include <workspace/core/save/writer.hxx>
#include <workspace/core/world/ghost.hxx>
#include <workspace/core/world/ghostgame.hxx>

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

// c_input_manager::save_profile against retail's 0x5111B0 (+ 0x5118D0 0x511E50, case save_input): the profile's dump after
// seeded bindings of every type, on documents holding old mappings, non-objects, nothing.
inline bool verify_save_input( )
{
    save_words_t c{ g_golden_save_input_words };
    const auto names = [ & ]( ) {
        std::vector< std::string > out( c.next( ) );
        for ( std::string& n : out )
            n = c.str( );
        return out;
    };
    const std::vector< std::string > axes = names( ), buttons = names( );
    static std::vector< std::string > pad_axes, pad_buttons;
    pad_axes = names( );
    pad_buttons = names( );
    const unsigned n = c.next( );
    value_t document;
    {
        value_t& db = json_member( document, "VuDBAsset" );
        const auto list = []( value_t& out, const std::vector< std::string >& from ) {
            out.tag = 5;
            out.kind = value_t::kind_t::list;
            for ( const std::string& name : from )
            {
                value_t entry;
                json_set_string( json_member( entry, "Name" ), name );
                out.list.push_back( entry );
            }
        };
        list( json_member( db, "Axes" ), axes );
        list( json_member( db, "Buttons" ), buttons );
    }
    c_input_manager manager;
    manager.hooks.axis_name = []( void*, int index ) { return pad_axes[ index ].c_str( ); };
    manager.hooks.button_name = []( void*, int index ) { return pad_buttons[ index ].c_str( ); };
    manager.init( document );
    int bad = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        const std::string doc = c.str( );
        const std::vector< std::uint8_t > blob( doc.begin( ), doc.end( ) );
        value_t profile;
        c_reader::read_at_start( blob, profile );
        auto& ctl = manager.edit_controller( 0 );
        const auto bind = [ & ]( input_binding_t& b ) {
            b.type = static_cast< std::int32_t >( c.next( ) );
            b.index = static_cast< std::int32_t >( c.next( ) );
        };
        for ( std::size_t k = 0; k < axes.size( ); ++k )
        {
            bind( ctl.axes[ k ].pos[ 0 ] );
            bind( ctl.axes[ k ].pos[ 1 ] );
            bind( ctl.axes[ k ].neg[ 0 ] );
            bind( ctl.axes[ k ].neg[ 1 ] );
        }
        for ( std::size_t k = 0; k < buttons.size( ); ++k )
        {
            bind( ctl.buttons[ k ].bind[ 0 ] );
            bind( ctl.buttons[ k ].bind[ 1 ] );
        }
        const std::string want = c.str( );
        manager.save_profile( profile );
        std::string got;
        vujb_dump( profile, got );
        if ( got != want && bad++ < 6 )
            LOG_ERROR( "save_input vector %u differs", v );
    }
    if ( bad == 0 )
        LOG_SUCCESS( "save_profile matches retail's input save writer 0x5111B0 0x5118D0 0x511E50: %u vectors", n );
    else
        LOG_ERROR( "save_input FAILED %d", bad );
    return bad == 0;
}

// save/ghost.hxx against retail (case save_ghost): base64 0x558300 / 0x558440, the load callback 0x485680. Then the round trip
// through the real seam: world/ghost.hxx's blob of retail's record (its ghost_write 0x4AFB90 / ghost_read 0x4AFCE0 / ghost_tick
// 0x4AF9C0 compared with this case's own retail vectors too) stored, loaded back by ghost_load_all, bytes equal to retail's.
inline bool verify_save_ghost( )
{
    save_words_t c{ g_golden_save_ghost_words };
    int bad = 0;
    const auto fail = [ & ]( const char* what, unsigned v ) {
        if ( bad++ < 8 )
            LOG_ERROR( "save_ghost %s vector %u differs", what, v );
    };
    const auto bytes_of = []( const std::string& s ) { return std::vector< std::uint8_t >( s.begin( ), s.end( ) ); };
    const auto sample_text = []( const ghost_data_t& d ) {
        return std::string( reinterpret_cast< const char* >( d.frames.data( ) ), static_cast< std::size_t >( d.count ) * 24 );
    };
    const unsigned nw = c.next( );
    save_check_dir_t dir;
    for ( unsigned v = 0; v < nw; ++v )
    {
        ghost_data_t record;
        record.boat = c.str( );
        record.skin = c.str( );
        record.driver = c.str( );
        const std::string frames = c.str( );
        record.count = static_cast< std::int32_t >( frames.size( ) / 24 );
        record.frames.assign( frames.begin( ), frames.end( ) );
        const std::string want = c.str( );
        std::vector< std::uint8_t > blob;
        ghost_write( record, blob );
        if ( std::string( blob.begin( ), blob.end( ) ) != want )
            fail( "write", v );
        // The round trip: store, load through the seam, the blob reaches add_ghost unchanged.
        dir.reset( );
        if ( !ghost_store_best( blob, 0, "Hydroelectric" ) )
            fail( "store", v );
        ghost_loads_t loads;
        loads.entries.resize( 1 );
        std::string got, condition;
        loads.add_ghost = [ & ]( int, const std::vector< std::uint8_t >& b ) {
            got.assign( b.begin( ), b.end( ) );
            return true;
        };
        loads.condition = [ & ]( const char* name ) { condition = name; };
        ghost_load_all( loads, "Hydroelectric" );
        if ( got != want || condition != "Success" || !loads.entries[ 0 ].loaded )
            fail( "round trip", v );
    }
    {
        // No file yet: retail's 404, a race with no ghost.
        dir.reset( );
        ghost_loads_t loads;
        loads.entries.resize( 1 );
        std::string condition;
        loads.add_ghost = []( int, const std::vector< std::uint8_t >& ) { return true; };
        loads.condition = [ & ]( const char* name ) { condition = name; };
        ghost_load_all( loads, "Hydroelectric" );
        if ( condition != "Success" || loads.entries[ 0 ].loaded )
            fail( "no file", 0 );
    }
    const unsigned nr = c.next( );
    for ( unsigned v = 0; v < nr; ++v )
    {
        const std::string blob = c.str( );
        const bool ret = c.next( ) != 0;
        const std::string boat = c.str( ), skin = c.str( ), driver = c.str( ), samples = c.str( );
        ghost_data_t record;
        record.boat = "old boat";
        record.skin = "old skin";
        record.driver = "old driver";
        ghost_resize( record, 2 );
        const std::vector< std::uint8_t > bytes = bytes_of( blob );
        const bool got = ghost_read( record, bytes.data( ), bytes.size( ) );
        if ( got != ret || record.boat != boat || record.skin != skin || record.driver != driver || sample_text( record ) != samples )
            fail( "read", v );
    }
    const unsigned nb = c.next( );
    for ( unsigned v = 0; v < nb; ++v )
    {
        const unsigned op = c.next( ), ret = c.next( );
        const std::string in = c.str( ), out = c.str( );
        if ( op == 0 )
        {
            if ( save_base64( reinterpret_cast< const std::uint8_t* >( in.data( ) ), static_cast< std::uint32_t >( in.size( ) ) ) != out )
                fail( "base64", v );
        }
        else
        {
            std::vector< std::uint8_t > got;
            const bool ok = save_unbase64( in, got );
            if ( static_cast< unsigned >( ok ) != ret || std::string( got.begin( ), got.end( ) ) != out )
                fail( "unbase64", v );
        }
    }
    const unsigned nt = c.next( );
    int ticks = 0;
    for ( unsigned v = 0; v < nt; ++v )
    {
        ghost_manager_t recorder;
        const auto word = [ & ]( ) {
            const unsigned u = c.next( );
            float f;
            std::memcpy( &f, &u, 4 );
            return f;
        };
        recorder.clock = word( );
        const unsigned count = c.next( );
        for ( unsigned k = 0; k < count; ++k, ++ticks )
        {
            const float dt = word( );
            std::uint8_t boat[ 0x470 ]{}, body[ 0xB0 ]{}, debris[ 0x98 ]{};
            for ( int lane = 0; lane < 3; ++lane )
            {
                const float x = word( );
                std::memcpy( body + 0x80 + 4 * lane, &x, 4 );
            }
            for ( int lane = 0; lane < 3; ++lane )
            {
                const float x = word( );
                std::memcpy( body + 0xA0 + 4 * lane, &x, 4 );
            }
            const float boost = word( );
            std::memcpy( boat + 0x310, &boost, 4 );
            boat[ 0x32C ] = static_cast< std::uint8_t >( c.next( ) );
            boat[ 0x464 ] = static_cast< std::uint8_t >( c.next( ) );
            debris[ 0x94 ] = static_cast< std::uint8_t >( c.next( ) );
            ghost_boat_t source;
            source.boat = boat;
            source.transform = body;
            source.debris = debris;
            source.throttle = word( );
            const unsigned elapsed = c.next( ), clock = c.next( ), samples = c.next( );
            ghost_tick( recorder, source, dt );
            if ( std::memcmp( &recorder.clock, &elapsed, 4 ) != 0 || std::memcmp( &recorder.accumulator, &clock, 4 ) != 0 || static_cast< unsigned >( recorder.record.count ) != samples )
                fail( "tick", v );
        }
        if ( sample_text( recorder.record ) != c.str( ) )
            fail( "samples", v );
    }
    const unsigned nl = c.next( );
    for ( unsigned v = 0; v < nl; ++v )
    {
        const unsigned n = c.next( ), steps = c.next( );
        ghost_loads_t loads;
        loads.entries.resize( n );
        for ( unsigned k = 0; k < steps; ++k )
        {
            const unsigned index = c.next( ), status = c.next( ), answer = c.next( );
            const std::string text = c.str( );
            const unsigned adds = c.next( );
            std::vector< std::pair< unsigned, std::string > > want_adds, got_adds;
            for ( unsigned a = 0; a < adds; ++a )
            {
                const unsigned at = c.next( );
                want_adds.push_back( { at, c.str( ) } );
            }
            std::vector< unsigned > entries;
            for ( unsigned e = 0; e < n * 3; ++e )
                entries.push_back( c.next( ) );
            const unsigned failure = c.next( ), success = c.next( );
            const std::string message = c.str( );
            unsigned got_failure = 0, got_success = 0;
            loads.add_ghost = [ & ]( int at, const std::vector< std::uint8_t >& b ) {
                got_adds.push_back( { static_cast< unsigned >( at ), std::string( b.begin( ), b.end( ) ) } );
                return answer != 0;
            };
            loads.condition = [ & ]( const char* name ) { ( std::strcmp( name, "Failure" ) == 0 ? got_failure : got_success ) = 1; };
            ghost_on_load( loads, static_cast< int >( status ), text, static_cast< int >( index ) );
            bool same = got_adds == want_adds && got_failure == failure && got_success == success && loads.message == message;
            for ( unsigned e = 0; e < n; ++e )
                same = same && static_cast< unsigned >( loads.entries[ e ].loading ) == entries[ e * 3 ] && static_cast< unsigned >( loads.entries[ e ].failed ) == entries[ e * 3 + 1 ] &&
                       static_cast< unsigned >( loads.entries[ e ].loaded ) == entries[ e * 3 + 2 ];
            if ( !same )
                fail( "load", v );
        }
    }
    if ( bad == 0 )
        LOG_SUCCESS( "ghost store matches retail 0x558300 0x558440 0x485680 (and world/ghost.hxx 0x4AFB90 0x4AFCE0 0x4AF9C0), round trip through the seam: %u blobs, %u reads, %u texts, %d ticks, %u load scripts", nw, nr, nb, ticks, nl );
    else
        LOG_ERROR( "save_ghost FAILED %d", bad );
    return bad == 0;
}

// cloud.hxx through the seam against retail's 0x4AC6C0 0x4ABD60 0x4AC5C0 0x4AC480 (case save_cloud): the file's bytes - zlib's
// deflate included - from every document, nothing written when nothing changed; every damaged file's status, document and box.
inline bool verify_save_cloud( )
{
    save_words_t c{ g_golden_save_cloud_words };
    const unsigned n = c.next( );
    save_check_dir_t dir;
    int bad = 0;
    std::size_t bytes = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        const unsigned op = c.next( ), status = c.next( ), events = c.next( ), puts = c.next( ), boxes = c.next( );
        const bool had = c.next( ) != 0;
        const std::string before = c.str( );
        const bool has = c.next( ) != 0;
        const std::string after = c.str( ), doc = c.str( ), blob = c.str( ), dump = c.str( );
        ( void )puts;
        dir.reset( );
        if ( had )
            dir.put( "cloudsave", before );
        c_cloud_save cloud;
        unsigned got_events = 0, got_boxes = 0;
        cloud.hooks.broadcast = [ & ]( const char* ) { ++got_events; };
        cloud.hooks.corrupt_box = [ & ]( ) { ++got_boxes; };
        if ( op <= 3 )
        {
            const std::vector< std::uint8_t > bytes_in( doc.begin( ), doc.end( ) );
            c_reader::read_at_start( bytes_in, cloud.data );
            if ( op == 3 )
                std::filesystem::create_directories( dir.path / "cloudsave" );
            if ( op == 2 )
                cloud.save( );
            else
                cloud.store( );
            if ( op == 1 )
            {
                std::filesystem::remove( dir.path / "cloudsave" );
                cloud.store( );
            }
        }
        else if ( op == 4 )
        {
            json_set_int( cloud.data, 5 );
            cloud.status = 7;
            cloud.load( );
        }
        else
        {
            json_set_int( cloud.data, 5 );
            cloud.status = 7;
            cloud.change_file( "cloudsave" );
        }
        std::string got_file, got_dump;
        const bool got_has = !std::filesystem::is_directory( dir.path / "cloudsave" ) && dir.get( "cloudsave", got_file );
        vujb_dump( cloud.data, got_dump );
        const std::string got_blob( cloud.blob.begin( ), cloud.blob.end( ) );
        // Retail's status word starts at the case's 0 for the store vectors; ours too. The document's dump is compared on loads only.
        const bool same = got_has == has && got_file == after && got_blob == blob && got_events == events && got_boxes == boxes &&
                          ( op < 4 || ( static_cast< unsigned >( cloud.status ) == status && got_dump == dump ) );
        bytes += got_file.size( );
        if ( !same && bad++ < 8 )
            LOG_ERROR( "save_cloud vector %u op %u: file %zu / %zu bytes (%s), +0x10 %s, status %d / %u, dump %s, events %u / %u, boxes %u / %u", v, op, got_file.size( ), after.size( ),
                       got_file == after ? "same" : "differs", got_blob == blob ? "same" : "differs", cloud.status, status, got_dump == dump ? "same" : "differs", got_events, events, got_boxes, boxes );
    }
    if ( bad == 0 )
        LOG_SUCCESS( "cloud save file matches retail's 0x4AC6C0 0x4ACAB0 0x5BD730 0x4AC5C0 0x4AC930 0x4AC480 0x4ABD60 through the seam: %u vectors, %zu bytes of files", n, bytes );
    else
        LOG_ERROR( "save_cloud FAILED %d", bad );
    return bad == 0;
}

// entity/persist.hxx against retail's VuScriptComponent slots 6 0x503EC0 and 7 0x504450 (+ 0x502720 0x504CC0, case save_script).
inline bool verify_save_script( )
{
    save_words_t c{ g_golden_save_script_words };
    const unsigned n = c.next( );
    int bad = 0;
    const auto word_float = [ & ]( ) {
        const unsigned u = c.next( );
        float f;
        std::memcpy( &f, &u, 4 );
        return f;
    };
    for ( unsigned v = 0; v < n; ++v )
    {
        const unsigned slot = c.next( );
        const std::string doc = c.str( );
        persist_component_t s;
        s.plugs.resize( c.next( ) );
        for ( persist_plug_t& p : s.plugs )
        {
            p.name = c.str( );
            p.baseline = static_cast< std::int32_t >( c.next( ) );
            p.links.resize( c.next( ) );
            for ( persist_link_t& l : p.links )
            {
                l.entity = c.str( );
                l.name = c.str( );
            }
        }
        s.refs.resize( c.next( ) );
        for ( persist_ref_t& r : s.refs )
        {
            r.name = c.str( );
            r.bound = c.next( ) != 0;
            r.baseline = c.next( ) != 0;
            r.entity = c.str( );
        }
        s.ref_links.resize( c.next( ) );
        for ( persist_link_t& l : s.ref_links )
        {
            l.entity = c.str( );
            l.name = c.str( );
        }
        s.ref_baseline = static_cast< std::int32_t >( c.next( ) );
        s.x = word_float( );
        s.y = word_float( );
        s.base_x = word_float( );
        s.base_y = word_float( );
        s.base_width = static_cast< std::int32_t >( c.next( ) );
        s.width = static_cast< std::int32_t >( c.next( ) );
        const std::string want = c.str( );
        const std::vector< std::uint8_t > blob( doc.begin( ), doc.end( ) );
        value_t out;
        c_reader::read_at_start( blob, out );
        if ( slot == 6 )
            persist_save_all( s, out );
        else
            persist_save_changes( s, out );
        std::string got;
        vujb_dump( out, got );
        if ( got != want && bad++ < 6 )
            LOG_ERROR( "save_script vector %u (slot %u) differs:\n%s\n%s", v, slot, got.c_str( ), want.c_str( ) );
    }
    if ( bad == 0 )
        LOG_SUCCESS( "persist_save_all / _changes match retail's VuScriptComponent slots 0x503EC0 0x504450 (0x502720 0x504CC0): %u vectors", n );
    else
        LOG_ERROR( "save_script FAILED %d", bad );
    return bad == 0;
}

// world/ghostgame.hxx against retail's VuGhostGame handlers (case ghost_game): 0x486060's table, 0x486660, 0x486880 + 0x486CE0's results and
// achievement, 0x486EC0's progress, PUT, leaderboard and voice. The PUT goes through the real seam: the file's text is retail's.
inline bool verify_ghost_game( )
{
    save_words_t c{ g_golden_ghost_game_words };
    const unsigned n = c.next( );
    save_check_dir_t dir;
    int bad = 0, puts = 0;
    struct boat_image_t
    {
        std::vector< std::uint8_t > bytes;
        race_results_entry_t entry;
    };
    const auto boat = [ & ]( ) {
        boat_image_t b;
        b.bytes.assign( 0x600, 0 );
        b.entry.human = c.next( ) != 0;
        b.entry.local = c.next( ) != 0;
        const auto word = [ & ]( int at ) {
            const unsigned u = c.next( );
            std::memcpy( b.bytes.data( ) + at, &u, 4 );
        };
        word( 0x4B4 );
        word( 0x4B8 );
        word( 0x4CC );
        word( 0x4D4 );
        word( 0x328 );
        b.bytes[ 0x32D ] = static_cast< std::uint8_t >( c.next( ) );
        b.bytes[ 0x32F ] = static_cast< std::uint8_t >( c.next( ) );
        b.bytes[ 0x334 ] = static_cast< std::uint8_t >( c.next( ) );
        b.entry.driver_name = c.str( );
        b.entry.boat_class = c.str( );
        b.entry.boat_name = c.str( );
        return b;
    };
    struct want_log_t
    {
        unsigned tag = 0;
        std::string text, data;
        unsigned a = 0, b = 0;
    };
    const auto logs = [ & ]( ) {
        std::vector< want_log_t > out( c.next( ) );
        for ( want_log_t& e : out )
        {
            e.tag = c.next( );
            if ( e.tag == 2 || e.tag == 6 )
                e.text = c.str( );
            else if ( e.tag == 3 || e.tag == 5 )
            {
                e.text = c.str( );
                e.a = c.next( );
                e.b = c.next( );
            }
            else if ( e.tag == 4 )
            {
                e.a = c.next( );
                e.b = c.next( );
                e.text = c.str( );
                e.data = c.str( );
            }
        }
        return out;
    };
    const auto same = []( const std::vector< want_log_t >& got, const std::vector< want_log_t >& want ) {
        if ( got.size( ) != want.size( ) )
            return false;
        for ( std::size_t k = 0; k < got.size( ); ++k )
            if ( got[ k ].tag != want[ k ].tag || got[ k ].text != want[ k ].text || got[ k ].data != want[ k ].data || got[ k ].a != want[ k ].a || got[ k ].b != want[ k ].b )
                return false;
        return true;
    };
    const char* const root_doc = "VUJB\0\0\0\1\0\0\0\6\0\0\0\1\0\0\0\5Other\0\0\0\1\0\0\0\3";
    const std::vector< std::uint8_t > root_blob( root_doc, root_doc + 33 );
    for ( unsigned v = 0; v < n; ++v )
    {
        ghost_game_t g;
        g.boats = static_cast< std::int32_t >( c.next( ) );
        g.circuit = static_cast< std::uint8_t >( c.next( ) );
        const std::uint64_t xuid = static_cast< std::uint64_t >( c.next( ) ) | static_cast< std::uint64_t >( c.next( ) ) << 32;
        boat_image_t human = boat( );
        g.human = human.bytes.data( );
        g.human_entry = human.entry;
        g.track = "Hydroelectric";
        g.entries.resize( c.next( ) );
        for ( ghost_entry_t& e : g.entries )
        {
            e.name = c.str( );
            e.loaded = c.next( ) != 0;
        }
        const unsigned nr = c.next( );
        std::vector< boat_image_t > images;
        images.reserve( nr );
        for ( unsigned k = 0; k < nr; ++k )
        {
            ghost_game_record_t r;
            r.index = static_cast< int >( c.next( ) );
            r.frames = static_cast< std::int32_t >( c.next( ) );
            const unsigned last = c.next( );
            std::memcpy( &r.last_time, &last, 4 );
            images.push_back( boat( ) );
            r.boat = images.back( ).bytes.data( );
            r.entry = images.back( ).entry;
            g.records.push_back( r );
        }
        ghost_data_t recording;
        recording.boat = c.str( );
        recording.skin = c.str( );
        recording.driver = c.str( );
        const std::string frames = c.str( );
        recording.count = static_cast< std::int32_t >( frames.size( ) / 24 );
        recording.frames.assign( frames.begin( ), frames.end( ) );
        const auto fresh = [ & ]( ) {
            value_t root;
            c_reader::read_at_start( root_blob, root );
            return root;
        };
        const auto check = [ & ]( const char* step, const value_t& root, const std::vector< want_log_t >& got, const std::string& dump, const std::vector< want_log_t >& want ) {
            std::string d;
            vujb_dump( root, d );
            if ( ( d != dump || !same( got, want ) ) && bad++ < 8 )
                LOG_ERROR( "ghost_game vector %u %s: dump %s, hooks %zu / %zu", v, step, d == dump ? "same" : "differs", got.size( ), want.size( ) );
        };
        {
            value_t root = fresh( );
            const std::string dump = c.str( );
            const auto want = logs( );
            ghost_game_table( root, g );
            check( "table", root, { }, dump, want );
        }
        {
            value_t root = fresh( );
            const std::string dump = c.str( );
            const auto want = logs( );
            ghost_game_table( root, g );
            ghost_game_table_erase( root );
            check( "erase", root, { }, dump, want );
        }
        {
            value_t root = fresh( );
            const std::string dump = c.str( );
            const auto want = logs( );
            std::vector< want_log_t > got;
            ghost_game_end( root, g, [ & ]( const char* name ) { got.push_back( { 2, name, { }, 0, 0 } ); } );
            got.push_back( { 0 } );
            got.push_back( { 1 } );
            check( "end", root, got, dump, want );
        }
        {
            value_t root = fresh( );
            const std::string dump = c.str( );
            const auto want = logs( );
            std::vector< want_log_t > got;
            ghost_game_post_t h;
            h.progress = [ & ]( const std::string& track, std::int32_t place, float time ) {
                unsigned bits;
                std::memcpy( &bits, &time, 4 );
                got.push_back( { 3, track, { }, static_cast< unsigned >( place ), bits } );
            };
            h.store = [ & ]( const std::vector< std::uint8_t >& blob ) {
                dir.reset( );
                ++puts;
                ghost_store_best( blob, xuid, g.track );
                std::string text;
                dir.get( ( "titlestorage/" + std::to_string( xuid ) + "/ghost/" + g.track ).c_str( ), text );
                got.push_back( { 4, "ghost/" + g.track, text, static_cast< unsigned >( xuid ), static_cast< unsigned >( xuid >> 32 ) } );
            };
            h.leaderboard = [ & ]( const std::string& name, std::int64_t score ) {
                got.push_back( { 5, name, { }, static_cast< unsigned >( score ), static_cast< unsigned >( score >> 32 ) } );
            };
            h.voice = [ & ]( const char* line ) { got.push_back( { 6, line, { }, 0, 0 } ); };
            ghost_game_post( g, 1, recording, h );
            check( "post", root, got, dump, want );
        }
    }
    if ( bad == 0 )
        LOG_SUCCESS( "ghost game matches retail's 0x486060 0x486660 0x486880 0x486CE0 0x486EC0, PUT through the seam: %u vectors, %d PUTs", n, puts );
    else
        LOG_ERROR( "ghost_game FAILED %d", bad );
    return bad == 0;
}

inline bool verify_save_all( )
{
    bool ok = verify_save_vujb( );
    ok = verify_save_profile( ) && ok;
    ok = verify_save_input( ) && ok;
    ok = verify_save_ghost( ) && ok;
    ok = verify_save_cloud( ) && ok;
    ok = verify_save_script( ) && ok;
    ok = verify_ghost_game( ) && ok;
    return ok;
}
