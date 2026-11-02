#pragma once

// VU3DDRAWBREAKABLEMODELCOMPONENT'S RUN (water batch 41): what a broken prop's pieces do, recreated from the disassembly. The ctor 0x542D70's properties are game/types.hxx breakable_draw_t;
// the seed 0x51E640 is world/meshes.hxx gfx_pieces_seed; launch 0x51E790 and tick 0x51F1C0 are boat/effects.hxx; the draw 0x543790 + 0x51F540 is world/meshes.hxx gfx_breakable_component_t, case over 0x51F540 in cases/render.py.
//   start, the same in 0x434AB3 (VuBreakablePropEntity) and 0x433923 (VuBreakableDynamicGamePropEntity): 0x51E790( transform world, the kept velocity ), +0xE8 = Fade Time + Fade Delay, +0xE4 = 1, +0xEC = the static component's colour +0xA4, added to the draw manager.
//   0x543640 (Decision, xmm1 dt): while +0xE4 is 1: +0xE8 -= dt; below 0: off the draw manager, +0xE8 = 0, +0xE4 = 2.
//   0x5436B0 (Build, xmm1 dt): while +0xE4 is 1: the aabb from FLT_MAX / -FLT_MAX (its fourth words stack, unread), 0x51F1C0; a changed min x: the aabb into +0x24, the draw manager's update (vt+0x30).
// SUBSTITUTION: retail's draw manager culls the component's aabb in its tree first; race_debris_t is walked as a list (the world's m_draw), so only 0x543790's distance test and 0x51F540's per-piece tests cull.
// The model is taken at the first Decision tick, not at the component's asset load: the same model, one tick later than retail's load, nothing reads it between.

#include <workspace/core/boat/effects.hxx>
#include <workspace/core/world/meshes.hxx>

#include <cstdint>
#include <cstring>
#include <vector>

struct race_debris_t
{
    const breakable_draw_t* m_props = nullptr;
    boat_debris_sub_t m_sub;
    std::vector< boat_debris_part_t > m_parts;
    std::vector< gfx_scene_piece_t > m_pieces;
    gfx_breakable_component_t m_draw;
    std::uint32_t m_state = 0;   // +0xE4: 0 whole, 1 broken and live, 2 done
    bool m_bound = false;

    // The component's loaded properties, the model's roots as 0x51E640 seeds them.
    void bind( const breakable_draw_t& props, const gfx_scene_view_t& view )
    {
        m_props = &props;
        std::memcpy( m_sub.range, props.lin_min, 16 );
        std::memcpy( m_sub.range + 4, props.lin_max, 16 );
        std::memcpy( m_sub.range + 8, props.ang_min, 16 );
        std::memcpy( m_sub.range + 12, props.ang_max, 16 );
        m_sub.range[ 16 ] = props.damping_min;
        m_sub.range[ 17 ] = props.damping_max;
        const std::vector< gfx_piece_seed_t > seeds = gfx_pieces_seed( view );
        m_parts.assign( seeds.size( ), boat_debris_part_t{ } );
        m_pieces.assign( seeds.size( ), gfx_scene_piece_t{ } );
        for ( std::size_t i = 0; i < seeds.size( ); ++i )
        {
            std::memcpy( m_parts[ i ].image + 4, seeds[ i ].m_inverse, 64 );
            std::memcpy( m_parts[ i ].image + 0x44, seeds[ i ].m_pivot, 12 );
            m_parts[ i ].model_aabb = seeds[ i ].m_node->m_aabb;
            m_parts[ i ].model_matrix = seeds[ i ].m_node->m_matrix;
            m_pieces[ i ].m_node = seeds[ i ].m_node;
        }
        gfx_scene_view_attach( m_draw.m_model, view );
        m_draw.m_pieces = m_pieces.data( );
        m_draw.m_count = static_cast< int >( m_pieces.size( ) );
        m_draw.m_draw_distance = props.draw_distance;
        m_draw.m_rejection = props.rejection;
        m_draw.m_lighting = props.dynamic_lighting;
        m_draw.m_fade_time = props.fade_time;
        m_bound = true;
    }

    // The start; `colour` the static component's +0xA4. Unbound (no model named or no renderer): the state alone, as before.
    void start( const float* world, const float* velocity, std::uint32_t colour, fleet_random_t* random )
    {
        if ( m_bound && random )
        {
            boat_debris_parts_launch( m_sub, m_parts, world, velocity, *random );
            m_draw.m_life = m_props->fade_time + m_props->fade_delay;
            m_draw.m_colour = colour;
        }
        m_state = 1;
    }

    // 0x543640.
    void decision( float dt )
    {
        if ( m_state != 1 )
            return;
        m_draw.m_life -= dt;
        if ( 0.0f > m_draw.m_life )
        {
            m_draw.m_life = 0.0f;
            m_state = 2;
        }
    }

    // 0x5436B0.
    void build( float dt )
    {
        if ( m_state != 1 || !m_bound )
            return;
        const std::uint32_t top = 0x7F7FFFFFu, bottom = 0xFF7FFFFFu;
        float aabb[ 8 ] = { };
        for ( int k = 0; k < 3; ++k )
        {
            std::memcpy( &aabb[ k ], &top, 4 );
            std::memcpy( &aabb[ 4 + k ], &bottom, 4 );
        }
        const float unmoved = aabb[ 0 ];
        boat_debris_parts_tick( m_props->gravity, m_parts, aabb, dt );
        if ( aabb[ 0 ] == unmoved )
            return;
        std::memcpy( m_draw.m_min, aabb, 16 );
        std::memcpy( m_draw.m_max, aabb + 4, 16 );
    }

    // The draw manager's walk (mask = Reflect +0x1C), then the method 0x543790.
    void draw( gfx_sort_t& sort, const gfx_draw_params_t& p )
    {
        if ( m_state != 1 || !m_bound || ( m_props->reflect & p.m_reflecting ) != p.m_reflecting )
            return;
        for ( std::size_t i = 0; i < m_parts.size( ); ++i )
            std::memcpy( m_pieces[ i ].m_matrix, m_parts[ i ].image + 0x94, 64 );
        m_draw.draw( sort, p );
    }
};
