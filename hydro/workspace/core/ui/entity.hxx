#pragma once

#include <workspace/core/game/types.hxx>
#include <workspace/core/ui/schema.hxx>

#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// The front end's entities as VuEntity::load leaves them: each type's property
// nodes (schema.hxx, captured from its ctor) loaded by their classes' read
// loads (engine entity/property.hxx, values.hxx). One slot per node holds the
// target and the node's default (+0x14) - the ctor's until a template mark
// copies the loaded value in (every class's vt+0x1C).

struct ui_slot_t
{
    std::uint32_t m_bits[ 4 ] = { };  // a numeric target's raw bits (bool: low byte; int / enum / colour: one word)
    std::string m_text;               // a string node's target
};

// A row's target width in words (a string: 0).
inline int ui_width( ui_kind_t kind )
{
    switch ( kind )
    {
    case ui_kind_t::vector2: return 2;
    case ui_kind_t::vector3:
    case ui_kind_t::rotation: return 3;
    case ui_kind_t::rect: return 4;
    case ui_kind_t::text: return 0;
    default: return 1;
    }
}

struct ui_state_t
{
    std::vector< const ui_property_row_t* > m_rows;  // the nodes in list order
    int m_count = 0;
    std::vector< ui_slot_t > m_value;
    std::vector< ui_slot_t > m_defaults;
};

// OURS, no retail counterpart: the schema table indexed once, by type. A type's
// first row and row count, and its node names to list positions (a repeated name
// keeps its first position, as a walk of the list would find).
struct ui_type_index_t
{
    const ui_property_row_t* m_first = nullptr;
    int m_count = 0;
    std::unordered_map< std::string_view, int > m_slots;
};

inline const ui_type_index_t* ui_type_index( const std::string& cls )
{
    static const std::unordered_map< std::string_view, ui_type_index_t > index = [ ]
    {
        std::unordered_map< std::string_view, ui_type_index_t > out;
        for ( const ui_property_row_t& row : g_ui_property_rows )
        {
            ui_type_index_t& type = out[ row.m_class ];
            if ( !type.m_first )
                type.m_first = &row;
            ++type.m_count;
        }
        for ( auto& entry : out )
            for ( int k = 0; k < entry.second.m_count; ++k )
                entry.second.m_slots.try_emplace( entry.second.m_first[ k ].m_name, k );
        return out;
    }( );
    const auto found = index.find( std::string_view( cls ) );
    return found == index.end( ) ? nullptr : &found->second;
}

inline const ui_property_row_t* ui_rows_for( const std::string& cls, int& count )
{
    const ui_type_index_t* type = ui_type_index( cls );
    count = type ? type->m_count : 0;
    return type ? type->m_first : nullptr;
}

// The state a ctor leaves for these nodes: every target at its node's default.
inline std::shared_ptr< ui_state_t > ui_state_of( std::vector< const ui_property_row_t* > rows )
{
    auto s = std::make_shared< ui_state_t >( );
    s->m_count = static_cast< int >( rows.size( ) );
    s->m_defaults.resize( rows.size( ) );
    for ( std::size_t k = 0; k < rows.size( ); ++k )
    {
        std::memcpy( s->m_defaults[ k ].m_bits, rows[ k ]->m_default, sizeof( rows[ k ]->m_default ) );
        s->m_defaults[ k ].m_text = rows[ k ]->m_text;
    }
    s->m_value = s->m_defaults;
    s->m_rows = std::move( rows );
    return s;
}

inline std::shared_ptr< ui_state_t > ui_state_construct( const std::string& cls )
{
    int count = 0;
    const ui_property_row_t* first = ui_rows_for( cls, count );
    if ( !first )
        return nullptr;
    std::vector< const ui_property_row_t* > rows;
    for ( int k = 0; k < count; ++k )
        rows.push_back( first + k );
    return ui_state_of( std::move( rows ) );
}

