#pragma once

#include <workspace/utility/spikes.hxx>

#include <workspace/utility/logger.hxx>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

extern "C"
{
#include <impl/inflate/puff.h>
}

#ifdef NIL
#  undef NIL
#endif

// PC Base.apf (FPUV, version 3). Header +8 is the directory offset and
// header +12 is the record count. Each record is a NUL-terminated name,
// then offset, uncompressed size, compressed size, type, crc32, and a
// compressed flag. A set flag means the payload is a zlib stream.
class c_archive
{
    struct entry_t
    {
        std::string name;
        std::uint32_t offset = 0;
        std::uint32_t raw = 0;
        std::uint32_t compressed = 0;
        std::uint32_t kind = 0;
        std::uint32_t crc = 0;
        bool packed = false;
    };

    mutable std::unordered_map< std::string, std::vector< std::uint8_t > > m_cache;
    mutable std::size_t m_cache_bytes = 0, m_cache_peak = 0;
    mutable int m_cache_depth = 0, m_late = 0;
    mutable bool m_cache_off = false, m_watch_late = false;

public:
    bool open( const char* path )
    {
        m_entries.clear( );
        m_path.clear( );
        if ( path == nullptr || path[ 0 ] == '\0' )
            return false;

        std::ifstream file( path, std::ios::binary );
        if ( !file )
        {
            LOG_ERROR( "pack did not open: %s", path );
            return false;
        }

        unsigned char header[ 32 ]{};
        file.read( reinterpret_cast< char* >( header ), sizeof( header ) );
        if ( !file )
            return false;

        const std::uint32_t magic = read_u32( header );
        const std::uint32_t directory = read_u32( header + 8 );
        const std::uint32_t listed = read_u32( header + 12 );
        const std::uint32_t directory_bytes = read_u32( header + 16 );
        if ( magic != 0x56555046u || directory_bytes == 0 || directory_bytes > 16u * 1024u * 1024u )
        {
            LOG_ERROR( "pack header is not a VuPF archive" );
            return false;
        }

        std::vector< unsigned char > table( directory_bytes );
        file.seekg( directory );
        file.read( reinterpret_cast< char* >( table.data( ) ), static_cast< std::streamsize >( table.size( ) ) );
        if ( !file )
        {
            LOG_ERROR( "pack directory did not read" );
            return false;
        }

        std::size_t cursor = 0;
        while ( cursor < table.size( ) )
        {
            std::size_t end = cursor;
            while ( end < table.size( ) && table[ end ] != 0 )
                ++end;
            if ( end == cursor || end >= table.size( ) || end + 1 + 24 > table.size( ) )
                break;

            entry_t entry{};
            entry.name.assign( reinterpret_cast< const char* >( table.data( ) + cursor ), end - cursor );
            cursor = end + 1;
            entry.offset = read_u32( table.data( ) + cursor );
            entry.raw = read_u32( table.data( ) + cursor + 4 );
            entry.compressed = read_u32( table.data( ) + cursor + 8 );
            entry.kind = read_u32( table.data( ) + cursor + 12 );
            entry.crc = read_u32( table.data( ) + cursor + 16 );
            entry.packed = read_u32( table.data( ) + cursor + 20 ) != 0;
            cursor += 24;
            m_entries.push_back( std::move( entry ) );
        }

        if ( m_entries.empty( ) || m_entries.front( ).name != "Assets/AssetData" || m_entries.front( ).offset != 0x44u )
        {
            LOG_ERROR( "pack directory did not start at Assets/AssetData" );
            m_entries.clear( );
            return false;
        }

        if ( m_entries.size( ) != listed )
            LOG_WARNING( "pack listed %u assets and the directory has %u", listed, static_cast< unsigned >( m_entries.size( ) ) );

        m_path = path;
        LOG_VERBOSE( "pack %u assets", static_cast< unsigned >( m_entries.size( ) ) );
        return true;
    }

