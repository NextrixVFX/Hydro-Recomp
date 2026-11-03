#pragma once

// Playback behind sound_hooks_t. The pack's VuAudioBankAsset files are FSB5 IMA ADPCM with FMOD's
// sample cipher (bit-reverse each byte, then XOR the 16-byte key the engine passes to
// FMOD_CREATESOUNDEXINFO::encryptionkey at 0x4DA40A). VuAudioProjectAsset files are FEV version
// 0x00450000; an event path is a sound-def index, and a sound def is a bank sample. The event
// header's volume and distances are the mix. Music streams (VuAudioStreamAsset) are FSB5 CELT.
//
// The mix is a pull of stereo frames. Windows feeds it to the shared-mode device. The same pull is
// what a Linux, macOS or browser port would hand to its device.

#include <workspace/core/assets/archive.hxx>
#include <workspace/core/assets/tree.hxx>
#include <workspace/core/world/celt_fsb.hxx>
#include <workspace/core/world/sound.hxx>
#include <workspace/utility/logger.hxx>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <objbase.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma comment( lib, "ole32.lib" )

struct platform_audio_t
{
    ~platform_audio_t( )
    {
        m_run.store( false );
        if ( m_thread.joinable( ) )
            m_thread.join( );
    }

    int get_event( c_archive& archive, const char* name, std::uint32_t* event, const void* holder )
    {
        if ( event == nullptr )
            return 1;
        *event = 0;
        if ( name == nullptr || name[ 0 ] == '\0' )
            return 1;
        if ( !ensure( archive ) )
            return 1;
        const auto found = m_events.find( name );
        if ( found == m_events.end( ) || found->second.empty( ) )
        {
            if ( m_misses++ == 0 )
                LOG_WARNING( "audio: no sample for '%s' (further misses counted)", name );
            return 1;
        }
        const wave_t& wave = found->second[ m_pick++ % found->second.size( ) ];
        load_bank( archive, wave.bank );
        const auto sample = m_samples.find( sample_key( wave.bank, wave.index ) );
        if ( sample == m_samples.end( ) )
            return 1;
        voice_t voice;
        voice.id = m_next++;
        if ( m_next == 0 )
            m_next = 1;
        voice.pcm = sample->second.pcm;
        voice.rate = sample->second.rate;
        voice.channels = sample->second.channels;
        voice.frames = sample->second.frames;
        voice.loop = wave.loop;
        voice.volume = wave.volume;
        voice.min_d = wave.min_d;
        voice.max_d = wave.max_d;
        voice.category = wave.category;
        voice.name = found->first;
        voice.holder = holder;
        *event = voice.id;
        std::lock_guard< std::mutex > lock( m_mutex );
        m_voices.push_back( std::move( voice ) );
        return 0;
    }

    void set_callback( std::uint32_t event, std::uint32_t callback, const void* holder )
    {
        std::lock_guard< std::mutex > lock( m_mutex );
        if ( voice_t* voice = find( event ) )
        {
            voice->callback = callback;
            if ( holder != nullptr )
                voice->holder = holder;
        }
    }

    void stop( std::uint32_t event )
    {
        std::lock_guard< std::mutex > lock( m_mutex );
        for ( auto it = m_voices.begin( ); it != m_voices.end( ); ++it )
            if ( it->id == event )
            {
                m_voices.erase( it );
                return;
            }
    }

    void start( std::uint32_t event )
    {
        std::lock_guard< std::mutex > lock( m_mutex );
        if ( voice_t* voice = find( event ) )
        {
            voice->cursor = 0.0;
            voice->playing = true;
            voice->done = false;
        }
    }

    void place( std::uint32_t event, const float* position )
    {
        if ( position == nullptr )
            return;
        std::lock_guard< std::mutex > lock( m_mutex );
        if ( voice_t* voice = find( event ) )
        {
            voice->placed = true;
            voice->x = position[ 0 ];
            voice->y = position[ 1 ];
            voice->z = position[ 2 ];
        }
    }

    void listener( float x, float y, float z )
    {
        std::lock_guard< std::mutex > lock( m_mutex );
        m_lx = x;
        m_ly = y;
        m_lz = z;
    }

    void category( const char* name, float volume )
    {
        if ( name == nullptr )
            return;
        std::lock_guard< std::mutex > lock( m_mutex );
        if ( std::strcmp( name, "ui" ) == 0 )
            m_ui_volume = volume;
        else if ( std::strcmp( name, "music" ) == 0 )
            m_music_volume = volume;
        else
            m_game_volume = volume;
    }

    bool music_on( ) const { return m_music_on; }

