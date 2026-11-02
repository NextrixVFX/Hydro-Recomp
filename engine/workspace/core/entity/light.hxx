#pragma once

#include <cmath>
#include <functional>
#include <cstring>
#include <set>
#include <string>
#include <vector>

// c_corona_entity - VuCoronaEntity, the light coronas (lamps, beacons, glows). Everything here
// is transliterated, not inferred:
//
//   VuCoronaEntity_create        @0x5e8c80  operator new(0xF8), NOT zeroed
//   VuCoronaEntity_preCtorInit   @0x5e8d00  zeroes ONLY handles/pointers
//   VuCoronaEntity_ctor          @0x5e8d80  property -> member bindings
//   VuCoronaParams_ctor          @0x61ce60  THE DEFAULTS, on entity+160
//   VuCoronaParams_loadTexture   @0x61cf00  Texture Name -> a tex0 material
//   VuCoronaEntity_onLoad        @0x5e9a10  register + "Corona" tick + normalise axis
//   VuCoronaEntity_onUnload      @0x5e9b20  unregister
//   VuCoronaEntity_refreshBounds @0x5e9980  local box = +-Query Radius
//   VuCoronaEntity_worldBounds   @0x5e9c50  world box = position +- Query Radius
//   VuCoronaEntity_tick          @0x5e9b60  the ray-test request + the phase
//   VuCoronaEntity_occlude       @0x61d000  per-view ray test
//   VuCoronaEntity_draw          @0x5e9de0  cone test, picks the matrix
//   VuCoronaEntity_drawCorona    @0x61d1a0  distance/cone/fade, emits the record
//   VuCoronaEntity_drawBatch     @0x5e8410  the camera-facing card itself
//   VuCoronaEntity_Show          @0x5e8be0  / _Hide @0x5e8c30
//
// Two findings worth stating because an implementation written from the
// property names would get them backwards:
//
//   * "Rotation Speed" and "Rotation Axis" do NOT spin the sprite. The draw
//     spins the ENTITY'S MATRIX about Rotation Axis by the accumulated phase
//     and uses the result only for the CONE DIRECTION (matrix row 1). A
//     rotating beacon sweeps its cone; its card never turns.
//   * The card's own roll is "Rotation Offset + screenX * Rotation Amount" -
//     the lens-flare roll that depends on where the corona sits across the
//     screen, read out of @0x5e8410, not a constant spin.
//
// Show/Hide are not a flag either: they register and unregister the draw
// component with the global Vu3dDrawManager (dword_7590B8, vtable+40/+44), so
// a hidden corona is not in the draw list at all.
class c_corona_entity
{
public:
    // One VuCoronaEntity placement. Defaults are VuCoronaParams_ctor's, which
    // runs on entity+160; the pre-ctor initialiser @0x5e8d00 touches only the
    // handles, so this is the ONLY place the engine defines them.
    struct corona_t
    {
        std::string path;
        std::string texture;
        float world[ 16 ]{};
        // VuCoronaEntity_ctor stores TRUE at +112 for "Initially Visible", and
        // onLoad registers only when it is set.
        bool visible = true;
        bool registered = false;
        float draw_distance = 3.4028235e38f;    // +160 FLT_MAX  (0x7F7FFFFF)
        float fade_distance = 0.0f;             // +164
        float query_radius = 1.0f;              // +168
        float cone = 6.2831855f;                // +172 2*pi, a VuAngleProperty
        float penumbra = 0.0f;                  // +176      a VuAngleProperty
        bool back_light = false;                // +180
        float color[ 4 ]{ 1.0f, 1.0f, 1.0f, 1.0f }; // +181 VuColor FF FF FF FF
        bool screen_space = false;              // +185
        float size = 10.0f;                     // +188
        float rotation_offset = 0.0f;           // +192      a VuAngleProperty
        // +196, a VuAngleProperty. The constructor stores 0x40490FDB; writing
        // it as 3.14159f gives 0x40490FD0, which is a different float.
        float rotation_amount = 3.1415927f;
        float rotation_speed = 0.0f;            // +140      a VuAngleProperty
        float axis[ 3 ]{ 0.0f, 0.0f, 1.0f };    // +144, normalised by onLoad
        // +244, the accumulated spin as a FRACTION of a turn.
        float phase = 0.0f;
        // +208/+212 per view. The ray test is the one piece of this entity we
        // cannot reproduce: @0x61d000 casts a ray per viewport through the
        // collision world and fills a VuCoronaRayTestResult, and we have no
        // level ray-cast. SUBSTITUTION, flagged: visibility stays 1, so a
        // corona behind geometry still shines through. Retail's result is
        // strictly 0 or 1 - it gates the draw, it does not scale the alpha -
        // so the only error is the missing occlusion, not a wrong intensity.
        float visibility = 1.0f;
        bool request = false;                   // +0xD0, the draw's: the next tick casts
    };

