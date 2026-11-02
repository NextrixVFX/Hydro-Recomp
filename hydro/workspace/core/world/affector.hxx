#pragma once

// VuForceAffectorEntity live in the race (force.hxx holds the entity's verified logic, case force_affector / force_watch; this
// file is its dynamics binding). 71 of them are authored: Area51 5, City 21, Pack01Track01 40 (its "Out of bounds force" folder),
// Pack01Track02 5; a census of every template finds no VuAttachEntity or motion naming one, and the class has no motion component,
// so nothing in a shipped level moves one and force_affector_watch has no caller (hooks.update_aabb is set for it).
//
// The ctor 0x439300: a btBoxShape (+0x80, 0x547D80, half extents 0x6F7B00 = 1) as the ghost's shape (+0xD0, 0x548E00: vt+8
// setCollisionShape) and +0xD8 of the ghost |= 4 (no contact response, 0x439734) over btCollisionObject's ctor 0x5470A0 defaults
// flags 1, activation 1; internal type 4. The Build tick 0x439ED0 is only the dev draw (0x758F93, off): not registered.

#include <workspace/core/world/bodies.hxx>
#include <workspace/core/world/dynamics.hxx>
#include <workspace/core/world/force.hxx>

#include <cstring>
#include <memory>
#include <vector>

struct force_live_t
{
    entity_t* entity = nullptr;
    force_affector_t* value = nullptr;
    const float* world = nullptr;       // the transform's +0x50
    const float* scale = nullptr;       // the transform's +0xB0
    race_dynamics_t* dyn = nullptr;
    convex_shape_t box{ };              // +0x80
    vu_rigid_body_t body{ };            // the ghost's btCollisionObject fields, as every Bullet path here reads them
    ghost_object_t ghost{ };            // +0xD0
    std::vector< force_body_t > scratch;
    force_affector_hooks_t hooks{ };
    bool failed = false;
    int pushes = 0;                     // ours, a log count: substeps in which the step changed a body's force
};

// The step 0x439970 over the ghost's overlapping objects (+0x1E4 of the entity): force_body_t is the fields it reads and writes.
inline void race_force_step( force_live_t& l )
{
    l.scratch.resize( l.ghost.overlapping.size( ) );
    for ( std::size_t i = 0; i < l.scratch.size( ); ++i )
    {
        const solver_rigid_t& b = *l.ghost.overlapping[ i ]->body.object;
        force_body_t& f = l.scratch[ i ];
        f.type = static_cast< std::uint32_t >( b.internal_type );
        std::memcpy( f.position, b.origin, 12 );
        std::memcpy( f.velocity, b.linear_velocity, 12 );
        f.inverse_mass = b.inverse_mass;
        std::memcpy( f.factor, b.linear_factor, 12 );
        std::memcpy( f.force, b.total_force, 12 );
    }
    force_affector_step( *l.value, l.box.local_scaling, l.scratch.data( ), static_cast< int >( l.scratch.size( ) ) );
    for ( std::size_t i = 0; i < l.scratch.size( ); ++i )
    {
        float* out = l.ghost.overlapping[ i ]->body.object->total_force;
        if ( std::memcmp( out, l.scratch[ i ].force, 12 ) != 0 )
            ++l.pushes;
        std::memcpy( out, l.scratch[ i ].force, 12 );
    }
}

inline void race_force_bind( force_live_t& l )
{
    const float half[ 3 ] = { 1.0f, 1.0f, 1.0f };   // 0x6F7B00
    bodies_box( l.box, half );
    solver_rigid_t& r = l.body.body;
    r.collision_flags = 1 | 4;
    r.activation_state = 1;
    r.internal_type = 4;
    r.friction = 0.5f;
    l.ghost.body = concave_body_t{ r.basis, &l.box, nullptr, &r, nullptr, rigid_bits( 0x5D5E0B6Bu ) };   // 0x5470A0's +0xC4
    l.ghost.interpolation_transform = r.interpolation_basis;
    force_affector_hooks_t& h = l.hooks;
    h.user = &l;
    h.set_scaling = []( void* u, const float* s ) { bodies_scale_primitive( static_cast< force_live_t* >( u )->box, s ); };
    h.add_object = []( void* u, std::uint32_t group, std::uint32_t mask ) {
        force_live_t& x = *static_cast< force_live_t* >( u );
        std::memcpy( x.body.body.basis, x.value->ghost, 64 );   // the ghost's +0x10 transform (the entity's +0xE0)
        if ( world_add_collision_object( x.dyn->world, x.dyn->collision, x.ghost, static_cast< short >( group ), static_cast< short >( mask ) ) != 1 )
            x.failed = true;
    };
    h.remove_object = []( void* u ) {
        force_live_t& x = *static_cast< force_live_t* >( u );
        if ( world_remove_collision_object( x.dyn->world, x.dyn->collision, x.ghost ) != 1 )
            x.failed = true;
    };
    h.step_callback = []( void* u, bool add ) {
        force_live_t& x = *static_cast< force_live_t* >( u );
        if ( add )
        {
            race_step_callback_t cb;
            cb.slot0 = []( float, bool ) {};   // 0x410B30: ret 8 (vtable 0x73A88C)
            cb.slot1 = [ &x ]( float ) { race_force_step( x ); };
            x.dyn->callbacks[ &x.scratch ] = cb;
            if ( !vu_dynamics_add_step_callback( x.dyn->impl, &x.scratch ) )
                x.failed = true;
        }
        else if ( !vu_dynamics_remove_step_callback( x.dyn->impl, &x.scratch ) )
            x.failed = true;
    };
    h.update_aabb = []( void* u ) {
        force_live_t& x = *static_cast< force_live_t* >( u );
        std::memcpy( x.body.body.basis, x.value->ghost, 64 );
        if ( update_single_aabb( x.dyn->collision.aabb, x.ghost ) < 0 )
            x.failed = true;
    };
}
