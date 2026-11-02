#pragma once

#include <workspace/core/entity/property.hxx>

#include <cstdint>
#include <cstring>

// VuDynamicLightEntity (vtable 0x7563B0; create 0x5EDE60, ctor 0x5EDF60): the
// members its ten properties are bound to. The ctor sets +0x6C to 1 and builds
// the light at +0x70 (0x5C2F30), whose defaults the rest take; each property's
// default is its target's value at the moment the ctor registered it.
struct dynamic_light_t
{
    std::uint8_t initially_on;     // +0x6C   "Initially On"
    std::uint32_t diffuse;         // +0x94   "Diffuse Color"     (light +0x24)
    std::uint32_t specular;        // +0x98   "Specular Color"    (light +0x28)
    float factor;                  // +0x9C   "Factor"            (light +0x2C)
    float falloff_min;             // +0xA0   "Falloff Range Min" (light +0x30)
    float falloff_max;             // +0xA4   "Falloff Range Max" (light +0x34)
    float cone;                    // +0xA8   "Cone Angle", radians
    float penumbra;                // +0xAC   "Penumbra Angle", radians
    float draw_distance;           // +0xB0   "Draw Distance"     (light +0x40)
    std::uint8_t reflecting;       // +0xB4   "Reflecting"        (light +0x44, a word 0)
};

// The defaults 0x5C2F30 writes (and +0x6C = 1 from 0x5EDF60 itself).
inline void dynamic_light_construct( dynamic_light_t& l )
{
    const auto bits = []( std::uint32_t b )
    {
        float f;
        std::memcpy( &f, &b, sizeof( f ) );
        return f;
    };
    l.initially_on = 1;
    l.diffuse = 0xFFFFFFFFu;
    l.specular = 0xFFFFFFFFu;
    l.factor = bits( 0x3F800000u );
    l.falloff_min = bits( 0x41F00000u );
    l.falloff_max = bits( 0x42480000u );
    l.cone = bits( 0x3FC90FDBu );
    l.penumbra = 0.0f;
    l.draw_distance = bits( 0x7F7FFFFFu );
    l.reflecting = 0;
}

// The ten property loads in the order the ctor appended them to the entity's
// +0x44 list (VuEntity::load 0x5009D0 runs them in that order), each defaulting
// to the value its target held when it was registered.
inline void dynamic_light_load( dynamic_light_t& l, const value_t& properties )
{
    // A later document (the instance over its template) leaves what it does not author: the fallback is the value held now, the first load's being the ctor's.
    const dynamic_light_t at_registration = l;
    property_bool( properties, "Initially On", l.initially_on, at_registration.initially_on );
    property_color( properties, "Diffuse Color", l.diffuse, at_registration.diffuse );
    property_color( properties, "Specular Color", l.specular, at_registration.specular );
    property_float( properties, "Factor", l.factor, at_registration.factor, false );
    property_float( properties, "Falloff Range Min", l.falloff_min, at_registration.falloff_min, false );
    property_float( properties, "Falloff Range Max", l.falloff_max, at_registration.falloff_max, false );
    property_float( properties, "Cone Angle", l.cone, at_registration.cone, true );
    property_float( properties, "Penumbra Angle", l.penumbra, at_registration.penumbra, true );
    property_float( properties, "Draw Distance", l.draw_distance, at_registration.draw_distance, false );
    property_bool( properties, "Reflecting", l.reflecting, at_registration.reflecting );
}
