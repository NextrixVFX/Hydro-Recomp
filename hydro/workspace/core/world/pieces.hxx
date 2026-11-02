#pragma once

// VuBoatDamageController's load (0x411EF0) and VuPiece (0x412270), recreated from the disassembly, with the boat damage's draw 0x4121C0.
//   0x411EF0 (thiscall debris, the Damage section; retn 4): "Breakable Mesh Asset" into +0x78 (only a tag 4 node), "Breakable Fade Time" into +0x90, then per "Pieces" entry
//   operator new( 0x230 ), 0x4120F0 (boat/damage.hxx piece_clear), 0x412270( the entry, the boat body's surface byte +0x2C8 ), pushed on +0x9C / +0xA0. Run twice: Default, then the boat.
//   0x412270: the reads Asset, Mass, CenterOfMass (+0x1B0), Buoyancy, SkinFricion (retail's spelling), MinRelLinVel +0x1C0, MaxRelLinVel +0x1D0, MinRelAngVel +0x1E0, MaxRelAngVel +0x1F0,
//   Pfx +0x210; +8 identity; the model 0x520080 on +0x48; +0x50 = +0x68; 0x5243A0 on the solver +0x78; the collision asset 0x413110 at +0x1A0; +0x1B0 += the asset's aabb centre (x 0.5 0x6F765C);
//   the convex hull over the VuStridingMesh +0x180 (+0x204, the boat's own shape: boat/load.hxx load_shape), the compound +0x200 holding it under identity with origin -(+0x1B0);
//   calculateLocalInertia( Mass ) (vt+0x1C); 0x4131E0( Mass, the piece as motion state, the compound, inertia ), linear / angular damping 0.1 (0x3DCCCCCD, cinfo +0x70 / +0x74), sleep
//   thresholds 0 (+0x80 / +0x84); the VuRigidBody 0x527B80( cinfo, group 0, mask 0x100, -1 ) at +0x208; its +0x2C8 = the surface byte; setGravity inline (rigid_set_gravity);
//   solver +0x14 = +0x18 = Mass / the fluids asset's +0xA0 / Buoyancy; solver +0x20 .. +0x28 = SkinFricion three times.
// The asset's fluids and collision meshes share the name: the piece's Asset.
// ORDER OURS (flagged): setGravity runs at the first launch, not at the load: the session's dynamics do not exist yet when the sections load.
// ORDER OURS (flagged): the models are looked up at the first launch through the world's debris_model, as race_session_debris_bind does, not at the load: nothing reads them before.
// HOOKS: the piece's pfx (g_vupfximpl) is the runtime's; the draw of a piece's pfx (slot vt+0x38) is the runtime's own walk.

#include <workspace/core/boat/damage.hxx>
#include <workspace/core/boat/effects.hxx>
#include <workspace/core/boat/load.hxx>
#include <workspace/core/boat/sections.hxx>
#include <workspace/core/world/bodies.hxx>
#include <workspace/core/world/meshes.hxx>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

struct race_piece_t
{
    std::vector< std::uint8_t > image = std::vector< std::uint8_t >( 0x230, 0 );
    level_body_t body;
    fluids_mesh_t mesh;
    fluid_solver_t solver{ };
    std::string asset, pfx;
    gfx_static_model_t model;
    const float* model_aabb = nullptr;
    bool bound = false;     // the model looked up
    bool stepping = false;  // the step callback and the motion set exist
};

// The boat's debris (boat +0x238): what the images' words stand for.
struct race_boat_debris_t
{
    std::vector< std::unique_ptr< race_piece_t > > pieces;   // +0x9C / +0xA0
    std::vector< boat_debris_part_t > parts;                 // +4 +0x6C / +0x70
    std::vector< gfx_scene_piece_t > drawn;
    gfx_static_model_t model;                                // +4
    bool bound = false;
};

