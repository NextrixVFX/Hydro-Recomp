#pragma once

#include <workspace/core/assets/archive.hxx>
#include <workspace/core/assets/tree.hxx>
#include <workspace/core/ui/config.hxx>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <impl/inflate/puff.h>
#include <fstream>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// THE SAVE SEAM (BRIEF "Saves decision"): c_save_files is the ONE place a save touches the disk. A later backend (online saves,
// a local fallback) replaces this class and nothing above it. Tests point it at a temp folder (set_root): never the user's.
// Local only: `%APPDATA%\HydroThunderHurricane\`; the retail file host g 0x75908C's slots it stands for are named per method.
class c_save_files
{
public:
    c_save_files( ) : m_root( default_root( ) ) { }
    explicit c_save_files( std::filesystem::path root ) : m_root( std::move( root ) ) { }

    static std::filesystem::path default_root( )
    {
        char* app = nullptr;
        std::size_t length = 0;
        std::filesystem::path out;
        if ( _dupenv_s( &app, &length, "APPDATA" ) == 0 && app )
            out = std::filesystem::path( app ) / "HydroThunderHurricane";
        std::free( app );
        return out;
    }
    void set_root( std::filesystem::path root ) { m_root = std::move( root ); }
    const std::filesystem::path& root( ) const { return m_root; }

    // 0x55B260: vt+0x34( name, 0 ) open, vt+0x2C size, vt+0x40 read, vt+0x3C close. False: no such file, or a short read.
    bool read( const std::string& name, std::vector< std::uint8_t >& bytes ) const
    {
        std::ifstream in( m_root / name, std::ios::binary | std::ios::ate );
        if ( !in )
            return false;
        const std::streamoff size = in.tellg( );
        in.seekg( 0 );
        bytes.resize( static_cast< std::size_t >( size ) );
        in.read( reinterpret_cast< char* >( bytes.data( ) ), size );
        return in.good( ) || size == 0;
    }
    // 0x4AC6C0's vt+0x34( name, 1 ) open, vt+0x44 write, vt+0x3C close. False: it could not open (retail then writes nothing).
    bool write( const std::string& name, const std::vector< std::uint8_t >& bytes ) const
    {
        std::error_code ignored;
        std::filesystem::create_directories( m_root, ignored );
        std::ofstream out( m_root / name, std::ios::binary | std::ios::trunc );
        if ( !out )
            return false;
        out.write( reinterpret_cast< const char* >( bytes.data( ) ), static_cast< std::streamsize >( bytes.size( ) ) );
        return out.good( );
    }

private:
    std::filesystem::path m_root;
};

// The VUJB writer: VuJsonBinaryWriter, transliterated from the disassembly (tools tmp/b30/d_55BDA0.txt). Big-endian.
// 0x55BDA0: a node's bytes. Tag 1 / 2: 8, 3: 5, 4: 8 + the text, 5: 8 + the elements, 6: 8 + per key 4 + its text + the value, 7: 12, else 4.
inline std::size_t ui_vujb_size( const value_t& v )
{
    switch ( v.tag )
    {
        case 1:
        case 2:
            return 8;
        case 3:
            return 5;
        case 4:
            return 8 + v.text.size( );
        case 5:
        {
            std::size_t n = 8;
            for ( const value_t& e : v.list )
                n += ui_vujb_size( e );
            return n;
        }
        case 6:
        {
            std::size_t n = 8;
            for ( const auto& field : v.object )
                n += 4 + field.first.size( ) + ui_vujb_size( field.second );
            return n;
        }
        case 7:
            return 12;
        default:
            return 4;
    }
}

inline void ui_vujb_word( std::vector< std::uint8_t >& out, std::uint32_t v )   // 0x55C100
{
    out.push_back( static_cast< std::uint8_t >( v >> 24 ) );
    out.push_back( static_cast< std::uint8_t >( v >> 16 ) );
    out.push_back( static_cast< std::uint8_t >( v >> 8 ) );
    out.push_back( static_cast< std::uint8_t >( v ) );
}

inline void ui_vujb_text( std::vector< std::uint8_t >& out, const std::string& s )   // 0x55C070
{
    ui_vujb_word( out, static_cast< std::uint32_t >( s.size( ) ) );
    out.insert( out.end( ), s.begin( ), s.end( ) );
}

