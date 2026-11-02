#pragma once

#include <workspace/core/boat/entity.hxx>
#include <workspace/core/boat/hull.hxx>
#include <workspace/core/boat/math.hxx>
#include <workspace/core/boat/sections.hxx>
#include <workspace/core/boat/state.hxx>
#include <workspace/core/collision/concave.hxx>
#include <workspace/core/collision/world.hxx>
#include <workspace/core/dynamics/body.hxx>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

// VuBoatDB_loadAllSections 0x420A70 (thiscall boat; ret), recreated from
// retail's disassembly - the load postLoad 0x41CFF0 runs after building the
// driver (then 0x421670). In retail's order:
//   slot 16 0x421870 (boat_teardown); the boat's own section reader slot 15
//   0x4205A0 on BoatDB["VuDBAsset"]["Default"], then on the boat's section
//   (["VuDBAsset"][the Boat property's text +0x8C], std::map find 0x4F22B0;
//   absent -> the static null container); the three model instances +0x2A4 /
//   +0x2A8 / +0x2AC loaded from +0xC4 / +0xDC / +0xF4 (0x5322C0 x2, 0x520080 -
//   rendering assets: HOOKS); the aabb +0x270 / +0x280 zeroed; the collision
//   mesh (below); the compound; mass, inertia, shape, surface, gravity and the
//   driver's CCD flags on the body; the camera, hull (+ 0x5243A0), engine,
//   animation, pfx and damage sections (Default's, then the boat's); the
//   instigator; the skin pass 0x421490 (a HOOK - its own unit).
// The `movq` pairs that store +0x278 / +0x288 / +0x298 carry a stack word the
// function never writes into +0x27C / +0x28C / +0x29C: UNKNOWN, left as they
// are here (the check masks them).

struct boat_load_hooks_t
{
    void* user = nullptr;
    // g 0x759004's slot 10 doesAssetExist / slot 7 create (0x421F40 / 0x413110,
    // 0x5261A0 / 0x526250) and slot 9 release: the asset factory.
    bool ( *exists )( void* user, const char* type, const std::string& name ) = nullptr;
    std::uint32_t ( *create )( void* user, const char* type, const std::string& name ) = nullptr;
    bool ( *release )( void* user, std::uint32_t asset ) = nullptr;
    // A recorded call into code not recreated: its address, `this` by name and
    // the argument (a container's name or a string's text).
    void ( *call )( void* user, std::uint32_t address, const char* self, const std::string& arg ) = nullptr;
    // g_vudynamicsimpl's +0x38 surface-type table, slot 2(name) -> the id byte.
    std::uint8_t ( *surface )( void* user, const std::string& name ) = nullptr;
    // A striding mesh's arrays for a VuCollisionMeshAsset (getLockedReadOnly
    // VertexIndexBase 0x527C50 reads asset +0x58 / +0x5C / +0x64 / +0x68), and a
    // VuFluidsMeshAsset's +0x7C / +0x88 counts.
    bool ( *mesh )( void* user, std::uint32_t asset, striding_mesh_t& out ) = nullptr;
    bool ( *fluid_counts )( void* user, std::uint32_t asset, int& vertices, int& edges ) = nullptr;
    // The VuFluidsMeshAsset's +0x58..+0x77 aabb (min 4, max 4) for 0x526090.
    // meshes.hxx's boat_mesh_library_t answers all three from Base.apf.
    bool ( *fluid_aabb )( void* user, std::uint32_t asset, float* aabb ) = nullptr;
};