    int add( const corona_t& light )
    {
        m_lights.push_back( light );
        corona_t& stored = m_lights.back( );
        // onLoad @0x5e9a10 normalises Rotation Axis IN PLACE at load, so every
        // later frame reads a unit axis. An unnormalised axis would change the
        // sweep rate, not just the direction.
        const float length = std::sqrt( stored.axis[ 0 ] * stored.axis[ 0 ]
            + stored.axis[ 1 ] * stored.axis[ 1 ] + stored.axis[ 2 ] * stored.axis[ 2 ] );
        if ( length > 0.0f )
        {
            const float scale = 1.0f / length;
            stored.axis[ 0 ] *= scale;
            stored.axis[ 1 ] *= scale;
            stored.axis[ 2 ] *= scale;
        }
        stored.registered = stored.visible;
        return static_cast< int >( m_lights.size( ) ) - 1;
    }

    // The "Show" input plug @0x5e8be0: register with the draw manager unless
    // already registered. Returns void in retail - the plug's return type is 0.
    void show( const std::string& path )
    {
        for ( corona_t& light : m_lights )
            if ( light.path == path )
                light.registered = true;
    }

    // "Hide" @0x5e8c30.
    void hide( const std::string& path )
    {
        for ( corona_t& light : m_lights )
            if ( light.path == path )
                light.registered = false;
    }

    bool owns( const std::string& path ) const
    {
        for ( const corona_t& light : m_lights )
            if ( light.path == path )
                return true;
        return false;
    }

    void place( const std::string& path, const float* world )
    {
        for ( corona_t& light : m_lights )
            if ( light.path == path )
                std::memcpy( light.world, world, sizeof( light.world ) );
    }

    void clear( ) { m_lights.clear( ); m_reported.clear( ); m_card = -1; }

    // Set: the frame's sprites go here instead of the engine's blended layer (world/stage.hxx: 0x5E8410).
    std::function< void( const float* position, std::uint32_t colour, float size, float rotation, float amount, const std::string& texture ) > m_sink;

    // The camera's projection, retail's +0x44 and 0x7594D0's [0][0] (screen-space size, roll).
    void projection( float x, float y ) { m_projection_x = x; m_projection_y = y; }

    // 0x61D000 per corona: a request casts eye -> position less Query Radius toward the eye; visible when nothing answers. `blocked` = the ray.
    template < typename Blocked >
    void occlusion( const float* eye, Blocked&& blocked )
    {
        for ( corona_t& light : m_lights )
        {
            light.visibility = 0.0f;
            if ( light.request )
            {
                const float dx = light.world[ 12 ] - eye[ 0 ], dy = light.world[ 13 ] - eye[ 1 ], dz = light.world[ 14 ] - eye[ 2 ];
                const float scale = 1.0f / std::sqrt( ( dy * dy + dx * dx ) + dz * dz );
                const float to[ 3 ] = { light.world[ 12 ] - ( scale * dx ) * light.query_radius, light.world[ 13 ] - ( scale * dy ) * light.query_radius, light.world[ 14 ] - ( scale * dz ) * light.query_radius };
                if ( !blocked( eye, to ) )
                    light.visibility = 1.0f;
            }
            light.request = false;
        }
    }

    int total( ) const { return static_cast< int >( m_lights.size( ) ); }

    int drawn( ) const { return m_drawn; }

    const std::vector< corona_t >& lights( ) const { return m_lights; }

    // VuCoronaEntity_tick @0x5e9b60, exactly: request the occlusion test, then
    // advance the phase by speed*dt/(2*pi) and keep only its fraction.
    void tick( float dt )
    {
        for ( corona_t& light : m_lights )
        {
            light.phase = light.rotation_speed * dt * 0.15915494f + light.phase;
            light.phase = light.phase - static_cast< float >( static_cast< int >( light.phase ) );
        }
    }