    // 0x4D2DE0: if a music channel is already playing, leave it. Otherwise take the next MusicList
    // entry, apply pow(10, Volume * 0.05), and start that stream. Returns the channel volume.
    float music( c_archive& archive, const std::vector< std::pair< std::string, float > >& tracks )
    {
        {
            std::lock_guard< std::mutex > lock( m_mutex );
            for ( const voice_t& voice : m_voices )
                if ( voice.music && voice.playing && !voice.done )
                    return m_music_level;
        }
        if ( tracks.empty( ) )
        {
            if ( !m_music_miss )
            {
                m_music_miss = true;
                LOG_WARNING( "audio: MusicList is empty" );
            }
            return 0.0f;
        }
        const int count = static_cast< int >( tracks.size( ) );
        for ( int n = 0; n < count; ++n )
        {
            const int index = ( m_music_index + n ) % count;
            const std::string& name = tracks[ static_cast< std::size_t >( index ) ].first;
            if ( m_music_failed.find( name ) != m_music_failed.end( ) )
                continue;
            const float db = tracks[ static_cast< std::size_t >( index ) ].second;
            sample_t sample = music_load( archive, name );
            if ( sample.pcm == nullptr )
            {
                m_music_failed.insert( name );
                continue;
            }
            m_music_index = index + 1;
            const float level = std::pow( 10.0f, db * 0.05f );
            voice_t voice;
            voice.id = m_next++;
            if ( m_next == 0 )
                m_next = 1;
            voice.pcm = sample.pcm;
            voice.rate = sample.rate;
            voice.channels = sample.channels;
            voice.frames = sample.frames;
            voice.playing = true;
            voice.music = true;
            voice.volume = level;
            voice.category = 2;
            voice.name = name;
            {
                std::lock_guard< std::mutex > lock( m_mutex );
                m_voices.push_back( std::move( voice ) );
            }
            m_music_level = level;
            m_music_on = true;
            LOG_INFO( "audio: music '%s' at %.1f dB (%.3f)", name.c_str( ), db, level );
            return level;
        }
        if ( !m_music_miss )
        {
            m_music_miss = true;
            LOG_WARNING( "audio: no music stream decoded" );
        }
        return 0.0f;
    }

    void music_gain( float volume )
    {
        std::lock_guard< std::mutex > lock( m_mutex );
        for ( voice_t& voice : m_voices )
            if ( voice.music )
                voice.volume = volume;
    }

    void music_stop( )
    {
        m_music_on = false;
        std::lock_guard< std::mutex > lock( m_mutex );
        for ( auto it = m_voices.begin( ); it != m_voices.end( ); )
        {
            if ( it->music )
                it = m_voices.erase( it );
            else
                ++it;
        }
    }

    // Retire voices the device has finished and tell the holder, the way FMOD's callback type 4 does.
    void pump( const sound_hooks_t& hooks )
    {
        std::vector< const void* > finished;
        {
            std::lock_guard< std::mutex > lock( m_mutex );
            for ( auto it = m_voices.begin( ); it != m_voices.end( ); )
            {
                if ( !it->done )
                {
                    ++it;
                    continue;
                }
                if ( it->callback == g_sound_event_callback && it->holder != nullptr )
                    finished.push_back( it->holder );
                it = m_voices.erase( it );
            }
        }
        for ( const void* holder : finished )
            sound_event_callback( *const_cast< sound_event_t* >( static_cast< const sound_event_t* >( holder ) ), 4u, hooks );
    }

private:
    struct wave_t
    {
        std::string bank;
        int index = 0;
        bool loop = false;
        float volume = 1.0f;
        float min_d = 0.0f;
        float max_d = 0.0f;
        int category = 0;   // 0 game, 1 ui, 2 music
    };
    struct sample_t
    {
        int rate = 0;
        int channels = 0;
        int frames = 0;
        std::shared_ptr< std::vector< std::int16_t > > pcm;
    };
    struct voice_t
    {
        std::uint32_t id = 0;
        std::shared_ptr< std::vector< std::int16_t > > pcm;
        int rate = 0;
        int channels = 0;
        int frames = 0;
        double cursor = 0.0;
        bool loop = false;
        bool playing = false;
        bool done = false;
        bool music = false;
        bool placed = false;
        float volume = 1.0f;
        float min_d = 0.0f;
        float max_d = 0.0f;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        int category = 0;
        std::string name;
        const void* holder = nullptr;
        std::uint32_t callback = 0;
    };

    struct reader_t
    {
        const std::uint8_t* p = nullptr;
        int n = 0;
        int o = 0;
        bool ok = true;
        int left( ) const { return n - o; }
        std::uint32_t u32( )
        {
            if ( left( ) < 4 ) { ok = false; return 0; }
            std::uint32_t v; std::memcpy( &v, p + o, 4 ); o += 4; return v;
        }
        std::uint16_t u16( )
        {
            if ( left( ) < 2 ) { ok = false; return 0; }
            std::uint16_t v; std::memcpy( &v, p + o, 2 ); o += 2; return v;
        }
        void skip( int bytes )
        {
            if ( bytes < 0 || left( ) < bytes ) { ok = false; return; }
            o += bytes;
        }
        std::string str( )
        {
            const std::uint32_t size = u32( );
            if ( !ok || size > 4096 || left( ) < static_cast< int >( size ) ) { ok = false; return { }; }
            std::string out;
            if ( size > 1 )
                out.assign( reinterpret_cast< const char* >( p + o ), size - 1 );
            o += static_cast< int >( size );
            return out;
        }
    };

    static std::string sample_key( const std::string& bank, int index ) { return bank + "#" + std::to_string( index ); }

    static bool contains_loop( const std::string& name )
    {
        for ( int i = 0; i + 4 <= static_cast< int >( name.size( ) ); ++i )
        {
            const char a = name[ static_cast< std::size_t >( i ) ];
            auto low = []( char c ) { return c >= 'A' && c <= 'Z' ? static_cast< char >( c + 32 ) : c; };
            if ( low( a ) == 'l' && low( name[ i + 1 ] ) == 'o' && low( name[ i + 2 ] ) == 'o' && low( name[ i + 3 ] ) == 'p' )
                return true;
        }
        return false;
    }