struct boat_load_t
{
    std::uint8_t* boat;            // 0x520
    std::uint32_t boat_address;    // for the shape's +0x80 (boat + 0x240)
    vu_rigid_body_t* body;         // boat +0x21C
    std::uint8_t* camera;          // +0x224, 0x308
    std::uint8_t* hull;            // +0x228, 0x1F0
    std::uint8_t* solver;          // hull +0x1CC, 0x104
    boat_engine_t* engine;         // +0x22C
    std::uint8_t* animation;       // +0x230, 0xDC
    std::uint32_t pfx_owner_flags; // [[boat +0x234] +0x30] +0x14
    std::uint8_t* damage;          // +0x238, 0xD0
    std::uint8_t* instigator;      // +0x70, 0x2C
    boat_strings_t boat_strings, hull_strings, animation_strings;
    std::string damage_model;      // damage +0x78's text (0x411EF0, a hook, would set it)
    const value_t* boatdb;         // the BoatDB document's root
    boat_driver_t driver;
    bool dynamics;                 // g_vudynamicsimpl 0x75900C non-null
    float gravity[ 4 ];            // its world's +0xE0 (slot 7 0x4F8660)
    // Retail's allocations, in: the shape (btAlignedAlloc 0x90), the compound
    // (0x60), its root leaf and the solver's two record arrays (0x526130).
    std::uint32_t shape_address, compound_address, root_address, array_address[ 2 ];
    boat_load_hooks_t hooks;
    // Out: the hull shape (its 0x90-byte image too), its mesh, the compound.
    striding_mesh_t mesh{};
    convex_shape_t shape{};
    std::uint8_t shape_image[ 0x90 ]{};
    bool has_shape = false;
    compound_shape_t compound;
    // The Pfx step (0x419EF0 / 0x419D70 x2 / 0x419FF0) handed over WHOLE: set, it is called with Default's and the
    // boat's Pfx sections, the driver class and the owner's flag bit 2 - bind it to boat/effects.hxx boat_pfx_fill
    // on the controller (boat +0x234) and its boat_pfx_fill_hooks_t. Null: the step stays the four logged calls.
    // 0x411EF0 on one Damage section (world/pieces.hxx): the pieces and +0x78 / +0x90.
    bool ( *damage_read )( void* user, const value_t& section ) = nullptr;
    bool ( *pfx_fill )( void* user, const value_t& fallback, const value_t& own, const boat_driver_t& driver, bool create ) = nullptr;
    void* pfx_user = nullptr;
    // 0x5322C0 on the model instance +0x2A4 (world/rig.hxx): null, the logged call only.
    void ( *model0 )( void* user, const std::string& name ) = nullptr;
    void* model_user = nullptr;
};

inline std::uint32_t load_word( const std::uint8_t* b, int at )
{
    std::uint32_t v;
    std::memcpy( &v, b + at, 4 );
    return v;
}

// 0x536180 (thiscall name; the trigger types std::vector<std::string> at
// 0x75913C that start-up's 0x4D9260 fills: "Human Boat", "AI Boat", "Boat",
// "Camera"): 1 << the index of the first equal name, else 0.
inline std::uint32_t load_instigator_bit( const char* name )
{
    static const char* const types[ 4 ] = { "Human Boat", "AI Boat", "Boat", "Camera" };
    for ( int k = 0; k < 4; ++k )
        if ( std::strcmp( types[ k ], name ) == 0 )
            return 1u << k;
    return 0;
}

// 0x526130, the record array resize (data +0, size +4, capacity +8): past the
// capacity, the new capacity max(n, capacity + capacity / 2) (`cdq; sub; sar`)
// - a new _aligned_malloc(40 * it, 16) block (retail's address, in) that the
// old records are copied into before the old block is freed; then size = n.
inline void load_resize( std::uint8_t* solver, int at, int n, std::uint32_t address )
{
    const std::int32_t capacity = static_cast< std::int32_t >( load_word( solver, at + 8 ) );
    if ( n > capacity )
    {
        std::int32_t grown = capacity + capacity / 2;
        if ( n > grown )
            grown = n;
        if ( grown > capacity )
        {
            image_put32( solver, at, address );
            image_put32( solver, at + 8, static_cast< std::uint32_t >( grown ) );
        }
    }
    image_put32( solver, at + 4, static_cast< std::uint32_t >( n ) );
}