    // VuCoronaEntity_draw @0x5e9de0 + _drawCorona @0x61d1a0 + _drawBatch
    // @0x5e8410, fused: retail splits them across a draw component, a params
    // block and a deferred batch callback, and the split carries no behaviour.
    template < typename Texture >
    void draw( const float* eye, const float* target, Texture&& texture )
    {
        m_drawn = 0;
        // NO drop() here on purpose. Coronas share the blended sprite layer
        // with the particle quads, and c_pfx::draw clears that whole layer at
        // the top of its own pass - which runs immediately before this one in
        // both frame paths. Dropping again here would delete the quads c_pfx
        // has just placed; dropping a layer of our own would delete the level
        // models that model.hxx marks with the neighbouring mode.
        if ( eye == nullptr || target == nullptr || m_lights.empty( ) )
            return;

        if ( m_card < 0 )
            m_card = card( );
        if ( m_card < 0 )
            return;

        // The camera basis. Retail reads the 64 bytes at 0x7595D0 - the
        // camera's world matrix, row 0 right, row 1 forward, row 2 up - which
        // the view sets once a frame. REUSE, flagged: this is c_pfx::facing's
        // construction, already verified on screen for the quad patterns, so
        // the two billboard families stay in one basis.
        float forward[ 3 ] = { target[ 0 ] - eye[ 0 ], target[ 1 ] - eye[ 1 ], target[ 2 ] - eye[ 2 ] };
        math_normalize( forward );
        const float world_up[ 3 ] = { 0.0f, 0.0f, 1.0f };
        float right[ 3 ]{};
        float up[ 3 ]{};
        math_cross( right, world_up, forward );
        math_normalize( right );
        math_cross( up, forward, right );
        right[ 0 ] = -right[ 0 ];
        right[ 1 ] = -right[ 1 ];
        right[ 2 ] = -right[ 2 ];

        for ( corona_t& light : m_lights )
        {
            if ( !light.registered )
                continue;

            // The matrix the cone test reads. @0x5e9de0 takes the entity's
            // world matrix untouched when Rotation Speed is zero, and
            // otherwise pre-rotates it about Rotation Axis by phase*2*pi
            // (VuMatrix_rotateAxisAngle_pre @0x55a350 = R * M).
            float matrix[ 16 ]{};
            std::memcpy( matrix, light.world, sizeof( matrix ) );
            if ( light.rotation_speed != 0.0f )
                spin( matrix, light.phase * 6.2831855f, light.axis );

            const float position[ 3 ] = { matrix[ 12 ], matrix[ 13 ], matrix[ 14 ] };
            const float to_eye[ 3 ] = {
                eye[ 0 ] - position[ 0 ], eye[ 1 ] - position[ 1 ], eye[ 2 ] - position[ 2 ] };
            const float distance_squared =
                to_eye[ 0 ] * to_eye[ 0 ] + to_eye[ 1 ] * to_eye[ 1 ] + to_eye[ 2 ] * to_eye[ 2 ];
            if ( distance_squared > light.draw_distance * light.draw_distance )
                continue;

            // The cone. Note that BOTH clamps are authored at a different
            // float: the inner one at 3.1415925 and the outer at 3.1415927.
            // DO NOT UNIFY - @0x61d1a0 loads two distinct constants.
            const float half_cone = light.cone * 0.5f;
            const float outer_cone = light.penumbra + half_cone;
            const float inner = clamp( std::fmin( half_cone, outer_cone ), 0.0f, 3.1415925f );
            const float outer = clamp( std::fmax( half_cone, outer_cone ), 0.0f, 3.1415927f );

            const float length = std::sqrt( distance_squared );
            if ( length <= 0.0f )
                continue;
            const float inverse = 1.0f / length;
            // Row 1 of the matrix is the corona's facing axis.
            float facing = matrix[ 4 ] * ( to_eye[ 0 ] * inverse )
                + matrix[ 5 ] * ( to_eye[ 1 ] * inverse )
                + matrix[ 6 ] * ( to_eye[ 2 ] * inverse );
            if ( light.back_light )
                facing = std::fabs( facing );
            if ( facing > 1.0f )
                facing = 1.0f;
            if ( facing < -1.0f )
                facing = -1.0f;
            const float angle = std::acos( facing );
            if ( outer <= angle )
                continue;

            float scale = 1.0f;
            if ( angle > inner )
                scale = ( outer - angle ) / ( outer - inner );

            // The alpha is the Texture Color's own alpha, scaled by the cone
            // falloff, then QUANTISED TO A BYTE - retail stores it back into
            // the VuColor. The quantisation is what makes a wide penumbra step
            // rather than ramp, so it is kept.
            int alpha = static_cast< int >( light.color[ 3 ] * 255.0f * scale );
            if ( alpha <= 0 )
                continue;

            light.request = true;   // 0x61D3FA, before the visibility test
            if ( light.visibility <= 0.0f )
                continue;

            // "Texture Size Screen Space" makes Texture Size a PERCENTAGE of
            // the viewport height at the corona's distance - the 0.0099999998
            // in @0x61d1a0 is the percent->fraction constant. The divisor is
            // the camera's vertical projection scale (cam+68).
            // SUBSTITUTION, flagged: c_world cannot ask the renderer for its
            // projection, so the race viewport's own vertical field of view
            // (1.05 rad, workspace/core/scene/viewport.hxx) stands in for it.
            float sprite = light.size;
            if ( light.screen_space )
                sprite = ( length * 2.0f / m_projection_y ) * ( light.size * 0.0099999998f );

            // The distance fade. Retail divides by Fade Distance with no
            // guard; a zero would make this inf and the byte store garbage, so
            // the guard below is ours - it cannot change a level that authors
            // a fade at all.
            if ( light.fade_distance > 0.0f && length > light.draw_distance - light.fade_distance )
                alpha = static_cast< int >(
                    ( light.draw_distance - length ) * static_cast< float >( alpha ) / light.fade_distance );
            if ( alpha <= 0 )
                continue;
            if ( alpha > 255 )
                alpha = 255;

            // The card's roll: Rotation Offset plus the screen-space x of the
            // corona times Rotation Amount. @0x5e8410 writes
            // 0.5 - (0.5 - (proj00 * clip.x / clip.z) * 0.5), which is
            // algebraically ndc_x * 0.5 - transliterated as it stands below so
            // the shape stays recognisable against the disassembly.
            const float roll = light.rotation_offset
                + ( 0.5f - ( 0.5f - screen_x( position, eye, forward, right ) * 0.5f ) ) * light.rotation_amount;
            const float cosine = std::cos( roll );
            const float sine = std::sin( roll );

            // rotationY(roll) * scale(sprite,1,sprite) * cameraBasis, with the
            // retail quad's local XZ plane carried onto our card's XY plane -
            // the same four corners and the same UVs, relabelled.
            float placed[ 16 ]{};
            for ( int index = 0; index < 3; ++index )
            {
                placed[ index ] = ( right[ index ] * cosine - up[ index ] * sine ) * sprite;
                placed[ 4 + index ] = ( right[ index ] * sine + up[ index ] * cosine ) * sprite;
            }
            placed[ 10 ] = 1.0f;
            placed[ 12 ] = position[ 0 ];
            placed[ 13 ] = position[ 1 ];
            placed[ 14 ] = position[ 2 ];
            placed[ 15 ] = 1.0f;

            // The record 0x61D1A0 hands the sort: the sprite on retail's layer 18 (additive, no depth).
            if ( m_sink )
            {
                std::uint32_t colour = static_cast< std::uint32_t >( alpha ) << 24;
                for ( int k = 0; k < 3; ++k )
                    colour |= static_cast< std::uint32_t >( std::lrintf( light.color[ k ] * 255.0f ) ) << ( 8 * k );
                m_sink( position, colour, sprite, light.rotation_offset, light.rotation_amount, light.texture );
                ++m_drawn;
                continue;
            }
            const float color[ 4 ] = {
                light.color[ 0 ], light.color[ 1 ], light.color[ 2 ],
                static_cast< float >( alpha ) / 255.0f };
            const int texture_id = texture( light.texture );
            // The blended sprite layer: depth-tested, no depth write, two
            // sided, unlit, alpha straight off the colour - which is what
            // retail asks for when it submits the corona at sort layer 18
            // with gColor and nothing else. No near fade (the first two
            // surface slots), no tile texture.
            const float surface[ 4 ] = { 0.0f, 0.0f, 0.0f, 1.0f };
            const float bias[ 2 ] = { 0.0f, 0.0f };
            c_engine::place( m_card, texture_id, 0, 6, placed, color, 11, surface, -1, 0.0f, bias );
            ++m_drawn;
            // Say the first time each corona survives the distance and cone
            // tests. The census above proves they were PARSED; this is the
            // only thing that proves one reached the screen, and a corona
            // that never appears in this list is a gap the audit can chase.
            if ( m_reported.insert( light.path ).second )
                LOG_SUCCESS( "corona %s first drawn at %.0f units, alpha %d, sprite %.1f",
                    light.path.c_str( ), length, alpha, sprite );
        }
    }

private:
    static float clamp( float value, float low, float high )
    {
        if ( value < low )
            return low;
        if ( value > high )
            return high;
        return value;
    }