    voice_t* find( std::uint32_t id )
    {
        for ( voice_t& voice : m_voices )
            if ( voice.id == id )
                return &voice;
        return nullptr;
    }

    bool ensure( c_archive& archive )
    {
        if ( m_ready )
            return m_events.size( ) != 0;
        std::vector< std::string > banks;
        int projects = 0;
        for ( const std::string& name : archive.names( "VuAudioProjectAsset/" ) )
        {
            std::vector< std::uint8_t > bytes;
            if ( !archive.read( ( "VuAudioProjectAsset/" + name ).c_str( ), bytes ) || bytes.size( ) < 32 )
                continue;
            if ( parse_project( bytes, banks ) )
                ++projects;
            else
                LOG_WARNING( "audio: project '%s' did not parse", name.c_str( ) );
        }
        ( void )banks;
        m_ready = true;
        if ( projects == 0 )
        {
            LOG_WARNING( "audio: no project parsed" );
            return false;
        }
        m_run.store( true );
        m_thread = std::thread( [ this ] { thread_main( ); } );
        for ( int i = 0; i < 2000 && m_opened.load( ) == 0; ++i )
            Sleep( 1 );
        LOG_INFO( "audio: %d projects, %u events%s", projects, static_cast< unsigned >( m_events.size( ) ),
                  m_device ? "" : " (no output device)" );
        return m_events.size( ) != 0;
    }

    void load_bank( c_archive& archive, const std::string& bank )
    {
        if ( bank.empty( ) || m_loaded_banks.count( bank ) != 0 )
            return;
        m_loaded_banks.insert( bank );
        std::vector< std::uint8_t > bytes;
        if ( !archive.read( ( "VuAudioBankAsset/" + bank ).c_str( ), bytes ) || bytes.size( ) < 64 )
        {
            LOG_WARNING( "audio: bank '%s' missing", bank.c_str( ) );
            return;
        }
        const int samples = parse_bank( bank, bytes );
        LOG_INFO( "audio: bank '%s', %d IMA samples", bank.c_str( ), samples );
    }

    static std::vector< std::uint8_t > chunk( const std::uint8_t* data, int size, const char* kind )
    {
        const auto take = [&]( auto& self, int at ) -> std::vector< std::uint8_t > {
            while ( at + 8 <= size )
            {
                if ( data[ at ] < 32 || data[ at ] > 126 )
                    return { };
                const int sz = static_cast< int >( data[ at + 4 ] | ( data[ at + 5 ] << 8 ) | ( data[ at + 6 ] << 16 ) | ( data[ at + 7 ] << 24 ) );
                if ( sz < 0 || at + 8 + sz > size )
                    return { };
                if ( std::memcmp( data + at, "LIST", 4 ) == 0 )
                    return self( self, at + 12 );
                if ( std::memcmp( data + at, kind, 4 ) == 0 )
                    return std::vector< std::uint8_t >( data + at + 8, data + at + 8 + sz );
                at += 8 + sz + ( sz & 1 );
            }
            return { };
        };
        return take( take, 12 );
    }