// 0x5243A0 (thiscall solver; the fluids mesh name; retn 4): the old asset
// (+0) released through the factory and +0 = 0; exists -> +0 = create; +0x3C
// = +0x48 = 0; with an asset, the vertex records (+0x38) resized to asset
// +0x7C and the edge records (+0x44) to +0x88, both memset 0 over 40 x size
// (their contents: zero, not kept here).
inline bool load_fluid_mesh( boat_load_t& io, const std::string& name )
{
    std::uint8_t* s = io.solver;
    if ( load_word( s, 0 ) )
    {
        if ( !io.hooks.release || !io.hooks.release( io.hooks.user, load_word( s, 0 ) ) )
            return false;
        image_put32( s, 0, 0 );
    }
    if ( !io.hooks.exists || !io.hooks.create )
        return false;
    if ( io.hooks.exists( io.hooks.user, "VuFluidsMeshAsset", name ) )
        image_put32( s, 0, io.hooks.create( io.hooks.user, "VuFluidsMeshAsset", name ) );
    image_put32( s, 0x3C, 0 );
    image_put32( s, 0x48, 0 );
    const std::uint32_t asset = load_word( s, 0 );
    if ( asset )
    {
        int vertices = 0, edges = 0;
        if ( !io.hooks.fluid_counts || !io.hooks.fluid_counts( io.hooks.user, asset, vertices, edges ) )
            return false;
        load_resize( s, 0x38, vertices, io.array_address[ 0 ] );
        load_resize( s, 0x44, edges, io.array_address[ 1 ] );
    }
    return true;
}

// The hull shape 0x420A70 builds inline: btAlignedAlloc(0x90, 16), +8 = +0x40
// = 0, the base ctor 0x54B650 (+4 0x23, +8 0, scaling (1, 1, 1, 0), margin
// 0.04, +0x40 0, the cached aabb min +0x50 (1, 1, 1, 0) / max +0x60 (-1, -1, -1, 0), +0x70 0),
// vtable 0x74CEBC, +0x80 the boat's VuStridingMesh (+0x240), +4 = 3, then
// recalcLocalAabb 0x54B700 (+0x70 = 1, the cached aabb's xyz).
inline void load_shape( boat_load_t& io )
{
    std::uint8_t* b = io.shape_image;
    image_put32( b, 8, 0 );
    image_put32( b, 0x40, 0 );
    image_put32( b, 4, 0x23 );
    for ( int at : { 0x10, 0x14, 0x18 } )
        image_put32( b, at, 0x3F800000u );
    image_put32( b, 0x1C, 0 );
    image_put32( b, 0x30, 0x3D23D70Au );
    image_put32( b, 0, 0x0074DFFCu );
    for ( int at : { 0x50, 0x54, 0x58 } )
        image_put32( b, at, 0x3F800000u );
    image_put32( b, 0x5C, 0 );
    for ( int at : { 0x60, 0x64, 0x68 } )
        image_put32( b, at, 0xBF800000u );
    image_put32( b, 0x6C, 0 );
    b[ 0x70 ] = 0;
    image_put32( b, 0, 0x0074CEBCu );
    image_put32( b, 0x80, io.boat_address + 0x240 );
    image_put32( b, 4, 3 );
    // The model: shape_init_triangle_mesh_hull's fields (scaling 1, margin
    // 0.04, type 3) over the mesh; its recalc is the same 0x54B700.
    io.shape = convex_shape_t{};
    io.shape.type = 3;
    io.shape.local_scaling[ 0 ] = io.shape.local_scaling[ 1 ] = io.shape.local_scaling[ 2 ] = 1.0f;
    io.shape.margin = support_bits( 0x3D23D70Au );
    io.shape.mesh = &io.mesh;
    shape_recalc_local_aabb( io.shape );
    b[ 0x70 ] = 1;
    for ( int k = 0; k < 3; ++k )
    {
        std::memcpy( b + 0x50 + 4 * k, &io.shape.local_aabb_min[ k ], 4 );   // m_localAabbMin +0x50
        std::memcpy( b + 0x60 + 4 * k, &io.shape.local_aabb_max[ k ], 4 );   // m_localAabbMax +0x60
    }
}