// 0x412270 on one "Pieces" entry. False (with `why`): a path that stops the load, UNKNOWN.
inline bool race_piece_load( race_piece_t& p, const value_t& e, boat_load_t& io, const boat_mesh_library_t& lib, std::string& why )
{
    std::uint8_t* i = p.image.data( );
    piece_clear( i );
    {
        const auto ctor = boat_5240c0( );
        std::memcpy( i + 0x78, ctor->image, 0x104 );
        for ( const int at : { 0x34, 0x38, 0x44 } )    // heap words the model does not read (the records live in `solver`)
            image_put32( i + 0x78, at, 0 );
    }
    if ( member( e, "Asset" ).tag == 4 )
        p.asset = member( e, "Asset" ).text;
    float mass = 0.0f, buoyancy = 0.0f, skin = 0.0f;
    {
        std::uint8_t scratch[ 12 ] = { };
        section_float( scratch, 0, member( e, "Mass" ) );
        section_float( scratch, 4, member( e, "Buoyancy" ) );
        section_float( scratch, 8, member( e, "SkinFricion" ) );
        std::memcpy( &mass, scratch, 4 );
        std::memcpy( &buoyancy, scratch + 4, 4 );
        std::memcpy( &skin, scratch + 8, 4 );
    }
    section_vec3( i, 0x1B0, member( e, "CenterOfMass" ) );
    section_vec3( i, 0x1C0, member( e, "MinRelLinVel" ) );
    section_vec3( i, 0x1D0, member( e, "MaxRelLinVel" ) );
    section_vec3( i, 0x1E0, member( e, "MinRelAngVel" ) );
    section_vec3( i, 0x1F0, member( e, "MaxRelAngVel" ) );
    if ( member( e, "Pfx" ).tag == 4 )
        p.pfx = member( e, "Pfx" ).text;
    float id[ 16 ];
    vu_identity( id );
    std::memcpy( i + 8, id, 64 );

    // 0x5243A0, on the solver's image and the model solver.
    if ( !io.hooks.exists || !io.hooks.create )
    {
        why = "0x412270: no asset factory";
        return false;
    }
    if ( io.hooks.exists( io.hooks.user, "VuFluidsMeshAsset", p.asset ) )
        image_put32( i + 0x78, 0, io.hooks.create( io.hooks.user, "VuFluidsMeshAsset", p.asset ) );
    const fluids_mesh_asset_t* fa = load_word( i + 0x78, 0 ) ? const_cast< boat_mesh_library_t& >( lib ).fluids_of( load_word( i + 0x78, 0 ) ) : nullptr;
    if ( !fa )
    {
        why = "0x412270: the piece '" + p.asset + "' has no fluids asset: the +0xA0 read would fault - UNKNOWN";
        return false;
    }
    std::memcpy( p.mesh.aabb_min, fa->aabb, 16 );
    std::memcpy( p.mesh.aabb_max, fa->aabb + 4, 16 );
    p.mesh.verts.resize( 4 * static_cast< std::size_t >( fa->vertices.size ) );
    std::memcpy( p.mesh.verts.data( ), fa->vertices.bytes.data( ), 16 * static_cast< std::size_t >( fa->vertices.size ) );
    p.mesh.edges.resize( 2 * static_cast< std::size_t >( fa->edges.size ) );
    std::memcpy( p.mesh.edges.data( ), fa->edges.bytes.data( ), 8 * static_cast< std::size_t >( fa->edges.size ) );
    p.mesh.triangles.resize( 15 * static_cast< std::size_t >( fa->records.size ) );
    std::memcpy( p.mesh.triangles.data( ), fa->records.bytes.data( ), 60 * static_cast< std::size_t >( fa->records.size ) );
    std::memcpy( &p.mesh.total_area, &fa->w9c, 4 );
    p.solver = fluid_solver_t{ };
    p.solver.mesh = &p.mesh;
    p.solver.vertices.assign( static_cast< std::size_t >( fa->vertices.size ), water_vertex_t{ } );
    p.solver.edges.assign( static_cast< std::size_t >( fa->edges.size ), fluid_edge_t{ } );
    float min_depth;
    std::memcpy( &min_depth, i + 0x78 + 0x5C, 4 );
    p.solver.min_depth = min_depth;

    // 0x413110: no doesAssetExist test before it.
    const std::uint32_t collision = io.hooks.create( io.hooks.user, "VuCollisionMeshAsset", p.asset );
    image_put32( i, 0x1A0, collision );
    const collision_mesh_asset_t* ca = collision ? const_cast< boat_mesh_library_t& >( lib ).collision_of( collision ) : nullptr;
    if ( !ca || !io.hooks.mesh || !io.hooks.mesh( io.hooks.user, collision, p.body.mesh ) )
    {
        why = "0x412270: the piece '" + p.asset + "' has no collision asset: the aabb read would fault - UNKNOWN";
        return false;
    }
    for ( int k = 0; k < 3; ++k )
    {
        const float centre = sse_mul( sse_add( ca->aabb[ k ], ca->aabb[ 4 + k ] ), 0.5f );
        state_put_f( i, 0x1B0 + 4 * k, sse_add( centre, state_f( i, 0x1B0 + 4 * k ) ) );
    }
    const float one[ 4 ] = { 1.0f, 1.0f, 1.0f, 0.0f };
    std::memcpy( p.body.mesh.scaling, one, 16 );

    convex_shape_t& shape = p.body.convex;
    shape = convex_shape_t{ };
    shape.type = 3;
    shape.local_scaling[ 0 ] = shape.local_scaling[ 1 ] = shape.local_scaling[ 2 ] = 1.0f;
    shape.margin = support_bits( 0x3D23D70Au );
    shape.mesh = &p.body.mesh;
    shape_recalc_local_aabb( shape );

    p.body.compound = compound_shape_t{ };
    p.body.has_compound = true;
    const float child[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, support_negate( state_f( i, 0x1B0 ) ), support_negate( state_f( i, 0x1B4 ) ), support_negate( state_f( i, 0x1B8 ) ), 0 };
    if ( compound_add_child( p.body.compound, child, shape ) != 1 )
    {
        why = "0x412270: addChildShape 0x526BF0 refused";
        return false;
    }
    float inertia[ 4 ] = { };
    compound_local_inertia( p.body.compound, mass, inertia );

    boat_4131e0_t cinfo;
    std::uint32_t mass_bits;
    std::memcpy( &mass_bits, &mass, 4 );
    boat_4131e0( cinfo, mass_bits, 1u, 0, reinterpret_cast< const std::uint8_t* >( inertia ) );
    image_put32( cinfo.image, 0x70, 0x3DCCCCCDu );
    image_put32( cinfo.image, 0x74, 0x3DCCCCCDu );
    image_put32( cinfo.image, 0x80, 0 );
    image_put32( cinfo.image, 0x84, 0 );
    const body_motion_get_fn get = []( void* user, std::uint32_t, float* transform ) {
        piece_motion_get( static_cast< const std::uint8_t* >( user ), transform );
        return true;
    };
    std::uint32_t unique_id = 0xFFFFFFFFu;
    if ( !vu_rigid_body_construct( p.body.body, cinfo.image, 0, 0x100, unique_id, get, i ) )
    {
        why = "0x412270: the VuRigidBody ctor 0x527B80 refused";
        return false;
    }
    p.body.built = true;
    p.body.collision = collision_object_t{ };
    p.body.collision.body = concave_body_t{ p.body.body.body.basis, nullptr, nullptr, &p.body.body.body, &p.body.compound, rigid_bits( body_word( p.body.body, 0xC4 ) ) };
    std::uint32_t volume_bits = fa->wa0;
    float volume;
    std::memcpy( &volume, &volume_bits, 4 );
    const float density = ( mass / volume ) / buoyancy;
    state_put_f( i, 0x8C, density );
    state_put_f( i, 0x90, density );
    state_put_f( i, 0x98, skin );
    state_put_f( i, 0x9C, skin );
    state_put_f( i, 0xA0, skin );
    return true;
}