    bool parse_project( const std::vector< std::uint8_t >& file, std::vector< std::string >& banks_out )
    {
        if ( file.size( ) < 28 )
            return false;
        const std::uint8_t* riff = file.data( ) + 4;
        const int riff_n = static_cast< int >( file.size( ) ) - 4;
        if ( std::memcmp( riff, "RIFF", 4 ) != 0 || std::memcmp( riff + 8, "FEV ", 4 ) != 0 )
            return false;
        std::uint32_t version = 0;
        std::memcpy( &version, riff + 0x14, 4 );
        if ( version != 0x00450000u )
        {
            LOG_WARNING( "audio: FEV version %08x is not 0x00450000", version );
            return false;
        }
        const std::vector< std::uint8_t > strr = chunk( riff, riff_n, "STRR" );
        const std::vector< std::uint8_t > lgcy = chunk( riff, riff_n, "LGCY" );
        if ( strr.size( ) < 8 || lgcy.size( ) < 16 )
            return false;
        reader_t strings{ strr.data( ), static_cast< int >( strr.size( ) ), 0, true };
        const std::uint32_t string_count = strings.u32( );
        if ( !strings.ok || string_count > 100000 || strings.left( ) < static_cast< int >( string_count ) * 4 )
            return false;
        std::vector< std::uint32_t > offsets( string_count );
        std::memcpy( offsets.data( ), strr.data( ) + 4, string_count * 4 );
        const int string_base = 4 + static_cast< int >( string_count ) * 4;
        const auto lookup = [&]( std::uint32_t index ) -> std::string {
            if ( index >= string_count )
                return { };
            const int at = string_base + static_cast< int >( offsets[ index ] );
            if ( at < 0 || at >= static_cast< int >( strr.size( ) ) )
                return { };
            const char* s = reinterpret_cast< const char* >( strr.data( ) + at );
            const int room = static_cast< int >( strr.size( ) ) - at;
            const int n = static_cast< int >( ::strnlen( s, static_cast< std::size_t >( room ) ) );
            return std::string( s, s + n );
        };

        reader_t r{ lgcy.data( ), static_cast< int >( lgcy.size( ) ), 0, true };
        r.skip( 8 );
        const std::string project = r.str( );
        const std::uint32_t bank_count = r.u32( );
        const std::uint32_t languages = r.u32( );
        if ( !r.ok || bank_count > 64 || languages > 32 )
            return false;
        std::vector< std::string > banks;
        for ( std::uint32_t i = 0; i < bank_count && r.ok; ++i )
        {
            r.skip( 8 + static_cast< int >( languages ) * 12 );
            banks.push_back( r.str( ) );
        }
        if ( !r.ok || !category( r ) )
            return false;
        struct pending_t
        {
            std::string path;
            std::vector< std::uint16_t > defs;
            float volume = 1.0f;
            float min_d = 0.0f;
            float max_d = 0.0f;
            int category = 0;
        };
        std::vector< pending_t > pending;
        const auto props = [&]( ) -> bool {
            const std::uint32_t count = r.u32( );
            if ( !r.ok || count > 10000 )
                return false;
            for ( std::uint32_t i = 0; i < count; ++i )
            {
                r.str( );
                const std::uint32_t type = r.u32( );
                if ( type == 2 )
                    r.str( );
                else if ( type < 2 )
                    r.skip( 4 );
                else
                    return false;
            }
            return r.ok;
        };
        const auto envelope = [&]( ) {
            r.skip( 4 );
            r.str( );
            r.skip( 12 );
            const std::uint32_t points = r.u32( );
            if ( points > 100000 )
                r.ok = false;
            r.skip( static_cast< int >( points ) * 4 + 8 );
        };
        const auto event_sound = [&]( ) -> std::uint16_t {
            const std::uint16_t index = r.u16( );
            r.skip( 0x38 );
            return index;
        };
        const auto parse_event = [&]( auto& self, const std::string& path ) -> bool {
            const std::uint32_t event_type = r.u32( );
            const std::string name = lookup( r.u32( ) );
            r.skip( 16 );
            const int header = r.o;
            r.skip( 0x90 );
            if ( !r.ok )
                return false;
            pending_t item;
            item.path = path + "/" + name;
            if ( header + 0x30 <= r.n )
            {
                float volume = 1.0f, min_d = 0.0f, max_d = 0.0f;
                std::memcpy( &volume, r.p + header, 4 );
                std::memcpy( &min_d, r.p + header + 0x24, 4 );
                std::memcpy( &max_d, r.p + header + 0x2c, 4 );
                if ( volume >= 0.0f && volume <= 4.0f )
                    item.volume = volume;
                if ( min_d >= 0.0f && min_d < 1000000.0f )
                    item.min_d = min_d;
                if ( max_d >= 0.0f && max_d < 1000000.0f )
                    item.max_d = max_d;
            }
            const std::uint32_t kind = event_type & 0x18u;
            if ( kind == 0x08u )
            {
                const std::uint32_t layers = r.u32( );
                if ( layers > 10000 )
                    return false;
                for ( std::uint32_t layer = 0; layer < layers && r.ok; ++layer )
                {
                    r.skip( 6 );
                    const std::uint32_t sounds = r.u16( );
                    const std::uint32_t envelopes = r.u16( );
                    if ( sounds > 10000 || envelopes > 10000 )
                        return false;
                    for ( std::uint32_t s = 0; s < sounds; ++s )
                        item.defs.push_back( event_sound( ) );
                    for ( std::uint32_t e = 0; e < envelopes; ++e )
                        envelope( );
                }
                const std::uint32_t params = r.u32( );
                if ( params > 10000 )
                    return false;
                for ( std::uint32_t p = 0; p < params && r.ok; ++p )
                {
                    r.skip( 4 + 0x18 );
                    const std::uint32_t sustain = r.u32( );
                    r.skip( static_cast< int >( sustain ) * 4 );
                }
                if ( !props( ) )
                    return false;
            }
            else if ( kind == 0x10u )
            {
                r.skip( 4 );
                item.defs.push_back( event_sound( ) );
            }
            else
                return false;
            const std::uint32_t categories = r.u32( );
            if ( categories > 1000 )
                return false;
            for ( std::uint32_t c = 0; c < categories; ++c )
            {
                const std::string group = r.str( );
                if ( group.rfind( "ui", 0 ) == 0 )
                    item.category = 1;
                else if ( group.rfind( "music", 0 ) == 0 )
                    item.category = 2;
            }
            if ( !r.ok )
                return false;
            pending.push_back( std::move( item ) );
            ( void )self;
            return true;
        };
        const auto parse_group = [&]( auto& self, const std::string& path ) -> bool {
            const std::string name = lookup( r.u32( ) );
            if ( !props( ) )
                return false;
            const std::uint32_t groups = r.u32( );
            const std::uint32_t events = r.u32( );
            if ( !r.ok || groups > 100000 || events > 100000 )
                return false;
            const std::string full = path.empty( ) ? name : path + "/" + name;
            for ( std::uint32_t i = 0; i < groups; ++i )
                if ( !self( self, full ) )
                    return false;
            for ( std::uint32_t i = 0; i < events; ++i )
                if ( !parse_event( parse_event, full ) )
                    return false;
            return r.ok;
        };
        const std::uint32_t groups = r.u32( );
        if ( !r.ok || groups > 100000 )
            return false;
        for ( std::uint32_t i = 0; i < groups; ++i )
            if ( !parse_group( parse_group, project ) )
                return false;
        const std::uint32_t def_defs = r.u32( );
        if ( !r.ok || def_defs > 100000 )
            return false;
        r.skip( static_cast< int >( def_defs ) * 74 );
        const std::uint32_t def_count = r.u32( );
        if ( !r.ok || def_count > 100000 )
            return false;
        struct def_t { std::vector< wave_t > waves; };
        std::vector< def_t > defs( def_count );
        for ( std::uint32_t i = 0; i < def_count && r.ok; ++i )
        {
            r.u32( );
            r.skip( 4 );
            const std::uint32_t entries = r.u32( );
            if ( entries > 10000 )
                return false;
            for ( std::uint32_t e = 0; e < entries && r.ok; ++e )
            {
                const std::uint32_t type = r.u32( );
                r.skip( 4 );
                if ( type == 0 )
                {
                    const std::string stream = r.str( );
                    const std::uint32_t bank = r.u32( );
                    const std::uint32_t index = r.u32( );
                    r.skip( 4 );
                    if ( bank < banks.size( ) )
                    {
                        wave_t wave;
                        wave.bank = banks[ bank ];
                        wave.index = static_cast< int >( index );
                        wave.loop = contains_loop( stream );
                        defs[ i ].waves.push_back( std::move( wave ) );
                    }
                }
                else if ( type == 1 )
                    r.skip( 8 );
                else if ( type != 2 && type != 3 )
                    return false;
            }
        }
        if ( !r.ok )
            return false;
        for ( const std::string& bank : banks )
            if ( std::find( banks_out.begin( ), banks_out.end( ), bank ) == banks_out.end( ) )
                banks_out.push_back( bank );
        for ( const pending_t& item : pending )
        {
            std::vector< wave_t > waves;
            for ( std::uint16_t def : item.defs )
                if ( def < defs.size( ) )
                    waves.insert( waves.end( ), defs[ def ].waves.begin( ), defs[ def ].waves.end( ) );
            for ( wave_t& wave : waves )
            {
                wave.volume = item.volume;
                wave.min_d = item.min_d;
                wave.max_d = item.max_d;
                wave.category = item.category;
            }
            if ( !waves.empty( ) && m_events.find( item.path ) == m_events.end( ) )
                m_events.emplace( item.path, std::move( waves ) );
        }
        return true;
    }