// 0x55BEA0: the tag, then the payload; a map's keys in std::map order (0x43CEE0's walk). 0x55C140 a 64-bit word, big-endian.
inline void ui_vujb_write( std::vector< std::uint8_t >& out, const value_t& v )
{
    ui_vujb_word( out, static_cast< std::uint32_t >( v.tag ) );
    switch ( v.tag )
    {
        case 1:
            ui_vujb_word( out, static_cast< std::uint32_t >( v.integer ) );
            break;
        case 2:
        {
            std::uint32_t bits;
            std::memcpy( &bits, &v.real, 4 );
            ui_vujb_word( out, bits );
            break;
        }
        case 3:
            out.push_back( static_cast< std::uint8_t >( v.integer ) );
            break;
        case 4:
            ui_vujb_text( out, v.text );
            break;
        case 5:
            ui_vujb_word( out, static_cast< std::uint32_t >( v.list.size( ) ) );
            for ( const value_t& e : v.list )
                ui_vujb_write( out, e );
            break;
        case 6:
        {
            ui_vujb_word( out, static_cast< std::uint32_t >( v.object.size( ) ) );
            std::vector< const std::pair< std::string, value_t >* > sorted;
            for ( const auto& field : v.object )
                sorted.push_back( &field );
            std::sort( sorted.begin( ), sorted.end( ), [ ]( const auto* a, const auto* b ) { return a->first < b->first; } );
            for ( const auto* field : sorted )
            {
                ui_vujb_text( out, field->first );
                ui_vujb_write( out, field->second );
            }
            break;
        }
        case 7:
        {
            const std::uint64_t bits = static_cast< std::uint64_t >( v.int64 );
            ui_vujb_word( out, static_cast< std::uint32_t >( bits >> 32 ) );
            ui_vujb_word( out, static_cast< std::uint32_t >( bits ) );
            break;
        }
        default:
            break;
    }
}

// 0x4ABB40: FNV-1a over the bytes.
inline std::uint32_t ui_save_hash( const std::uint8_t* data, std::size_t size )
{
    std::uint32_t h = 0x811C9DC5u;
    for ( std::size_t i = 0; i < size; ++i )
        h = ( h ^ data[ i ] ) * 0x1000193u;
    return h;
}

// A zlib stream of stored blocks. SUBSTITUTION for 0x5BD730 (zlib 1.2.7's compress, level 6): the file's bytes differ from
// retail's and it is larger; case save_file proves retail's loader 0x4AC930 reads this form. Retail never writes one.
inline std::vector< std::uint8_t > ui_save_deflate( const std::vector< std::uint8_t >& raw )
{
    std::vector< std::uint8_t > out = { 0x78u, 0x9Cu };
    std::size_t at = 0;
    do
    {
        const std::size_t n = std::min< std::size_t >( raw.size( ) - at, 0xFFFFu );
        const bool last = at + n == raw.size( );
        out.push_back( last ? 1u : 0u );
        out.push_back( static_cast< std::uint8_t >( n ) );
        out.push_back( static_cast< std::uint8_t >( n >> 8 ) );
        out.push_back( static_cast< std::uint8_t >( ~n ) );
        out.push_back( static_cast< std::uint8_t >( ~n >> 8 ) );
        out.insert( out.end( ), raw.begin( ) + static_cast< std::ptrdiff_t >( at ), raw.begin( ) + static_cast< std::ptrdiff_t >( at + n ) );
        at += n;
    } while ( at < raw.size( ) );
    std::uint32_t a = 1, b = 0;
    for ( const std::uint8_t c : raw )
    {
        a = ( a + c ) % 65521u;
        b = ( b + a ) % 65521u;
    }
    ui_vujb_word( out, b << 16 | a );
    return out;
}

// 0x4ACAB0: the header { "SCUV", 2, the body's size, 0x4ABB40 of the body }, the body = the deflated "VUJB" + 0x1000000 + the document.
inline std::vector< std::uint8_t > ui_save_encode( const value_t& document )
{
    std::vector< std::uint8_t > raw = { 'V', 'U', 'J', 'B', 0, 0, 0, 1 };
    raw.reserve( ui_vujb_size( document ) + 8 );
    ui_vujb_write( raw, document );
    const std::vector< std::uint8_t > body = ui_save_deflate( raw );
    std::vector< std::uint8_t > out( 16 );
    const std::uint32_t head[ 4 ] = { 0x56554353u, 2u, static_cast< std::uint32_t >( body.size( ) ), ui_save_hash( body.data( ), body.size( ) ) };
    std::memcpy( out.data( ), head, 16 );
    out.insert( out.end( ), body.begin( ), body.end( ) );
    return out;
}

// 0x5BD7E0 (zlib's uncompress) into at most 0x40000 bytes. SUBSTITUTION: puff + the header and adler-32 tests zlib makes (as archive.hxx).
inline bool ui_save_inflate( const std::vector< std::uint8_t >& in, std::vector< std::uint8_t >& out )
{
    if ( in.size( ) < 6 )
        return false;
    const unsigned header = static_cast< unsigned >( in[ 0 ] ) * 256u + in[ 1 ];
    if ( ( in[ 0 ] & 0x0F ) != 8 || ( in[ 0 ] >> 4 ) > 7 || ( header % 31u ) != 0 || ( in[ 1 ] & 0x20 ) != 0 )
        return false;
    out.resize( 0x40000u );
    unsigned long dest = 0x40000ul, source = static_cast< unsigned long >( in.size( ) - 2 );
    if ( puff( out.data( ), &dest, in.data( ) + 2, &source ) != 0 )
        return false;
    out.resize( dest );
    const std::size_t tail = 2 + source;
    if ( tail + 4 > in.size( ) )
        return false;
    std::uint32_t a = 1, b = 0;
    for ( const std::uint8_t c : out )
    {
        a = ( a + c ) % 65521u;
        b = ( b + a ) % 65521u;
    }
    const std::uint32_t want = static_cast< std::uint32_t >( in[ tail ] ) << 24 | static_cast< std::uint32_t >( in[ tail + 1 ] ) << 16 | static_cast< std::uint32_t >( in[ tail + 2 ] ) << 8 | in[ tail + 3 ];
    return want == ( b << 16 | a );
}