    // VuMatrix_rotateAxisAngle_pre @0x55a350: build Rodrigues from the axis,
    // then matrix = rotation * matrix.
    static void spin( float* matrix, float angle, const float* axis )
    {
        const float c = std::cos( angle );
        const float s = std::sin( angle );
        const float t = 1.0f - c;
        const float x = axis[ 0 ];
        const float y = axis[ 1 ];
        const float z = axis[ 2 ];

        float rotation[ 16 ]{};
        rotation[ 0 ] = t * x * x + c;
        rotation[ 1 ] = t * x * y + s * z;
        rotation[ 2 ] = t * x * z - s * y;
        rotation[ 4 ] = t * x * y - s * z;
        rotation[ 5 ] = t * y * y + c;
        rotation[ 6 ] = t * y * z + s * x;
        rotation[ 8 ] = t * x * z + s * y;
        rotation[ 9 ] = t * y * z - s * x;
        rotation[ 10 ] = t * z * z + c;
        rotation[ 15 ] = 1.0f;

        // math_multiply( out, a, b ) stores b * a in the row-major layout the
        // entity walk uses, so the arguments read backwards here: this is
        // matrix = rotation * matrix, the same order update_rotators() uses
        // for a LOCAL rotation.
        float product[ 16 ]{};
        math_multiply( product, matrix, rotation );
        std::memcpy( matrix, product, 16 * sizeof( float ) );
    }