    static bool category( reader_t& r )
    {
        r.str( );
        r.skip( 16 );
        const std::uint32_t children = r.u32( );
        if ( !r.ok || children > 10000 )
            return false;
        for ( std::uint32_t i = 0; i < children; ++i )
            if ( !category( r ) )
                return false;
        return r.ok;
    }

    static std::uint8_t bitrev( std::uint8_t x )
    {
        x = static_cast< std::uint8_t >( ( x & 0xF0 ) >> 4 | ( x & 0x0F ) << 4 );
        x = static_cast< std::uint8_t >( ( x & 0xCC ) >> 2 | ( x & 0x33 ) << 2 );
        x = static_cast< std::uint8_t >( ( x & 0xAA ) >> 1 | ( x & 0x55 ) << 1 );
        return x;
    }

    int parse_bank( const std::string& bank, const std::vector< std::uint8_t >& file )
    {
        static const char key[] = "gUGEwemu2Ec2!u&_";
        if ( file.size( ) < 68 )
            return 0;
        std::vector< std::uint8_t > fsb( file.size( ) - 4 );
        for ( std::size_t i = 4; i < file.size( ); ++i )
            fsb[ i - 4 ] = static_cast< std::uint8_t >( bitrev( file[ i ] ) ^ static_cast< std::uint8_t >( key[ ( i - 4 ) % 16 ] ) );
        if ( fsb.size( ) < 0x40 || std::memcmp( fsb.data( ), "FSB5", 4 ) != 0 )
        {
            LOG_WARNING( "audio: bank '%s' did not decrypt to FSB5", bank.c_str( ) );
            return 0;
        }
        std::uint32_t samples = 0, header = 0, names = 0, data = 0, codec = 0;
        std::memcpy( &samples, fsb.data( ) + 8, 4 );
        std::memcpy( &header, fsb.data( ) + 12, 4 );
        std::memcpy( &names, fsb.data( ) + 16, 4 );
        std::memcpy( &data, fsb.data( ) + 20, 4 );
        std::memcpy( &codec, fsb.data( ) + 24, 4 );
        if ( codec != 0x07u )
        {
            LOG_INFO( "audio: bank '%s' codec %u is not IMA", bank.c_str( ), codec );
            return 0;
        }
        if ( samples > 100000 || 0x3C + header + names > fsb.size( ) )
            return 0;
        static const int rates[] = { 4000, 8000, 11000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 96000 };
        static const int channels[] = { 1, 2, 6, 8 };
        struct info_t { std::uint32_t frames, offset; int rate, ch; };
        std::vector< info_t > info;
        int at = 0x3C;
        for ( std::uint32_t i = 0; i < samples; ++i )
        {
            if ( at + 8 > 0x3C + static_cast< int >( header ) )
                return 0;
            std::uint64_t mode = 0;
            std::memcpy( &mode, fsb.data( ) + at, 8 );
            info_t row;
            row.frames = static_cast< std::uint32_t >( ( mode >> 34 ) & 0x3FFFFFFF );
            row.offset = static_cast< std::uint32_t >( ( ( mode >> 7 ) & 0x07FFFFFF ) << 5 );
            row.ch = channels[ ( mode >> 5 ) & 3 ];
            const unsigned rate_i = static_cast< unsigned >( ( mode >> 1 ) & 0x0F );
            row.rate = rate_i < 11 ? rates[ rate_i ] : 0;
            int pos = at + 8;
            int extra = static_cast< int >( mode & 1 );
            while ( extra && pos + 4 <= 0x3C + static_cast< int >( header ) )
            {
                std::uint32_t flag = 0;
                std::memcpy( &flag, fsb.data( ) + pos, 4 );
                const std::uint32_t type = ( flag >> 25 ) & 0x7F;
                const std::uint32_t size = ( flag >> 1 ) & 0xFFFFFF;
                extra = static_cast< int >( flag & 1 );
                if ( type == 1 && pos + 5 <= static_cast< int >( fsb.size( ) ) )
                    row.ch = fsb[ static_cast< std::size_t >( pos + 4 ) ];
                if ( type == 2 && pos + 8 <= static_cast< int >( fsb.size( ) ) )
                    std::memcpy( &row.rate, fsb.data( ) + pos + 4, 4 );
                pos += 4 + static_cast< int >( size );
            }
            at = pos;
            info.push_back( row );
        }
        const int data_at = 0x3C + static_cast< int >( header ) + static_cast< int >( names );
        const std::uint8_t* bytes = fsb.data( );
        const int bytes_n = static_cast< int >( fsb.size( ) );
        int decoded = 0;
        for ( std::size_t i = 0; i < info.size( ); ++i )
        {
            const info_t& row = info[ i ];
            if ( row.rate <= 0 || row.ch <= 0 || row.ch > 8 )
                continue;
            const std::uint32_t next = ( i + 1 < info.size( ) ) ? info[ i + 1 ].offset : data;
            if ( next <= row.offset )
                continue;
            const int start = data_at + static_cast< int >( row.offset );
            const int size = static_cast< int >( next - row.offset );
            if ( start < 0 || size < 36 || start + size > bytes_n )
                continue;
            sample_t sample;
            sample.rate = row.rate;
            sample.channels = row.ch;
            sample.frames = static_cast< int >( row.frames );
            sample.pcm = std::make_shared< std::vector< std::int16_t > >( );
            decode_ima( bytes + start, size, row.ch, sample.frames, *sample.pcm );
            if ( sample.pcm->empty( ) )
                continue;
            m_samples.emplace( sample_key( bank, static_cast< int >( i ) ), std::move( sample ) );
            ++decoded;
        }
        return decoded;
    }

