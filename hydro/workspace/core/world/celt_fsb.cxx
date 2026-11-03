#include <workspace/core/world/celt_fsb.hxx>

#include <celt.h>

#include <cstring>

int hydro_celt_decode( const std::uint8_t* data, int size, int channels, int frames, std::vector< std::int16_t >& pcm )
{
    pcm.clear( );
    if ( data == nullptr || size < 8 || channels < 1 || channels > 2 || frames <= 0 )
        return 0;
    int error = 0;
    CELTMode* mode = celt_mode_create( 44100, 512, &error );
    if ( mode == nullptr || error != 0 )
        return 0;
    CELTDecoder* decoder = celt_decoder_create_custom( mode, channels, &error );
    if ( decoder == nullptr || error != 0 )
    {
        celt_mode_destroy( mode );
        return 0;
    }
    pcm.assign( static_cast< std::size_t >( frames ) * static_cast< std::size_t >( channels ), 0 );
    int produced = 0;
    int at = 0;
    while ( produced < frames && at + 8 <= size )
    {
        const std::uint32_t magic = ( static_cast< std::uint32_t >( data[ at ] ) << 24 )
            | ( static_cast< std::uint32_t >( data[ at + 1 ] ) << 16 )
            | ( static_cast< std::uint32_t >( data[ at + 2 ] ) << 8 )
            | static_cast< std::uint32_t >( data[ at + 3 ] );
        std::uint32_t packet = 0;
        std::memcpy( &packet, data + at + 4, 4 );
        if ( magic != 0x17C30DF3u || packet == 0 || packet > 0x200u || at + 8 + static_cast< int >( packet ) > size )
            break;
        const int room = frames - produced;
        if ( room < 512 )
            break;
        const int result = celt_decode( decoder, data + at + 8, static_cast< int >( packet ),
            pcm.data( ) + static_cast< std::size_t >( produced ) * static_cast< std::size_t >( channels ), 512 );
        if ( result <= 0 )
            break;
        produced += result;
        at += 8 + static_cast< int >( packet );
    }
    celt_decoder_destroy( decoder );
    celt_mode_destroy( mode );
    if ( produced <= 0 )
    {
        pcm.clear( );
        return 0;
    }
    pcm.resize( static_cast< std::size_t >( produced ) * static_cast< std::size_t >( channels ) );
    return produced;
}