    bool read( const char* name, std::vector< std::uint8_t >& out ) const
    {
        const spikes::scope_t spike( spikes::k_read );
        const double spike_start = spikes::g_state.on ? trace::now( ) : 0.0;
        out.clear( );
        const entry_t* entry = find( name );
        if ( entry == nullptr )
        {
            LOG_ERROR( "asset missing: %s", name != nullptr ? name : "" );
            return false;
        }
        if ( entry->raw == 0 )
            return false;

        const std::uint32_t stored = entry->packed ? entry->compressed : entry->raw;
        if ( stored < 8 )
            return false;

        // Ours, not retail's (a platform-layer cost, frame spikes fix 2): one inflate per asset. Each caller gets its own
        // copy, so a caller that edits its bytes cannot reach another's.
        const auto cached = m_cache.find( name );
        if ( cached != m_cache.end( ) )
        {
            out = cached->second;
            return true;
        }
        if ( !inflate_entry( *entry, stored, name, out ) )
            return false;
        spikes::note( name, stored, trace::now( ) - spike_start );

        // Ours: a read during a race is a hitch the load's prewarm missed (world/prewarm.hxx). The goal is 0.
        if ( m_watch_late )
        {
            ++m_late;
            LOG_WARNING( "late first use %d: %s", m_late, name );
        }
        if ( !m_cache_off && ( m_cache_depth > 0 || session_kept( name ) ) )
        {
            m_cache_bytes += out.size( );
            m_cache_peak = m_cache_bytes > m_cache_peak ? m_cache_bytes : m_cache_peak;
            m_cache.emplace( name, out );
        }
        return true;
    }

    // Ours: reads between begin and end are held; end drops all but what session_kept names. Nests.
    void cache_begin( ) const { ++m_cache_depth; }
    void cache_end( ) const
    {
        if ( m_cache_depth == 0 || --m_cache_depth != 0 )
            return;
        LOG_INFO( "read cache: %u assets, %.1f MB at the load's end, peak %.1f MB", static_cast< unsigned >( m_cache.size( ) ),
            static_cast< double >( m_cache_bytes ) / 1048576.0, static_cast< double >( m_cache_peak ) / 1048576.0 );
        for ( auto it = m_cache.begin( ); it != m_cache.end( ); )
        {
            if ( session_kept( it->first.c_str( ) ) )
                ++it;
            else
            {
                m_cache_bytes -= it->second.size( );
                it = m_cache.erase( it );
            }
        }
    }
    void cache_off( bool off ) const { m_cache_off = off; }   // --no-read-cache (diagnostic, the before of the measure)
    void watch_late( bool on ) const { m_watch_late = on; }
    int late( ) const { return m_late; }
    bool has( const std::string& name ) const { return find( name.c_str( ) ) != nullptr; }

private:
    // The small documents every screen and load reads again.
    static bool session_kept( const char* name )
    {
        return std::strncmp( name, "VuDBAsset/", 10 ) == 0 || std::strcmp( name, "Assets/AssetData" ) == 0;
    }

    bool inflate_entry( const entry_t& record, std::uint32_t stored, const char* name, std::vector< std::uint8_t >& out ) const
    {
        const entry_t* entry = &record;
        spikes::count( spikes::c_reads );
        spikes::count( spikes::c_read_bytes, stored );

        std::ifstream file( m_path, std::ios::binary );
        if ( !file )
            return false;

        std::vector< unsigned char > compressed( stored );
        file.seekg( entry->offset );
        file.read( reinterpret_cast< char* >( compressed.data( ) ), static_cast< std::streamsize >( compressed.size( ) ) );
        if ( !file )
            return false;

        if ( !entry->packed )
        {
            out = std::move( compressed );
            return true;
        }

        const unsigned header = static_cast< unsigned >( compressed[ 0 ] ) * 256u + compressed[ 1 ];
        if ( ( compressed[ 0 ] & 0x0f ) != 8 || ( header % 31u ) != 0 || ( compressed[ 1 ] & 0x20 ) != 0 )
        {
            LOG_ERROR( "asset %s is not a zlib stream", name );
            return false;
        }

        out.resize( entry->raw );
        unsigned long dest_len = entry->raw;
        unsigned long source_len = stored - 6;
        const int error = puff( out.data( ), &dest_len, compressed.data( ) + 2, &source_len );
        if ( error != 0 || dest_len != entry->raw )
        {
            LOG_ERROR( "asset %s inflate failed (%d)", name, error );
            out.clear( );
            return false;
        }

        LOG_VERBOSE( "asset %s  %u bytes", name, static_cast< unsigned >( dest_len ) );
        return true;
    }

public:
    int count( ) const
    {
        return static_cast< int >( m_entries.size( ) );
    }

