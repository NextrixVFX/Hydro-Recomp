#pragma once

#include <workspace/core/assets/tree.hxx>
#include <workspace/core/entity/json.hxx>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// VuScriptComponent's save (vtable 0x74C3F0): slot 6 0x503EC0, the whole component, and slot 7 0x504450, the difference
// from the template (the baseline 0x5042A0 took, repository.hxx vu_script_component_snapshot), into a document. On plain
// records: each connection's target is already the two texts retail reads through it.

struct persist_link_t
{
    std::string entity;  // 0x500BF0 of the target's entity: [ [ link + 0x4C or +0x14 ] + 8 ]'s long name
    std::string name;    // the target plug's name +0xC, or the ref's text [ +0xC ]
};

struct persist_plug_t
{
    std::string name;                     // +0xC
    std::vector< persist_link_t > links;   // +0x50
    std::int32_t baseline = 0;            // +0x5C: links the template already had
};

struct persist_ref_t
{
    std::string name;      // [ +0xC ]
    bool bound = false;    // +0x18 != 0
    std::string entity;    // when bound: the target component's entity's long name
    bool baseline = false; // +0x1C: bound in the template
};

struct persist_component_t
{
    std::vector< persist_plug_t > plugs;          // +0x10
    std::vector< persist_ref_t > refs;            // +0x1C
    std::vector< persist_link_t > ref_links;      // +0x28
    std::int32_t ref_baseline = 0;               // +0x34
    float base_x = 0.0f, base_y = 0.0f;          // +0x38 / +0x3C
    std::int32_t base_width = 0, width = 0;      // +0x44 / +0x48
    float x = 0.0f, y = 0.0f;                    // +0x4C / +0x50
};

// 0x4FBB10 + tag 5: what every array access here starts with (anything else is cleared into an empty array).
inline value_t& persist_array( value_t& j )
{
    if ( j.tag != 5 )
    {
        json_clear( j );
        j.tag = 5;
        j.kind = value_t::kind_t::list;
    }
    return j;
}

// 0x4FBC10 element-or-grow.
inline value_t& persist_element( value_t& j, std::size_t index )
{
    persist_array( j );
    if ( index >= j.list.size( ) )
        j.list.resize( index + 1 );
    return j.list[ index ];
}

// 0x4FC500 of a member, through the find-or-insert the sort walks with (a non-object element becomes an empty object, a
// missing member a null one): the text, "" for anything else.
inline std::string persist_key( value_t& element, const char* key )
{
    return json_as_cstring( json_member( element, key ) );
}

// 0x502720 ( plug, out ): the links past the baseline appended, then the whole array sorted by EntityName then PlugName:
// retail's exchange sort, i against every j after it, swapping when the pair is out of order (strcmp).
inline void persist_save_links( const persist_plug_t& plug, value_t& out )
{
    for ( std::size_t k = static_cast< std::size_t >( plug.baseline ); k < plug.links.size( ); ++k )
    {
        const std::size_t at = out.tag == 5 ? out.list.size( ) : 0;
        value_t& entry = persist_element( out, at );
        json_set_string( json_member( entry, "EntityName" ), plug.links[ k ].entity );
        json_set_string( json_member( entry, "PlugName" ), plug.links[ k ].name );
    }
    const auto size = [ & ]( ) { return out.tag == 5 ? out.list.size( ) : std::size_t{ 0 }; };
    for ( std::size_t i = 0; i < size( ); ++i )
    {
        for ( std::size_t j = i + 1; j < size( ); ++j )
        {
            const std::string name_j = persist_key( persist_element( out, j ), "EntityName" );
            const std::string name_i = persist_key( persist_element( out, i ), "EntityName" );
            const int entity = std::strcmp( name_i.c_str( ), name_j.c_str( ) );
            const std::string plug_j = persist_key( persist_element( out, j ), "PlugName" );
            const std::string plug_i = persist_key( persist_element( out, i ), "PlugName" );
            const int name = std::strcmp( plug_i.c_str( ), plug_j.c_str( ) );
            if ( entity > 0 || ( entity == 0 && name > 0 ) )
                std::swap( out.list[ i ], out.list[ j ] );
        }
    }
}

// 0x504CC0 ( component, out ): the ref connections past the baseline appended: EntityName, RefName.
inline void persist_save_ref_links( const persist_component_t& c, value_t& out )
{
    for ( std::size_t k = static_cast< std::size_t >( c.ref_baseline ); k < c.ref_links.size( ); ++k )
    {
        const std::size_t at = out.tag == 5 ? out.list.size( ) : 0;
        value_t& entry = persist_element( out, at );
        json_set_string( json_member( entry, "EntityName" ), c.ref_links[ k ].entity );
        json_set_string( json_member( entry, "RefName" ), c.ref_links[ k ].name );
    }
}

// What slots 6 and 7 share: "Plugs" made for any plug at all, a plug's own member only when it has a link; "Refs" made for any
// ref, a ref's text only when it is bound now and was not in the template.
inline void persist_save_plugs( const persist_component_t& c, value_t& out )
{
    for ( const persist_plug_t& plug : c.plugs )
    {
        value_t& plugs = json_member( out, "Plugs" );
        if ( !plug.links.empty( ) )
            persist_save_links( plug, json_member( json_member( plugs, plug.name ), "Connections" ) );
    }
    for ( const persist_ref_t& ref : c.refs )
    {
        value_t& refs = json_member( out, "Refs" );
        if ( ref.bound && !ref.baseline )
            json_set_string( json_member( refs, ref.name ), ref.entity );
    }
}

inline void persist_save_width( const persist_component_t& c, value_t& out )
{
    if ( c.width != c.base_width )
        json_set_int( json_member( out, "Width" ), c.width );
}

inline void persist_save_position( const persist_component_t& c, value_t& out )
{
    value_t& position = json_member( out, "Position" );
    json_set_float( json_member( position, "X" ), c.x );
    json_set_float( json_member( position, "Y" ), c.y );
}

// 0x503EC0: the position when it is not the origin, the ref connections when there are any.
inline void persist_save_all( const persist_component_t& c, value_t& out )
{
    persist_save_width( c, out );
    if ( c.x * c.x + c.y * c.y > 0.0f )
        persist_save_position( c, out );
    persist_save_plugs( c, out );
    if ( !c.ref_links.empty( ) )
        persist_save_ref_links( c, json_member( out, "RefConnections" ) );
}

// 0x504450: the position when it moved from the baseline; no ref connections.
inline void persist_save_changes( const persist_component_t& c, value_t& out )
{
    persist_save_width( c, out );
    if ( c.x != c.base_x || c.y != c.base_y )
        persist_save_position( c, out );
    persist_save_plugs( c, out );
}