// 0x4AC930: false for a short file, a wrong magic / version / size / hash, a body that does not inflate into the static
// 0x40000 bytes (0x759898), a document 0x55B560 refuses. SUBSTITUTION: archive.hxx's puff for zlib's inflate.
inline bool ui_save_decode( const std::vector< std::uint8_t >& file, value_t& document )
{
    if ( file.size( ) < 16 )
        return false;
    std::uint32_t head[ 4 ];
    std::memcpy( head, file.data( ), 16 );
    if ( head[ 0 ] != 0x56554353u || head[ 1 ] != 2u || head[ 2 ] != file.size( ) - 16u || head[ 3 ] != ui_save_hash( file.data( ) + 16, file.size( ) - 16 ) )
        return false;
    const std::vector< std::uint8_t > stored( file.begin( ) + 16, file.end( ) );
    std::vector< std::uint8_t > raw;
    if ( !ui_save_inflate( stored, raw ) )
        return false;
    if ( raw.size( ) < 8 || std::memcmp( raw.data( ), "VUJB", 4 ) != 0 )
        return false;
    value_t parsed = c_reader( raw ).parse( );
    document = std::move( parsed );
    return true;
}

// VuCloudSaveManager (vtable 0x745358, g 0x758FA0) without the cloud: +0x10 the file's last bytes, +0x20 the document (the whole
// save: Progress, Unlock, TotalCredits, ... every manager's container is inside it), +0x50 the load's status, +0x54 the name.
// The cloud half (0x4ABDC0's "%llu_%s", the conflict 0x4ABE80, 0x4AC290, 0x558300's upload through g 0x758FB4) is a hook that
// answers "not connected": not recreated.
struct ui_save_hooks_t
{
    std::function< void( const char* event ) > m_broadcast;                 // 0x505690 ( "OnCloudSavePreSave", "OnCloudSaveDataChanged" )
    std::function< void( const char* heading, const char* body, const char* ok ) > m_message;   // 0x4B1430 ( the corrupt save's box )
};

class c_save_manager
{
public:
    c_save_manager( c_save_files& files, ui_save_hooks_t hooks ) : m_files( files ), m_hooks( std::move( hooks ) ) { }

    // 0x4ABE70 / 0x4AC480: the name, the document cleared, 0x4AC5C0; "OnCloudSaveDataChanged"; a corrupt file (status 2): the box, status 0.
    void open( const std::string& name )
    {
        m_name = name;
        ui_json_clear( m_document );
        load( );
        if ( m_hooks.m_broadcast )
            m_hooks.m_broadcast( "OnCloudSaveDataChanged" );
        if ( m_status == 2 )
        {
            if ( m_hooks.m_message )
                m_hooks.m_message( "SaveGame_Corrupt_Heading", "SaveGame_Corrupt_Body", "MessageBox_OK" );
            m_status = 0;
        }
    }
    // 0x4ABD60: "OnCloudSavePreSave", 0x4AC6C0.
    void request( )
    {
        if ( m_hooks.m_broadcast )
            m_hooks.m_broadcast( "OnCloudSavePreSave" );
        save( );
    }
    // 0x4AC6C0: encoded; the same bytes as the last: nothing. Else kept and written.
    void save( )
    {
        std::vector< std::uint8_t > encoded = ui_save_encode( m_document );
        if ( encoded == m_blob )
            return;
        m_blob = std::move( encoded );
        m_files.write( m_name, m_blob );
    }

    value_t& document( ) { return m_document; }
    std::int32_t status( ) const { return m_status; }

private:
    // 0x4AC5C0: no file 1, a bad one 2, else 0 (the document holds what 0x55B560 read before a failure in retail: only a whole read is taken).
    void load( )
    {
        m_blob.clear( );
        if ( !m_files.read( m_name, m_blob ) )
        {
            m_status = 1;
            return;
        }
        m_status = ui_save_decode( m_blob, m_document ) ? 0 : 2;
    }

    c_save_files& m_files;
    ui_save_hooks_t m_hooks;
    std::string m_name;
    std::vector< std::uint8_t > m_blob;
    value_t m_document;
    std::int32_t m_status = 0;
};