inline bool boat_load_sections( boat_load_t& io )
{
    std::uint8_t* b = io.boat;
    auto call = [ & ]( std::uint32_t address, const char* self, const std::string& arg )
    {
        if ( io.hooks.call )
            io.hooks.call( io.hooks.user, address, self, arg );
    };
    // Slot 16 0x421870.
    {
        float* centre = reinterpret_cast< float* >( b + 0x290 );
        float* mass = reinterpret_cast< float* >( b + 0x134 );
        std::uint32_t* handle = reinterpret_cast< std::uint32_t* >( b + 0x2B0 );
        const std::uint32_t shape = load_word( reinterpret_cast< const std::uint8_t* >( io.body ), 0xCC );
        if ( shape )
            return false;   // a second load: the old shape's teardown (compound children) is not modelled here
        boat_teardown_hooks_t th;
        struct ctx_t { boat_load_t* io; };
        ctx_t ctx{ &io };
        th.user = &ctx;
        th.release = []( void* user, uint32_t h ) { ctx_t& c = *static_cast< ctx_t* >( user );
                                                     return c.io->hooks.release && c.io->hooks.release( c.io->hooks.user, h ); };
        boat_teardown_t t{ centre, *mass, *handle, *io.body, nullptr, 0, nullptr, th };
        if ( !boat_teardown( t ) )
            return false;
    }
    const value_t& db = member( *io.boatdb, "VuDBAsset" );
    std::string name = "";
    {
        // The Boat property's value (+0x8C), an inline std::string here (the
        // case's names are under 16 characters).
        const std::uint32_t size = load_word( b, 0x8C + 16 );
        if ( load_word( b, 0x8C + 20 ) >= 16 )
            return false;
        name.assign( reinterpret_cast< const char* >( b + 0x8C ), size );
    }
    const value_t& fallback = member( db, "Default" );
    const value_t& own = member( db, name.c_str( ) );
    boat_read_default( b, io.boat_strings, fallback, io.driver );
    boat_read_default( b, io.boat_strings, own, io.driver );
    call( 0x5322C0, "model0", io.boat_strings[ 0xC4 ] );
    if ( io.model0 )
        io.model0( io.model_user, io.boat_strings[ 0xC4 ] );
    call( 0x5322C0, "model1", io.boat_strings[ 0xDC ] );
    call( 0x520080, "model2", io.boat_strings[ 0xF4 ] );
    for ( int at : { 0x270, 0x274, 0x278, 0x280, 0x284, 0x288 } )
        image_put32( b, at, 0 );

    float lo[ 4 ] = {}, hi[ 4 ] = {};
    if ( !io.hooks.exists || !io.hooks.create )
        return false;
    if ( io.hooks.exists( io.hooks.user, "VuCollisionMeshAsset", io.boat_strings[ 0x148 ] ) )
    {
        const std::uint32_t asset = io.hooks.create( io.hooks.user, "VuCollisionMeshAsset", io.boat_strings[ 0x148 ] );
        image_put32( b, 0x2B0, asset );
        image_put32( b, 0x260, asset );
        // The VuStridingMesh at +0x240: m_scaling +0x250, the asset +0x260.
        if ( !io.hooks.mesh || !io.hooks.mesh( io.hooks.user, asset, io.mesh ) )
            return false;
        std::memcpy( io.mesh.scaling, b + 0x250, 16 );
        load_shape( io );
        io.has_shape = true;
        // getAabb (slot 1) under 0x422030's identity btTransform.
        static const float identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 };
        if ( !shape_get_aabb( io.shape, identity, lo, hi ) )
            return false;
        for ( int k = 0; k < 3; ++k )
        {
            state_put_f( b, 0x290 + 4 * k, ( hi[ k ] + lo[ k ] ) * 0.5f );
            state_put_f( b, 0x270 + 4 * k, lo[ k ] );
            state_put_f( b, 0x280 + 4 * k, hi[ k ] );
        }
    }
    // The compound (0x5269C0) and its one child under identity . translation(-centre),
    // converted by 0x410C30.
    io.compound = compound_shape_t{};
    {
        float id[ 16 ], tr[ 16 ], m[ 16 ], t[ 16 ];
        vu_identity( id );
        const float neg[ 3 ] = { support_negate( state_f( b, 0x290 ) ), support_negate( state_f( b, 0x294 ) ),
                                 support_negate( state_f( b, 0x298 ) ) };
        hull_translation( tr, neg );
        hull_multiply( id, m, tr );
        const int column[ 3 ][ 3 ] = { { 0, 4, 8 }, { 1, 5, 9 }, { 2, 6, 10 } };
        for ( int row = 0; row < 3; ++row )
        {
            for ( int k = 0; k < 3; ++k )
                t[ 4 * row + k ] = m[ column[ row ][ k ] ];
            t[ 4 * row + 3 ] = 0.0f;
        }
        t[ 12 ] = m[ 12 ];
        t[ 13 ] = m[ 13 ];
        t[ 14 ] = m[ 14 ];
        t[ 15 ] = 0.0f;
        if ( io.has_shape && compound_add_child( io.compound, t, io.shape ) != 1 )
            return false;
    }
    // Extents; below FLT_EPSILON (0x6F7584, `comiss eps, x; jbe`) along x, the
    // fluid solver's 0x526090: its mesh asset's +0x58..+0x77 aabb, or zeros.
    float ex = state_f( b, 0x280 ) - state_f( b, 0x270 );
    float ey = state_f( b, 0x284 ) - state_f( b, 0x274 );
    float ez = state_f( b, 0x288 ) - state_f( b, 0x278 );
    if ( _mm_comigt_ss( _mm_set_ss( support_bits( 0x34000000u ) ), _mm_set_ss( ex ) ) )
    {
        // 0x526090: the asset's +0x58 min / +0x68 max (fluids_mesh_asset_t's
        // aabb), else zeros; the extents are max - min per lane.
        float box[ 8 ] = { };
        if ( load_word( io.solver, 0 ) )
            if ( !io.hooks.fluid_aabb || !io.hooks.fluid_aabb( io.hooks.user, load_word( io.solver, 0 ), box ) )
                return false;
        ex = box[ 4 ] - box[ 0 ];
        ey = box[ 5 ] - box[ 1 ];
        ez = box[ 6 ] - box[ 2 ];
    }
    {
        const float mass = state_f( b, 0x134 );
        const float twelfth = support_bits( 0x3DAAAAABu );
        const float x2 = ex * ex, y2 = ey * ey, z2 = ez * ez;
        const float xz = ( ( x2 + z2 ) * mass ) * twelfth;
        const float yz = ( ( y2 + z2 ) * mass ) * twelfth;
        const float xy = ( ( x2 + y2 ) * mass ) * twelfth;
        const float factor = state_f( b, 0x144 );
        const float inertia[ 4 ] = { factor * yz, factor * xz, factor * xy, 0.0f };
        rigid_set_mass_props( io.body->body, mass, inertia );
    }
    // setCollisionShape (VuRigidBody slot 2 0x50FB90): +0xCC = +0xD4 = the compound.
    body_word( *io.body, 0xCC ) = io.compound_address;
    body_word( *io.body, 0xD4 ) = io.compound_address;
    if ( io.dynamics )
    {
        if ( !io.hooks.surface )
            return false;
        body_byte( *io.body, 0x2C8 ) = io.hooks.surface( io.hooks.user, io.boat_strings[ 0x11C ] );
        const float g[ 4 ] = { io.gravity[ 0 ], io.gravity[ 1 ], io.gravity[ 2 ], 0.0f };
        rigid_set_gravity( io.body->body, g );
    }
    // The driver's slots 12 (human) / 13 (AI): body +0x2D0 = 0x40 | 0x20; a
    // human's CCD threshold and swept radius (+0x100 / +0x104) = half the z extent.
    body_word( *io.body, 0x2D0 ) = ( io.driver.slot[ 0 ] ? 0x40u : 0u ) | ( io.driver.slot[ 1 ] ? 0x20u : 0u );
    if ( io.driver.slot[ 0 ] )
    {
        const float r = ( state_f( b, 0x288 ) - state_f( b, 0x278 ) ) * 0.5f;
        std::memcpy( reinterpret_cast< std::uint8_t* >( io.body ) + 0x100, &r, 4 );
        std::memcpy( reinterpret_cast< std::uint8_t* >( io.body ) + 0x104, &r, 4 );
    }
    boat_read_camera( io.camera, member( fallback, "Camera" ) );
    boat_read_camera( io.camera, member( own, "Camera" ) );
    boat_read_hull( io.hull, io.hull_strings, member( fallback, "Hull" ), io.driver );
    boat_read_hull( io.hull, io.hull_strings, member( own, "Hull" ), io.driver );
    if ( !load_fluid_mesh( io, io.hull_strings[ 4 ] ) )
        return false;
    const std::uint32_t flags = load_word( b, 0x14 );
    boat_load_engine( *io.engine, member( own, "Engine" ), member( fallback, "Engine" ), io.driver, flags );
    if ( flags & 4 )
        return false;   // the engine's FMOD events (a hook) on a loaded boat: not modelled here
    // Animation: 0x4108C0 (the four refcounted assets +4..+0x10 released -
    // their dtors are hooks: non-null is not modelled), the reader twice, then
    // with the owner's flag bit 2 the asset loads 0x410610 (a hook).
    for ( int at = 4; at <= 0x10; at += 4 )
        if ( load_word( io.animation, at ) )
            return false;
    boat_read_animation( io.animation, io.animation_strings, member( fallback, "Animation" ), io.driver );
    boat_read_animation( io.animation, io.animation_strings, member( own, "Animation" ), io.driver );
    if ( flags & 4 )
        call( 0x410610, "animation+0x0", "" );
    // Pfx: 0x419EF0, the reader 0x419D70 twice (the model skeleton's
    // findNodeByName - a hook), 0x419FF0 behind its owner's bit 2: boat/effects.hxx boat_pfx_fill.
    if ( io.pfx_fill )
    {
        if ( !io.pfx_fill( io.pfx_user, member( fallback, "Pfx" ), member( own, "Pfx" ), io.driver, ( io.pfx_owner_flags & 4 ) != 0 ) )
            return false;
    }
    else
    {
        call( 0x419EF0, "pfx", "" );
        call( 0x419D70, "pfx", "Default/Pfx" );
        call( 0x419D70, "pfx", name == "Default" ? "Default/Pfx" : "boat/Pfx" );
        if ( io.pfx_owner_flags & 4 )
            call( 0x419FF0, "pfx", "" );
    }
    // Damage: 0x412170 (each piece's scalar deleting dtor - a hook - then the
    // count +0xA0 = 0), the debris reader 0x411EF0 twice (world/pieces.hxx,
    // io.damage_read), the model 0x520080(+4, +0x78), +0xCC = count > 0.
    if ( static_cast< std::int32_t >( load_word( io.damage, 0xA0 ) ) > 0 )
        return false;
    image_put32( io.damage, 0xA0, 0 );
    call( 0x411EF0, "damage+0x0", "Default/Damage" );
    if ( io.damage_read && !io.damage_read( io.pfx_user, member( fallback, "Damage" ) ) )
        return false;
    call( 0x411EF0, "damage+0x0", name == "Default" ? "Default/Damage" : "boat/Damage" );
    if ( io.damage_read && !io.damage_read( io.pfx_user, member( name == "Default" ? fallback : own, "Damage" ) ) )
        return false;
    call( 0x520080, "damage+0x4", io.damage_model );
    io.damage[ 0xCC ] = static_cast< std::int32_t >( load_word( io.damage, 0xA0 ) ) > 0 ? 1 : 0;
    // The instigator: "Boat" | "Human Boat" (slot 12) | "AI Boat" (slot 13)
    // through 0x536180; 0x531E10 - registered (+0x28) is not modelled - +0x10 =
    // the mask; then +0x24 = half the x extent, +0x14.. = the centre, +0x20 = 0
    // (a stack word the setMassProps call's inertia w lane zeroed).
    {
        std::uint32_t mask = load_instigator_bit( "Boat" );
        if ( io.driver.slot[ 0 ] )
            mask |= load_instigator_bit( "Human Boat" );
        if ( io.driver.slot[ 1 ] )
            mask |= load_instigator_bit( "AI Boat" );
        if ( io.instigator[ 0x28 ] )
            return false;
        image_put32( io.instigator, 0x10, mask );
        state_put_f( io.instigator, 0x24, ( state_f( b, 0x280 ) - state_f( b, 0x270 ) ) * 0.5f );
        state_put_f( io.instigator, 0x14, ( state_f( b, 0x270 ) + state_f( b, 0x280 ) ) * 0.5f );
        state_put_f( io.instigator, 0x18, ( state_f( b, 0x284 ) + state_f( b, 0x274 ) ) * 0.5f );
        state_put_f( io.instigator, 0x1C, ( state_f( b, 0x278 ) + state_f( b, 0x288 ) ) * 0.5f );
        image_put32( io.instigator, 0x20, 0 );
    }
    call( 0x421490, "boat+0x0", "" );
    return true;
}

