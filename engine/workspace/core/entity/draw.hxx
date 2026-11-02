#pragma once

#include <workspace/core/entity/property.hxx>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

// The model-draw components' property targets: Vu3dDrawStaticModelComponent
// (vtable 0x7519AC) and Vu3dDrawAnimatedModelComponent (vtable 0x758240). The
// property set - names, classes, registration order - is each component's
// node list (+4, next +0x10) as its ctor leaves it, captured by running the
// prop types' create fns under emulation (hydro-tools census/createprobe.py);
// the defaults are the targets straight after create (oracle case
// load_prop_models). Every class is a verified load: bool VuBasicProperty
// (0x737450), float (0x737484), VuColor (0x739DA8), VuAssetNameProperty
// (0x74D690).
//
// A Model Asset change notifies the component's watcher, which loads the
// model through the asset factory (0x5340B0) and rebuilds the draw state
// against the graphics device (0x5B09E0, g_vumetrod3d11gfx) - the rendering
// side, a hook: the renderer loads the named model itself.
struct model_draw_t
{
    bool animated;
    std::uint8_t reflect;
    std::uint8_t shadow;
    std::string model_asset;
    std::string lod1_model_asset;      // static only
    std::string lod2_model_asset;      // static only
    std::string reflection_model_asset; // static only
    std::uint32_t color;               // VuColor bytes R, G, B, A
    float draw_distance;
    float lod0_draw_distance;          // static only
    float lod1_draw_distance;          // static only
    float rejection_scale_modifier;    // static only
    std::uint8_t dynamic_lighting;
};

inline float draw_bits( std::uint32_t b )
{
    float f;
    std::memcpy( &f, &b, sizeof( f ) );
    return f;
}

// The values a fresh create leaves (load_prop_models' depth -1 rows).
inline void model_draw_construct( model_draw_t& c, bool animated )
{
    c.animated = animated;
    c.reflect = 0;
    c.shadow = 1;
    c.model_asset.clear( );
    c.lod1_model_asset.clear( );
    c.lod2_model_asset.clear( );
    c.reflection_model_asset.clear( );
    c.color = 0xFFFFFFFFu;
    c.draw_distance = draw_bits( 0x7F7FFFFFu );
    c.lod0_draw_distance = draw_bits( 0x7F7FFFFFu );
    c.lod1_draw_distance = draw_bits( 0x7F7FFFFFu );
    c.rejection_scale_modifier = draw_bits( 0x3F800000u );
    c.dynamic_lighting = 0;
}

// The component's property loads in node order, each reset first to its
// node's default `d` (the ctor's, until a template mark makes the loaded
// values the defaults).
inline void model_draw_load( model_draw_t& c, const value_t& properties, const model_draw_t& d )
{
    property_bool( properties, "Reflect", c.reflect, d.reflect );
    property_bool( properties, "Shadow", c.shadow, d.shadow );
    property_string( properties, "Model Asset", c.model_asset, d.model_asset );
    if ( !c.animated )
    {
        property_string( properties, "LOD 1 Model Asset", c.lod1_model_asset, d.lod1_model_asset );
        property_string( properties, "LOD 2 Model Asset", c.lod2_model_asset, d.lod2_model_asset );
        property_string( properties, "Reflection Model Asset", c.reflection_model_asset, d.reflection_model_asset );
    }
    property_color( properties, "Color", c.color, d.color );
    property_float( properties, "Draw Distance", c.draw_distance, d.draw_distance, false );
    if ( !c.animated )
    {
        property_float( properties, "LOD 0 Draw Distance", c.lod0_draw_distance, d.lod0_draw_distance, false );
        property_float( properties, "LOD 1 Draw Distance", c.lod1_draw_distance, d.lod1_draw_distance, false );
        property_float( properties, "Rejection Scale Modifier", c.rejection_scale_modifier, d.rejection_scale_modifier, false );
    }
    property_bool( properties, "Dynamic Lighting", c.dynamic_lighting, d.dynamic_lighting );
}

// Vu3dDrawStaticModelComponent's draw (0x5B0730, installed as its VuMethod1
// draw at 0x5B03C9) up to the model instance's draw 0x520130 - the GPU side,
// a hook. `aabb_min` / `aabb_max` are the component's world aabb (+0x24 /
// +0x34, which 0x531D10 builds from the model's aabb and the draw matrix
// +0x13C), `eye` the draw params' +0, `reflection_pass` their +0x18 byte and
// `skip` their +0x2D byte; `fade` is the component's +0x17C (1.0 from the ctor;
// its other writer is unread). Returns the model instance drawn - -1 none, 0
// LOD 0 (+0xBC, the Model Asset), 1 LOD 1 (+0xDC), 2 LOD 2 (+0xFC), 3 the
// reflection model (+0x11C) - and fills what the component writes into it
// first: +0x14 the Rejection Scale Modifier, +0x18 the colour (each byte x
// fade, fistp - round to nearest - low byte kept), +0x1C Dynamic Lighting.
struct static_draw_t
{
    int instance;
    float rejection;
    std::uint32_t color;
    std::uint8_t dynamic_lighting;
};

// Vu3dDrawAnimatedModelComponent's draw 0x61DB60 up to the model instance's
// draw 0x532AD0 (on +0x68 with the matrix +0x104 - the GPU side, a hook). The
// draw params' +0x2D byte (`skip`) set: nothing at all. Otherwise the distance
// from `eye` to the world aabb's centre (+0x24 / +0x34) against Draw Distance
// (+0x60): drawn only when Draw Distance^2 is STRICTLY greater (comiss / jbe -
// unordered draws nothing), first copying Color (+0x5C) to +0xF8 and Dynamic
// Lighting (+0x64) to +0xFC. No LOD, no reflection model, no fade.
struct animated_draw_t
{
    bool drawn;
    std::uint32_t color;
    std::uint8_t dynamic_lighting;
};