    // Every asset named "<prefix><name>", as <name>, in directory order.
    std::vector< std::string > names( const std::string& prefix ) const
    {
        std::vector< std::string > out;
        for ( const entry_t& entry : m_entries )
            if ( entry.name.compare( 0, prefix.size( ), prefix ) == 0 )
                out.push_back( entry.name.substr( prefix.size( ) ) );
        return out;
    }

    // ---- The package's pack file, as VuAssetPackFile (boat batch 25): ctor 0x567590, open 0x567760, read 0x5679F0.
    // The handle is the platform file system's (g_vumetrofile vt+0x34 / +0x3C / +0x40 / +0x48): a std::ifstream here.

    // 0x567760 open( path ). A pack whose path is already set (+0x50, the +0x40 string's size) answers false - the
    // path is copied BEFORE the open, so a failed open leaves it set and every later open fails too. The 68-byte
    // header: +0x18 a 32-byte text (the sku, VuString_assign through strlen) and the two words +0x38 / +0x3C are taken
    // before any test; then magic 'VUPF', version 3, the FNV-1a 32 of bytes +0..+0x3F == +0x40; seek +8; the directory
    // (+0x10 bytes) read whole, its FNV-1a == +0x14; then +0xC records (a signed count), each needing 0x20 bytes left:
    // a C string, then offset / size / stored size / version / hash / flags into the directory map (operator[] 0x567EB0:
    // a repeated name overwrites). Any failure closes the handle (+0x58 = 0); records already read stay in the map.
    // `header` replaces the 68 bytes the file holds (the self-test's variants).
    bool open_pack( const char* path, const unsigned char* header = nullptr )
    {
        if ( !m_pack_path.empty( ) )
            return false;
        m_pack_path = path != nullptr ? path : "";
        m_pack_open = false;
        if ( m_pack_path.empty( ) )
            return false;
        std::ifstream file( m_pack_path, std::ios::binary );
        if ( !file )
            return false;
        unsigned char head[ 68 ]{};
        file.read( reinterpret_cast< char* >( head ), sizeof( head ) );
        if ( file.gcount( ) != 68 )
            return false;
        if ( header != nullptr )
            std::memcpy( head, header, sizeof( head ) );
        std::size_t text = 0;
        while ( 0x18 + text < sizeof( head ) && head[ 0x18 + text ] != 0 )
            ++text;
        if ( 0x18 + text == sizeof( head ) )
        {
            // strlen runs past the header into retail's stack frame: not modelled.
            m_pack_unknown = true;
            return false;
        }
        m_pack_sku.assign( reinterpret_cast< const char* >( head + 0x18 ), text );
        m_pack_words[ 0 ] = read_u32( head + 0x38 );
        m_pack_words[ 1 ] = read_u32( head + 0x3C );
        if ( read_u32( head ) != 0x56555046u || read_u32( head + 4 ) != 3u )
            return false;
        if ( fnv( head, 0x40 ) != read_u32( head + 0x40 ) )
            return false;
        file.clear( );
        file.seekg( read_u32( head + 8 ) );
        if ( !file )
            return false;
        std::vector< unsigned char > table( read_u32( head + 0x10 ) );
        if ( !table.empty( ) )
        {
            file.read( reinterpret_cast< char* >( table.data( ) ), static_cast< std::streamsize >( table.size( ) ) );
            if ( static_cast< std::size_t >( file.gcount( ) ) != table.size( ) )
                return false;
        }
        if ( fnv( table.data( ), table.size( ) ) != read_u32( head + 0x14 ) )
            return false;
        const std::int32_t count = static_cast< std::int32_t >( read_u32( head + 0xC ) );
        std::size_t pos = 0;
        for ( std::int32_t index = 0; index < count; ++index )
        {
            if ( pos > table.size( ) )
            {
                // The unsigned size - pos wraps and retail reads past its buffer: not modelled.
                m_pack_unknown = true;
                return false;
            }
            if ( table.size( ) - pos < 0x20 )
                return false;
            std::size_t end = pos;
            while ( end < table.size( ) && table[ end ] != 0 )
                ++end;
            if ( end + 1 + 24 > table.size( ) )
            {
                m_pack_unknown = true;
                return false;
            }
            pack_record_t& r = m_pack_records[ std::string( reinterpret_cast< const char* >( table.data( ) + pos ), end - pos ) ];
            pos = end + 1;
            r.offset = read_u32( table.data( ) + pos );
            r.size = read_u32( table.data( ) + pos + 4 );
            r.stored = read_u32( table.data( ) + pos + 8 );
            r.version = read_u32( table.data( ) + pos + 12 );
            r.hash = read_u32( table.data( ) + pos + 16 );
            r.flags = read_u32( table.data( ) + pos + 20 );
            pos += 24;
        }
        m_pack_open = true;
        return true;
    }

