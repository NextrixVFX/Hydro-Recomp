#pragma once

#include <impl/engine/engine.hxx>
#include <workspace/core/assets/animation.hxx>
#include <workspace/core/assets/model.hxx>
#include <workspace/utility/math.hxx>

#include <cmath>
#include <cstring>
#include <vector>

// Posing a skinned model: the bone palette for one frame, and the CPU skin
// that writes the posed vertices into a streamed buffer. Shared by the boat
// hulls and the animated props, which differ only in which clip drives them
// and whether a steering delta is layered on top.

// The bind mesh kept for anything carrying a per-vertex binding: the rest
// vertices, their four-bone bindings, and the draw state the posed copy is
// submitted with.
struct skinned_mesh_t
{
    std::vector< mesh_vertex_t > bind;
    std::vector< model_skin_t > skin;
    std::vector< unsigned short > indices;
    int texture = -1;
    int detail = -1;
    int alpha_test = 0;
};

// Build the bone palette for one pose. Shared by the boat hulls and the
// animated props, which differ only in which clip drives them and whether
// a steering delta is layered on top.
//
// The two poses do NOT share a space. The bind pose is absolute, so it
// composes directly, but a clip stores each bone RELATIVE TO ITS PARENT
// and must be chained. Measured, not assumed: chaining clip frame 0
// reproduces the bind pose on 41 of 41 bones with a worst offset of
// 0.0000, where taking it directly matched only 14 of 41 and missed by up
// to 3.74 units.
inline void build_palette( std::vector< float >& palette,
    const std::vector< model_bone_t >& bind, const c_clip& clip, int frame,
    const c_clip* turn, int extreme, float swing )
{
    palette.assign( bind.size( ) * 16, 0.0f );
    for ( std::size_t b = 0; b < bind.size( ); ++b )
    {
        const clip_pose_t& pose = clip.at( frame, static_cast< int >( b ) );
        float local[ 16 ]{};
        model_compose( local, pose.position, pose.rotation, pose.scale );

        if ( turn != nullptr )
        {
            // Interpolate between the centre frame and whichever extreme
            // the steering points at. A plain normalised lerp rather than
            // a slerp: over a 20 degree arc the two differ by well under
            // a tenth of a degree, and the target cannot spare the trig.
            const clip_pose_t& centre = turn->at( 1, static_cast< int >( b ) );
            const clip_pose_t& swung = turn->at( extreme, static_cast< int >( b ) );
            float q[ 4 ];
            for ( int c = 0; c < 4; ++c )
                q[ c ] = centre.rotation[ c ] + ( swung.rotation[ c ] - centre.rotation[ c ] ) * swing;
            const float length = std::sqrt( q[ 0 ] * q[ 0 ] + q[ 1 ] * q[ 1 ]
                + q[ 2 ] * q[ 2 ] + q[ 3 ] * q[ 3 ] );
            // Every bone in Vector's clip measured unit length, so this
            // is a safety net rather than a filter: it stops a
            // zero-filled record in some other boat's clip from
            // collapsing a bone, and an identity delta passes through it
            // to multiply harmlessly.
            if ( length > 0.5f )
            {
                for ( int c = 0; c < 4; ++c )
                    q[ c ] /= length;
                const float origin[ 3 ]{ 0.0f, 0.0f, 0.0f };
                const float unit[ 3 ]{ 1.0f, 1.0f, 1.0f };
                float delta[ 16 ]{};
                model_compose( delta, origin, q, unit );
                // Order is forced, not chosen: the delta has no
                // translation, so local * delta swivels the motor about
                // its own origin and leaves it attached, where delta *
                // local would sweep it around the parent instead.
                float composed[ 16 ];
                math_multiply( composed, local, delta );
                std::memcpy( local, composed, sizeof( local ) );
            }
        }

        const int parent = bind[ b ].parent;
        if ( parent >= 0 && static_cast< std::size_t >( parent ) < b )
            math_multiply( palette.data( ) + b * 16, palette.data( ) + parent * 16, local );
        else
            std::memcpy( palette.data( ) + b * 16, local, sizeof( local ) );
    }
    // Now fold in the inverse bind, in place: palette[b] becomes
    // animatedWorld[b] * inverse(bindWorld[b]). Done after the chain so
    // children still read their parent's world matrix above.
    for ( std::size_t b = 0; b < bind.size( ); ++b )
    {
        float world[ 16 ];
        std::memcpy( world, palette.data( ) + b * 16, sizeof( world ) );
        math_multiply( palette.data( ) + b * 16, world, bind[ b ].inverse );
    }
}