    static void ima_expand( int& predictor, int& index, int nibble )
    {
        static const int step[ 89 ] = {
            7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
            73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
            494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499,
            2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
            12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
        };
        static const int adjust[ 8 ] = { -1, -1, -1, -1, 2, 4, 6, 8 };
        if ( index < 0 )
            index = 0;
        if ( index > 88 )
            index = 88;
        const int s = step[ index ];
        int diff = s >> 3;
        if ( nibble & 1 ) diff += s >> 2;
        if ( nibble & 2 ) diff += s >> 1;
        if ( nibble & 4 ) diff += s;
        if ( nibble & 8 ) diff = -diff;
        predictor += diff;
        if ( predictor > 32767 ) predictor = 32767;
        if ( predictor < -32768 ) predictor = -32768;
        index += adjust[ nibble & 7 ];
        if ( index < 0 ) index = 0;
        if ( index > 88 ) index = 88;
    }

    // FSB5 IMA is Xbox IMA for one or two channels (low nibble first, the header sample counts,
    // the last nibble is skipped) and FMOD's own interleave above that. A sequential high-nibble
    // read drives the predictor into the rails, which is the harsh voice and the blown ambience.
    static void decode_ima( const std::uint8_t* src, int size, int channels, int frames, std::vector< std::int16_t >& out )
    {
        if ( channels < 1 || channels > 8 )
            return;
        const int frame = 36 * channels;
        const int blocks = size / frame;
        const int count = blocks * 64;
        if ( blocks <= 0 || frames <= 0 )
            return;
        out.assign( static_cast< std::size_t >( count * channels ), 0 );
        for ( int block = 0; block < blocks; ++block )
        {
            const std::uint8_t* packet = src + block * frame;
            for ( int ch = 0; ch < channels; ++ch )
            {
                int predictor = 0;
                int index = 0;
                if ( channels <= 2 )
                {
                    const int header = channels == 1 ? 0 : ch * 4;
                    predictor = static_cast< std::int16_t >( packet[ header ] | ( packet[ header + 1 ] << 8 ) );
                    index = packet[ header + 2 ];
                }
                else
                {
                    const int hist = ch * 2;
                    predictor = static_cast< std::int16_t >( packet[ hist ] | ( packet[ hist + 1 ] << 8 ) );
                    index = packet[ channels * 2 + ch * 2 ];
                }
                std::int16_t* dst = out.data( ) + ( block * 64 ) * channels + ch;
                dst[ 0 ] = static_cast< std::int16_t >( predictor );
                for ( int n = 0; n < 63; ++n )
                {
                    int byte = 0;
                    if ( channels == 1 )
                        byte = 4 + n / 2;
                    else if ( channels == 2 )
                        byte = 8 + 4 * ch + 8 * ( n / 8 ) + ( n % 8 ) / 2;
                    else
                        byte = 4 * channels + 2 * ch + ( n / 4 ) * 2 * channels + ( n % 4 ) / 2;
                    const int nibble = ( n & 1 ) ? ( packet[ byte ] >> 4 ) & 0xF : packet[ byte ] & 0xF;
                    ima_expand( predictor, index, nibble );
                    dst[ ( n + 1 ) * channels ] = static_cast< std::int16_t >( predictor );
                }
            }
        }
        if ( frames < count )
            out.resize( static_cast< std::size_t >( frames * channels ) );
    }