    // 0x5679F0 read( type, name, lang, &version, &hash, &data ): 0 when no path is set (+0x50, the path's size) or
    // "<type>/<name><lang>" is not in the directory (a failed open with its path set still looks the name up; the seek
    // on its closed handle is then the platform's answer, not modelled); 2 when the seek, the read or the inflate fails or the
    // data's FNV-1a differs from the record's hash; 1 with the record's version and hash. Flags bit 0: zlib (0x55B3A0,
    // zlib 1.2.7's inflate in 0x40000-byte reads of the stored size; the data must end the stream and fill the size).
    // SUBSTITUTION: puff + the zlib header / adler-32 checks zlib makes, for zlib's inflate.
    int read_asset( const std::string& type, const std::string& name, const std::string& lang, std::uint32_t& version,
                    std::uint32_t& hash, std::vector< std::uint8_t >& out ) const
    {
        if ( m_pack_path.empty( ) )
            return 0;
        auto it = m_pack_records.find( type + "/" + name + lang );
        if ( it == m_pack_records.end( ) )
            return 0;
        if ( !m_pack_open )
        {
            // The seek on a closed handle (+0x58 = 0) is the platform's answer: not modelled.
            m_pack_unknown = true;
            return 2;
        }
        const pack_record_t& r = it->second;
        std::ifstream file( m_pack_path, std::ios::binary );
        if ( !file )
            return 2;
        file.seekg( r.offset );
        if ( !file )
            return 2;
        out.assign( r.size, 0 );
        if ( ( r.flags & 1 ) != 0 )
        {
            std::vector< unsigned char > stored( r.stored );
            file.read( reinterpret_cast< char* >( stored.data( ) ), static_cast< std::streamsize >( stored.size( ) ) );
            stored.resize( static_cast< std::size_t >( file.gcount( ) ) );
            if ( !inflate_zlib( stored, out ) )
                return 2;
        }
        else if ( !out.empty( ) )
        {
            file.read( reinterpret_cast< char* >( out.data( ) ), static_cast< std::streamsize >( out.size( ) ) );
            if ( static_cast< std::size_t >( file.gcount( ) ) != out.size( ) )
                return 2;
        }
        if ( fnv( out.data( ), out.size( ) ) != r.hash )
            return 2;
        version = r.version;
        hash = r.hash;
        return 1;
    }