// The transform-changed delegate 0x421670 (thiscall boat; ret): the transform
// component's +0xC0 VuMethod0 and postLoad's last call. W = translation(boat
// +0x290) . the component's world (+0x50; 0x410AB0, 0x4078C0); the body's
// linear and angular velocities (+0x140 / +0x150) zeroed; setCenterOfMass
// Transform 0x50E660 with W as a btTransform (0x410C30); camera +0x2C4 = 1;
// with the instigator registered (+0x28) the instigator manager's 0x536350
// (a HOOK); with +0x3D4 set, +0x3D8 = W's origin row and +0x3E8 = (atan2(W6,
// W10), asinf(clamp(-W2)), atan2(W1, W0)), +0x3F4 a stack word never written
// (UNKNOWN, left). The clamp is `minss 1.0, x` then `maxss -1.0, m` (a NaN
// passes both). SUBSTITUTION: the CRT's __libm_sse2_atan2 0x631CD0 (on
// doubles) / __libm_sse2_asinf 0x631CCA are served by std::atan2 / std::asin
// - their equality is what the oracle case measures: asinf matches retail as the DOUBLE asin rounded once (the
// float std::asin missed 2 of 59). A NaN W2 is UNKNOWN: retail's atan2 after the NaN compares returned the
// default NaN under the oracle (the sticky MXCSR invalid flag, likely).
struct boat_transform_hooks_t
{
    void* user = nullptr;
    void ( *instigator_update )( void* user ) = nullptr;   // g 0x759064's 0x536350(instigator)
};