// A ctor-made component's nodes (g_ui_component_rows for this owner and component vtable), or none.
inline std::vector< const ui_property_row_t* > ui_component_rows_for( const std::string& owner, std::uint32_t component )
{
    std::vector< const ui_property_row_t* > rows;
    for ( const ui_component_row_t& row : g_ui_component_rows )
        if ( row.m_component == component && owner == row.m_row.m_class )
            rows.push_back( &row.m_row );
    return rows;
}

// One node's vt+8 load on the entity's "Properties": the class's load (reset to
// the default, read, filter, store if it differs). The post-store vt+0x2C (a
// nop, or VuAssetProperty<VuTextureAsset>'s texture load 0x426840 - the
// rendering layer) and the watcher (vt+8 of +0xC, e.g. a text's Font node) are
// hooks.
inline void ui_load_slot( const ui_property_row_t& row, const value_t& properties, ui_slot_t& value, const ui_slot_t& fallback )
{
    float f[ 4 ];
    float d[ 4 ];
    std::memcpy( d, fallback.m_bits, sizeof( d ) );
    switch ( row.m_kind )
    {
    case ui_kind_t::boolean:
    {
        std::uint8_t b = 0;
        property_bool( properties, row.m_name, b, static_cast< std::uint8_t >( fallback.m_bits[ 0 ] ) );
        value.m_bits[ 0 ] = b;
        return;
    }
    case ui_kind_t::real:
    case ui_kind_t::angle:
        property_float( properties, row.m_name, f[ 0 ], d[ 0 ], row.m_kind == ui_kind_t::angle );
        std::memcpy( value.m_bits, f, 4 );
        return;
    case ui_kind_t::percent:
        property_percentage( properties, row.m_name, f[ 0 ], d[ 0 ] );
        std::memcpy( value.m_bits, f, 4 );
        return;
    case ui_kind_t::integer:
    {
        std::int32_t i = 0;
        property_int( properties, row.m_name, i, static_cast< std::int32_t >( fallback.m_bits[ 0 ] ) );
        value.m_bits[ 0 ] = static_cast< std::uint32_t >( i );
        return;
    }
    case ui_kind_t::enumeration:
    {
        const ui_choices_t& table = g_ui_choices[ row.m_choices ];
        std::int32_t i = 0;
        property_enum( properties, row.m_name, i, static_cast< std::int32_t >( fallback.m_bits[ 0 ] ), table.m_first, table.m_count );
        value.m_bits[ 0 ] = static_cast< std::uint32_t >( i );
        return;
    }
    case ui_kind_t::color:
        property_color( properties, row.m_name, value.m_bits[ 0 ], fallback.m_bits[ 0 ] );
        return;
    case ui_kind_t::vector2:
        property_floats( properties, row.m_name, f, d, 2 );
        std::memcpy( value.m_bits, f, 8 );
        return;
    case ui_kind_t::rect:
        property_floats( properties, row.m_name, f, d, 4 );
        std::memcpy( value.m_bits, f, 16 );
        return;
    case ui_kind_t::vector3:
        property_vector3( properties, row.m_name, f, d );
        std::memcpy( value.m_bits, f, 12 );
        return;
    case ui_kind_t::rotation:
        // VuRotation3dProperty (vtable 0x73D24C): the vector3's load / read with the
        // filter 0x45CB50 (each lane x 0x3C8EFA35) - transform.hxx's verified load.
        transform_property( properties, row.m_name, f, d, true );
        std::memcpy( value.m_bits, f, 12 );
        return;
    case ui_kind_t::text:
        property_string( properties, row.m_name, value.m_text, fallback.m_text );
        return;
    }
}

// The world callbacks for the front end: a captured UI type gets its ui_state_t;
// every other type falls through to types.hxx's own (the corona's).
inline std::shared_ptr< void > ui_entity_state_for( const std::string& cls )
{
    if ( auto s = ui_state_construct( cls ) )
        return s;
    return entity_state_for( cls );
}

inline bool ui_is_state( const entity_t& entity )
{
    return entity.m_state && ui_type_index( entity.m_type ) != nullptr;
}