    void thread_main( )
    {
        const HRESULT init = CoInitializeEx( nullptr, COINIT_MULTITHREADED );
        const bool com = SUCCEEDED( init ) && init != RPC_E_CHANGED_MODE;
        open_device( );
        m_opened.store( m_device ? 1 : 2 );
        if ( m_device )
            mix_loop( );
        else
        {
            if ( m_render != nullptr )
                m_render->Release( );
            if ( m_client != nullptr )
                m_client->Release( );
            m_render = nullptr;
            m_client = nullptr;
        }
        if ( com )
            CoUninitialize( );
    }

    void open_device( )
    {
        IMMDeviceEnumerator* enumerator = nullptr;
        if ( FAILED( CoCreateInstance( __uuidof( MMDeviceEnumerator ), nullptr, CLSCTX_ALL, __uuidof( IMMDeviceEnumerator ), reinterpret_cast< void** >( &enumerator ) ) ) )
            return;
        IMMDevice* device = nullptr;
        const HRESULT got = enumerator->GetDefaultAudioEndpoint( eRender, eConsole, &device );
        enumerator->Release( );
        if ( FAILED( got ) )
            return;
        if ( FAILED( device->Activate( __uuidof( IAudioClient ), CLSCTX_ALL, nullptr, reinterpret_cast< void** >( &m_client ) ) ) )
        {
            device->Release( );
            return;
        }
        device->Release( );
        WAVEFORMATEX* mix = nullptr;
        if ( FAILED( m_client->GetMixFormat( &mix ) ) || mix == nullptr )
            return;
        m_out_rate = static_cast< int >( mix->nSamplesPerSec );
        m_out_channels = mix->nChannels;
        m_out_float = false;
        if ( mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT )
            m_out_float = true;
        else if ( mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE )
        {
            const auto* ext = reinterpret_cast< WAVEFORMATEXTENSIBLE* >( mix );
            m_out_float = ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        }
        const HRESULT started = m_client->Initialize( AUDCLNT_SHAREMODE_SHARED, 0, 1000000, 0, mix, nullptr );
        CoTaskMemFree( mix );
        if ( FAILED( started ) )
            return;
        if ( FAILED( m_client->GetBufferSize( &m_buffer ) ) )
            return;
        if ( FAILED( m_client->GetService( __uuidof( IAudioRenderClient ), reinterpret_cast< void** >( &m_render ) ) ) )
            return;
        if ( FAILED( m_client->Start( ) ) )
            return;
        m_device = m_out_rate > 0 && m_out_channels > 0;
    }

    void mix_loop( )
    {
        while ( m_run.load( ) )
        {
            UINT32 padding = 0;
            if ( m_client->GetCurrentPadding( &padding ) != S_OK )
                break;
            const UINT32 want = m_buffer - padding;
            if ( want == 0 )
            {
                Sleep( 5 );
                continue;
            }
            BYTE* dest = nullptr;
            if ( m_render->GetBuffer( want, &dest ) != S_OK || dest == nullptr )
                break;
            mix( dest, static_cast< int >( want ) );
            m_render->ReleaseBuffer( want, 0 );
        }
        if ( m_client != nullptr )
            m_client->Stop( );
        if ( m_render != nullptr )
            m_render->Release( );
        if ( m_client != nullptr )
            m_client->Release( );
        m_render = nullptr;
        m_client = nullptr;
    }

    void mix( BYTE* dest, int frames )
    {
        std::lock_guard< std::mutex > lock( m_mutex );
        const int channels = m_out_channels > 0 ? m_out_channels : 2;
        for ( int frame = 0; frame < frames; ++frame )
        {
            float left = 0.0f;
            float right = 0.0f;
            for ( voice_t& voice : m_voices )
            {
                if ( !voice.playing || voice.done || voice.pcm == nullptr || voice.frames <= 0 || voice.rate <= 0 )
                    continue;
                const int i0 = static_cast< int >( voice.cursor );
                if ( i0 >= voice.frames )
                {
                    if ( voice.loop )
                        voice.cursor = 0.0;
                    else
                        voice.done = true;
                    continue;
                }
                const int i1 = i0 + 1 < voice.frames ? i0 + 1 : i0;
                const float t = static_cast< float >( voice.cursor - i0 );
                const std::int16_t* pcm = voice.pcm->data( );
                const int ch = voice.channels > 0 ? voice.channels : 1;
                const float l = ( static_cast< float >( pcm[ i0 * ch ] ) * ( 1.0f - t ) + static_cast< float >( pcm[ i1 * ch ] ) * t ) / 32768.0f;
                const float r = ch > 1
                    ? ( static_cast< float >( pcm[ i0 * ch + 1 ] ) * ( 1.0f - t ) + static_cast< float >( pcm[ i1 * ch + 1 ] ) * t ) / 32768.0f
                    : l;
                float gain = voice.volume;
                if ( voice.category == 1 )
                    gain *= m_ui_volume;
                else if ( voice.category == 2 )
                    gain *= m_music_volume;
                else
                    gain *= m_game_volume;
                if ( voice.placed && voice.min_d > 0.0f && voice.min_d < 5000.0f && voice.max_d > voice.min_d )
                {
                    const float dx = voice.x - m_lx;
                    const float dy = voice.y - m_ly;
                    const float dz = voice.z - m_lz;
                    const float dist = std::sqrt( dx * dx + dy * dy + dz * dz );
                    if ( dist >= voice.max_d )
                        gain = 0.0f;
                    else if ( dist > voice.min_d )
                        gain *= voice.min_d / dist;
                }
                left += l * gain;
                right += r * gain;
                voice.cursor += static_cast< double >( voice.rate ) / static_cast< double >( m_out_rate );
                if ( voice.cursor >= voice.frames )
                {
                    if ( voice.loop )
                        voice.cursor -= voice.frames;
                    else
                        voice.done = true;
                }
            }
            if ( left > 1.0f ) left = 1.0f;
            if ( left < -1.0f ) left = -1.0f;
            if ( right > 1.0f ) right = 1.0f;
            if ( right < -1.0f ) right = -1.0f;
            if ( m_out_float )
            {
                float* out = reinterpret_cast< float* >( dest ) + frame * channels;
                for ( int c = 0; c < channels; ++c )
                    out[ c ] = ( c & 1 ) ? right : left;
            }
            else
            {
                std::int16_t* out = reinterpret_cast< std::int16_t* >( dest ) + frame * channels;
                for ( int c = 0; c < channels; ++c )
                {
                    const float s = ( c & 1 ) ? right : left;
                    out[ c ] = static_cast< std::int16_t >( s * 32767.0f );
                }
            }
        }
    }