inline animated_draw_t animated_model_select( const model_draw_t& c, const float* aabb_min, const float* aabb_max, const float* eye, bool skip )
{
    animated_draw_t out{ false, 0u, 0 };
    if ( skip )
        return out;
    const float half = draw_bits( 0x3F000000u );
    const float dx = ( aabb_max[ 0 ] + aabb_min[ 0 ] ) * half - eye[ 0 ];
    const float dy = ( aabb_max[ 1 ] + aabb_min[ 1 ] ) * half - eye[ 1 ];
    const float dz = ( aabb_max[ 2 ] + aabb_min[ 2 ] ) * half - eye[ 2 ];
    const float dist2 = ( dy * dy + dx * dx ) + dz * dz;
    if ( !( c.draw_distance * c.draw_distance > dist2 ) )
        return out;
    out.drawn = true;
    out.color = c.color;
    out.dynamic_lighting = c.dynamic_lighting;
    return out;
}

// Vu3dDrawAnimatedModelComponent's shadow 0x61DC00 up to 0x533060: the same distance test, no skip, nothing copied.
inline bool animated_model_casts( const model_draw_t& c, const float* aabb_min, const float* aabb_max, const float* eye )
{
    return animated_model_select( c, aabb_min, aabb_max, eye, false ).drawn;
}

// The Draw Distance / LOD choice 0x5B0730 shares with the two below: -1 at or beyond the draw distance.
inline int static_model_lod( const model_draw_t& c, const float* aabb_min, const float* aabb_max, const float* eye, bool last )
{
    const float half = draw_bits( 0x3F000000u );
    const float dx = ( aabb_max[ 0 ] + aabb_min[ 0 ] ) * half - eye[ 0 ];
    const float dy = ( aabb_max[ 1 ] + aabb_min[ 1 ] ) * half - eye[ 1 ];
    const float dz = ( aabb_max[ 2 ] + aabb_min[ 2 ] ) * half - eye[ 2 ];
    const float dist2 = ( dy * dy + dx * dx ) + dz * dz;
    if ( dist2 >= c.draw_distance * c.draw_distance )
        return -1;
    if ( last )
        return 3;
    if ( c.lod0_draw_distance * c.lod0_draw_distance > dist2 )
        return 0;
    return c.lod1_draw_distance * c.lod1_draw_distance > dist2 ? 1 : 2;
}

// Vu3dDrawStaticModelComponent's shadow 0x5B08D0 up to 0x5201B0: `reflection_pass` the shadow params' +0x24, `skip` their +0x38 (read after the choice). Writes only +0x14.
inline int static_model_select_shadow( const model_draw_t& c, const float* aabb_min, const float* aabb_max, const float* eye, bool reflection_pass, bool skip )
{
    const int instance = static_model_lod( c, aabb_min, aabb_max, eye, reflection_pass );
    return skip ? -1 : instance;
}

// 0x5B0600 up to 0x520130, the VuGfxDrawInfoParams path: `force` (its +7): LOD 0, no distance at all; otherwise the same choice, no reflection model. Colour raw, no fade.
inline static_draw_t static_model_select_info( const model_draw_t& c, const float* aabb_min, const float* aabb_max, const float* eye, bool force )
{
    return static_draw_t{ force ? 0 : static_model_lod( c, aabb_min, aabb_max, eye, false ), c.rejection_scale_modifier, c.color, c.dynamic_lighting };
}

inline static_draw_t static_model_select( const model_draw_t& c, const float* aabb_min, const float* aabb_max, const float* eye,
    bool reflection_pass, bool skip, float fade )
{
    static_draw_t out{ -1, c.rejection_scale_modifier, 0u, c.dynamic_lighting };
    const float half = draw_bits( 0x3F000000u );
    const float dx = ( aabb_max[ 0 ] + aabb_min[ 0 ] ) * half - eye[ 0 ];
    const float dy = ( aabb_max[ 1 ] + aabb_min[ 1 ] ) * half - eye[ 1 ];
    const float dz = ( aabb_max[ 2 ] + aabb_min[ 2 ] ) * half - eye[ 2 ];
    const float dist2 = ( dy * dy + dx * dx ) + dz * dz;
    // comiss dist2, Draw Distance^2; jae: at or beyond it, nothing is drawn.
    if ( dist2 >= c.draw_distance * c.draw_distance )
        return out;
    int instance;
    if ( reflection_pass )
        instance = 3;
    else if ( c.lod0_draw_distance * c.lod0_draw_distance > dist2 )
        instance = 0;
    else if ( c.lod1_draw_distance * c.lod1_draw_distance > dist2 )
        instance = 1;
    else
        instance = 2;
    if ( skip )
        return out;
    out.instance = instance;
    std::uint32_t packed = 0;
    for ( int lane = 0; lane < 4; ++lane )
    {
        const float scaled = static_cast< float >( static_cast< std::int32_t >( ( c.color >> ( 8 * lane ) ) & 0xFF ) ) * fade;
        const std::int32_t rounded = static_cast< std::int32_t >( std::nearbyint( scaled ) );
        packed |= ( static_cast< std::uint32_t >( rounded ) & 0xFFu ) << ( 8 * lane );
    }
    out.color = packed;
    return out;
}