#pragma once

#include <cstdint>
#include <vector>

// One FSB5 CELT stream (libcelt 0.11, 44100 Hz, 512-sample frames) into interleaved int16.
// Returns the number of frames written, or 0 when the stream is not that codec.
int hydro_celt_decode( const std::uint8_t* data, int size, int channels, int frames, std::vector< std::int16_t >& pcm );