    bool m_ready = false;
    bool m_device = false;
    bool m_out_float = false;
    int m_out_rate = 48000;
    int m_out_channels = 2;
    UINT32 m_buffer = 0;
    IAudioClient* m_client = nullptr;
    IAudioRenderClient* m_render = nullptr;
    std::atomic< bool > m_run{ false };
    std::atomic< int > m_opened{ 0 };
    std::thread m_thread;
    std::mutex m_mutex;
    std::uint32_t m_next = 1;
    std::uint32_t m_pick = 0;
    int m_misses = 0;
    std::map< std::string, std::vector< wave_t > > m_events;
    std::map< std::string, sample_t > m_samples;
    std::map< std::string, sample_t > m_music_samples;
    std::set< std::string > m_loaded_banks;
    std::set< std::string > m_music_failed;
    std::vector< voice_t > m_voices;
    float m_game_volume = 0.8f;
    float m_ui_volume = 0.8f;
    float m_music_volume = 0.8f;
    float m_lx = 0.0f;
    float m_ly = -40.0f;
    float m_lz = 18.0f;
    float m_music_level = 0.0f;
    int m_music_index = 0;
    bool m_music_on = false;
    bool m_music_miss = false;

    sample_t music_load( c_archive& archive, const std::string& name )
    {
        const auto cached = m_music_samples.find( name );
        if ( cached != m_music_samples.end( ) )
            return cached->second;
        std::vector< std::uint8_t > file;
        if ( !archive.read( ( "VuAudioStreamAsset/" + name ).c_str( ), file ) || file.size( ) < 0x60 )
        {
            LOG_WARNING( "audio: music '%s' is not in the pack", name.c_str( ) );
            return { };
        }
        if ( std::memcmp( file.data( ) + 4, "FSB5", 4 ) != 0 )
        {
            LOG_WARNING( "audio: music '%s' is not FSB5", name.c_str( ) );
            return { };
        }
        std::uint32_t header = 0, names = 0, data = 0, codec = 0;
        std::memcpy( &header, file.data( ) + 4 + 12, 4 );
        std::memcpy( &names, file.data( ) + 4 + 16, 4 );
        std::memcpy( &data, file.data( ) + 4 + 20, 4 );
        std::memcpy( &codec, file.data( ) + 4 + 24, 4 );
        if ( codec != 0x0Cu )
        {
            LOG_WARNING( "audio: music '%s' codec %u is not CELT", name.c_str( ), codec );
            return { };
        }
        const std::uint8_t* fsb = file.data( ) + 4;
        const int fsb_n = static_cast< int >( file.size( ) - 4 );
        if ( 0x3C + static_cast< int >( header ) > fsb_n )
            return { };
        std::uint64_t mode = 0;
        std::memcpy( &mode, fsb + 0x3C, 8 );
        const int frames = static_cast< int >( ( mode >> 34 ) & 0x3FFFFFFF );
        int channels = ( ( mode >> 5 ) & 3 ) == 0 ? 1 : 2;
        const int data_at = 0x3C + static_cast< int >( header ) + static_cast< int >( names );
        if ( frames <= 0 || data_at < 0 || data_at + static_cast< int >( data ) > fsb_n )
            return { };
        LOG_INFO( "audio: decoding '%s'", name.c_str( ) );
        sample_t sample;
        sample.rate = 44100;
        sample.channels = channels;
        sample.pcm = std::make_shared< std::vector< std::int16_t > >( );
        sample.frames = hydro_celt_decode( fsb + data_at, static_cast< int >( data ), channels, frames, *sample.pcm );
        if ( sample.frames <= 0 )
        {
            LOG_WARNING( "audio: music '%s' did not decode", name.c_str( ) );
            return { };
        }
        m_music_samples.emplace( name, sample );
        return sample;
    }
};