// 0x411EF0 on one Damage section. `surface` = the boat body's +0x2C8, stored on each piece's body.
inline bool race_boat_damage_read( race_boat_debris_t& d, const value_t& section, boat_load_t& io, const boat_mesh_library_t& lib, std::uint8_t surface, std::string& why )
{
    if ( member( section, "Breakable Mesh Asset" ).tag == 4 )
        io.damage_model = member( section, "Breakable Mesh Asset" ).text;
    section_float( io.damage, 0x90, member( section, "Breakable Fade Time" ) );
    const value_t& list = member( section, "Pieces" );
    for ( int k = 0; k < section_count( list ); ++k )
    {
        auto p = std::make_unique< race_piece_t >( );
        if ( !race_piece_load( *p, section_at( list, k ), io, lib, why ) )
            return false;
        body_byte( p->body.body, 0x2C8 ) = surface;
        d.pieces.push_back( std::move( p ) );
        image_put32( d.pieces.back( )->image.data( ), 0x208, static_cast< std::uint32_t >( d.pieces.size( ) ) );   // the body word: the index + 1 here
        image_put32( io.damage, 0xA0, static_cast< std::uint32_t >( d.pieces.size( ) ) );
    }
    return true;
}

// 0x4121C0 (thiscall debris; params; retn 4): while +0xC8 > 0 (comiss / jbe) the parts 0x51F540 with the colour's alpha = +0xC8 x 255 by fistp, rgb 0xFFFFFF, at sub +0x18;
// while +0x94 each piece's instance 0x520130 under its matrix +8.
inline void race_boat_debris_draw( race_boat_debris_t& d, const std::uint8_t* debris, gfx_sort_t& sort, const gfx_draw_params_t& p )
{
    const float timer = state_f( debris, 0xC8 );
    if ( timer > 0.0f && d.bound )
    {
        const std::uint32_t a = static_cast< std::uint32_t >( _mm_cvtss_si32( _mm_set_ss( sse_mul( timer, 255.0f ) ) ) ) & 0xFFu;
        d.model.m_colour = 0x00FFFFFFu | ( a << 24 );
        for ( std::size_t i = 0; i < d.parts.size( ); ++i )
            std::memcpy( d.drawn[ i ].m_matrix, d.parts[ i ].image + 0x94, 64 );
        d.model.draw_pieces( sort, d.drawn.data( ), static_cast< int >( d.drawn.size( ) ), p );
    }
    if ( debris[ 0x94 ] == 0 )
        return;
    for ( const std::unique_ptr< race_piece_t >& q : d.pieces )
        if ( q->bound )
            q->model.draw( sort, reinterpret_cast< const float* >( q->image.data( ) + 8 ), p );
}