inline void boat_transform_changed( std::uint8_t* boat, const float* world, vu_rigid_body_t& body, std::uint8_t* camera,
    const std::uint8_t* instigator, const boat_transform_hooks_t& hooks )
{
    float tr[ 16 ], w[ 16 ];
    hull_translation( tr, reinterpret_cast< const float* >( boat + 0x290 ) );
    hull_multiply( tr, w, world );
    std::memset( body.body.linear_velocity, 0, 16 );
    std::memset( body.body.angular_velocity, 0, 16 );
    float basis[ 12 ], origin[ 4 ];
    const int column[ 3 ][ 3 ] = { { 0, 4, 8 }, { 1, 5, 9 }, { 2, 6, 10 } };
    for ( int row = 0; row < 3; ++row )
    {
        for ( int k = 0; k < 3; ++k )
            basis[ 4 * row + k ] = w[ column[ row ][ k ] ];
        basis[ 4 * row + 3 ] = 0.0f;
    }
    origin[ 0 ] = w[ 12 ];
    origin[ 1 ] = w[ 13 ];
    origin[ 2 ] = w[ 14 ];
    origin[ 3 ] = 0.0f;
    body_set_center_of_mass_transform( body.body, basis, origin );
    camera[ 0x2C4 ] = 1;
    if ( instigator[ 0x28 ] && hooks.instigator_update )
        hooks.instigator_update( hooks.user );
    if ( !boat[ 0x3D4 ] )
        return;
    std::memcpy( boat + 0x3D8, w + 12, 16 );
    const float v = support_negate( w[ 2 ] );
    const float m = _mm_cvtss_f32( _mm_min_ss( _mm_set_ss( 1.0f ), _mm_set_ss( v ) ) );
    const float r = _mm_cvtss_f32( _mm_max_ss( _mm_set_ss( -1.0f ), _mm_set_ss( m ) ) );
    const float e[ 3 ] = { static_cast< float >( std::atan2( static_cast< double >( w[ 6 ] ), static_cast< double >( w[ 10 ] ) ) ),
                           static_cast< float >( std::asin( static_cast< double >( r ) ) ),
                           static_cast< float >( std::atan2( static_cast< double >( w[ 1 ] ), static_cast< double >( w[ 0 ] ) ) ) };
    std::memcpy( boat + 0x3E8, e, 12 );
}