    // ndc x of a world point. Retail reads it out of the view matrix at
    // 0x759510 and the projection's [0][0] at 0x7594D0; here the view basis is
    // the one the card is built in and the projection scale is the race
    // viewport's. FLAGGED with the screen-space size above - same assumption.
    float screen_x( const float* position, const float* eye,
        const float* forward, const float* right ) const
    {
        const float delta[ 3 ] = {
            position[ 0 ] - eye[ 0 ], position[ 1 ] - eye[ 1 ], position[ 2 ] - eye[ 2 ] };
        const float depth = forward[ 0 ] * delta[ 0 ] + forward[ 1 ] * delta[ 1 ] + forward[ 2 ] * delta[ 2 ];
        if ( depth <= 0.001f )
            return 0.0f;
        const float lateral = right[ 0 ] * delta[ 0 ] + right[ 1 ] * delta[ 1 ] + right[ 2 ] * delta[ 2 ];
        return m_projection_x * lateral / depth;
    }

    // The quad from @0x5e8410: four corners at +-0.5 with UVs (0,1) (1,1)
    // (0,0) (1,0), two triangles. Retail lays it in XZ with y = 0 because its
    // billboard basis puts forward on row 1; ours lies in XY for the same
    // reason the pfx card does. Identical geometry, relabelled axes.
    static int card( )
    {
        mesh_vertex_t vertices[ 4 ]{};
        const float corner[ 4 ][ 2 ] = { { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.5f }, { -0.5f, 0.5f } };
        const float uv[ 4 ][ 2 ] = { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } };
        for ( int index = 0; index < 4; ++index )
        {
            vertices[ index ].position[ 0 ] = corner[ index ][ 0 ];
            vertices[ index ].position[ 1 ] = corner[ index ][ 1 ];
            vertices[ index ].normal[ 2 ] = 1.0f;
            vertices[ index ].uv[ 0 ] = uv[ index ][ 0 ];
            vertices[ index ].uv[ 1 ] = uv[ index ][ 1 ];
            vertices[ index ].color[ 0 ] = 1.0f;
            vertices[ index ].color[ 1 ] = 1.0f;
            vertices[ index ].color[ 2 ] = 1.0f;
            vertices[ index ].color[ 3 ] = 1.0f;
        }
        const unsigned short indices[ 6 ] = { 0, 1, 2, 0, 2, 3 };
        return c_engine::geometry( vertices, 4, indices, 6 );
    }

    std::vector< corona_t > m_lights;
    std::set< std::string > m_reported;
    int m_card = -1;
    int m_drawn = 0;
    // 1.05 rad vertical fov and a 16:9 viewport, from the race viewport in
    // workspace/core/scene/viewport.hxx. Both only scale the roll and the
    // screen-space sprite size; see the flags above.
    float m_projection_y = 1.0f / 0.5783742f;
    float m_projection_x = 1.0f / ( 0.5783742f * 1.7777778f );
};