// A loaded entity's property value by name, or null.
inline const ui_slot_t* ui_state_slot( const entity_t& entity, const char* name )
{
    const ui_type_index_t* type = entity.m_state ? ui_type_index( entity.m_type ) : nullptr;
    if ( !type )
        return nullptr;
    const auto found = type->m_slots.find( name );
    if ( found == type->m_slots.end( ) )
        return nullptr;
    return &static_cast< const ui_state_t* >( entity.m_state.get( ) )->m_value[ static_cast< std::size_t >( found->second ) ];
}

// VuEntity::load's "Properties" step: each node in +0x44 order.
inline void ui_entity_load_properties( entity_t& entity, const value_t& properties )
{
    if ( !ui_is_state( entity ) )
    {
        entity_load_properties( entity, properties );
        return;
    }
    ui_state_t* s = static_cast< ui_state_t* >( entity.m_state.get( ) );
    for ( int k = 0; k < s->m_count; ++k )
        ui_load_slot( *s->m_rows[ k ], properties, s->m_value[ k ], s->m_defaults[ k ] );
}

// The template mark's property step: each node's vt+0x1C - default = value.
inline void ui_entity_mark_properties( entity_t& entity )
{
    if ( !ui_is_state( entity ) )
    {
        entity_mark_properties( entity );
        return;
    }
    ui_state_t* s = static_cast< ui_state_t* >( entity.m_state.get( ) );
    s->m_defaults = s->m_value;
}

// The components: entity_components_constructed (types.hxx: the transform, the
// model draws) plus a ui_state_t for every other ctor-made component with
// captured nodes (VuScriptComponent "Enable", VuUIDrawComponent "Depth",
// Vu3dDrawComponent "Reflect" / "Shadow"). Their loads are 0x501F70's /
// 0x502010's node loads; each component's post-load (vt+0xC / vt+0x10) and the
// mark's vt+0x20 are hooks.
inline std::vector< entity_component_t > ui_components_for( const std::string& cls )
{
    std::vector< entity_component_t > out = entity_components_constructed( cls );
    for ( entity_component_t& c : out )
        if ( !c.m_state )
        {
            std::vector< const ui_property_row_t* > rows = ui_component_rows_for( cls, c.m_vtable );
            if ( !rows.empty( ) )
                c.m_state = ui_state_of( std::move( rows ) );
        }
    return out;
}

inline bool ui_is_component_state( const entity_t& owner, const entity_component_t& component )
{
    return component.m_state && component.m_vtable != k_transform_component_vtable &&
           component.m_vtable != k_static_model_component_vtable && component.m_vtable != k_animated_model_component_vtable &&
           !ui_component_rows_for( owner.m_type, component.m_vtable ).empty( );
}

inline void ui_load_component( entity_t& owner, entity_component_t& component, const value_t& properties, bool override )
{
    if ( !ui_is_component_state( owner, component ) )
    {
        entity_load_component( owner, component, properties, override );
        return;
    }
    ui_state_t* s = static_cast< ui_state_t* >( component.m_state.get( ) );
    for ( int k = 0; k < s->m_count; ++k )
        ui_load_slot( *s->m_rows[ k ], properties, s->m_value[ k ], s->m_defaults[ k ] );
}

inline void ui_mark_component( entity_t& owner, entity_component_t& component )
{
    if ( !ui_is_component_state( owner, component ) )
    {
        entity_mark_component( owner, component );
        return;
    }
    ui_state_t* s = static_cast< ui_state_t* >( component.m_state.get( ) );
    s->m_defaults = s->m_value;
}

// The world as the front end's projects load: the registry, the captured ctor
// facts and components (types.hxx), the templates by name, and the UI property
// callbacks above.
inline entity_world_t ui_world( const entity_registry_t& registry, std::function< const value_t*( const std::string& ) > templates )
{
    entity_world_t world;
    world.m_registry = &registry;
    world.m_ctor_for = entity_ctor_for;
    world.m_template = std::move( templates );
    world.m_components_for = ui_components_for;
    world.m_component_load = ui_load_component;
    world.m_component_mark = ui_mark_component;
    world.m_state_for = ui_entity_state_for;
    world.m_properties_load = ui_entity_load_properties;
    world.m_properties_mark = ui_entity_mark_properties;
    return world;
}