    const std::string& pack_sku( ) const
    {
        return m_pack_sku;
    }

    std::uint32_t pack_word( int index ) const
    {
        return m_pack_words[ index & 1 ];
    }

    std::size_t pack_records( ) const
    {
        return m_pack_records.size( );
    }

    // A pack path a recreation does not model was reached (named where it is set).
    bool pack_unknown( ) const
    {
        return m_pack_unknown;
    }

    // 0x4ABB40: FNV-1a 32 over a byte range.
    static std::uint32_t fnv( const unsigned char* bytes, std::size_t size )
    {
        std::uint32_t h = 0x811C9DC5u;
        for ( std::size_t index = 0; index < size; ++index )
            h = ( h ^ bytes[ index ] ) * 16777619u;
        return h;
    }

private:
    struct pack_record_t
    {
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
        std::uint32_t stored = 0;
        std::uint32_t version = 0;
        std::uint32_t hash = 0;
        std::uint32_t flags = 0;
    };

    // zlib's stream as inflate() takes it: CMF method 8 with a window of at most 32K, FCHECK, no preset dictionary,
    // the deflate data, then the big-endian adler-32 of the output. The output must be exactly out.size( ) bytes.
    static bool inflate_zlib( const std::vector< unsigned char >& in, std::vector< std::uint8_t >& out )
    {
        if ( in.size( ) < 2 )
            return false;
        const unsigned header = static_cast< unsigned >( in[ 0 ] ) * 256u + in[ 1 ];
        if ( ( in[ 0 ] & 0x0F ) != 8 || ( in[ 0 ] >> 4 ) > 7 || ( header % 31u ) != 0 || ( in[ 1 ] & 0x20 ) != 0 )
            return false;
        unsigned long dest_len = static_cast< unsigned long >( out.size( ) );
        unsigned long source_len = static_cast< unsigned long >( in.size( ) - 2 );
        unsigned char none = 0;
        if ( puff( out.empty( ) ? &none : out.data( ), &dest_len, in.data( ) + 2, &source_len ) != 0 || dest_len != out.size( ) )
            return false;
        const std::size_t tail = 2 + source_len;
        if ( tail + 4 > in.size( ) )
            return false;
        std::uint32_t a = 1, b = 0;
        for ( std::uint8_t byte : out )
        {
            a = ( a + byte ) % 65521u;
            b = ( b + a ) % 65521u;
        }
        const std::uint32_t want = ( static_cast< std::uint32_t >( in[ tail ] ) << 24 ) | ( static_cast< std::uint32_t >( in[ tail + 1 ] ) << 16 )
            | ( static_cast< std::uint32_t >( in[ tail + 2 ] ) << 8 ) | in[ tail + 3 ];
        return ( ( b << 16 ) | a ) == want;
    }

    std::string m_pack_path;
    std::string m_pack_sku;
    std::uint32_t m_pack_words[ 2 ]{ 0xFFFFFFFFu, 0xFFFFFFFFu };
    std::map< std::string, pack_record_t > m_pack_records;
    bool m_pack_open = false;
    mutable bool m_pack_unknown = false;

    static std::uint32_t read_u32( const unsigned char* bytes )
    {
        return static_cast< std::uint32_t >( bytes[ 0 ] )
            | ( static_cast< std::uint32_t >( bytes[ 1 ] ) << 8 )
            | ( static_cast< std::uint32_t >( bytes[ 2 ] ) << 16 )
            | ( static_cast< std::uint32_t >( bytes[ 3 ] ) << 24 );
    }

    const entry_t* find( const char* name ) const
    {
        if ( name == nullptr )
            return nullptr;
        for ( const entry_t& entry : m_entries )
        {
            if ( entry.name == name )
                return &entry;
        }
        return nullptr;
    }

    std::string m_path;
    std::vector< entry_t > m_entries;
};
