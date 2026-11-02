#pragma once

#include <workspace/core/world/attach.hxx>
#include <workspace/core/world/rotate.hxx>
#include <workspace/core/world/race.hxx>
#include <workspace/core/world/skeleton.hxx>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

// The level's attach graph over the entity tree: VuAttachEntity (world/attach.hxx) between a parent's attach component and a
// child's VuMotionComponent. Components live here, keyed by entity, created on first use - the retail objects' state, the ctor
// having made nothing else that this reads.
//   0x5F57E0 the first component whose type chain holds 0x736E18: VuOffsetAttachComponent 0x75182C or VuAnimatedAttachComponent 0x75829C;
//   0x5F5810 the first holding 0x736968: VuMotionComponent 0x74E984 (one per entity in every captured class).
// A motion's owner IF slot 0 is `m_moved` (c_world::motion_child by class); slots 1 / 2 are `m_claim`: the props' 0x546D80 / 0x546DB0;
// corona 0x755BC8, static pfx 0x755EB4, audio emitter 0x756EA0 hold the nullsub 0x401560.
// An animated parent's update 0x61E010 reads its instance's +0x18 node matrices: `m_instance` (c_world::prop_instance) answers the
// instance; none (no model): UNKNOWN, the update not run, counted (`unknown( )`).
class c_joints
{
public:
    using moved_t = std::function< void( entity_t& child, const float* matrix, const float* linear, const float* angular ) >;
    using instance_t = std::function< skel_instance_t*( entity_t& parent ) >;

    struct host_t
    {
        entity_t* entity = nullptr;
        bool animated = false;
        skel_instance_t* instance = nullptr;   // the animated component's +0x10
        attach_component_t c;
    };
    struct motion_t
    {
        entity_t* entity = nullptr;
        c_joints* owner = nullptr;
        attach_motion_t m;
    };
    struct join_t
    {
        entity_t* entity = nullptr;
        entity_t* parent = nullptr;
        entity_t* child = nullptr;
        const attach_properties_t* properties = nullptr;
    };

    void clear( )
    {
        m_joins.clear( );
        m_hosts.clear( );
        m_motions.clear( );
        m_unknown = 0;
    }
    void on_moved( moved_t moved ) { m_moved = std::move( moved ); }
    void on_instance( instance_t instance ) { m_instance = std::move( instance ); }
    void on_claim( std::function< void( entity_t&, bool ) > claim ) { m_claim = std::move( claim ); }

    void add( entity_t& entity, const attach_properties_t& p, entity_t* parent, entity_t* child )
    {
        join_t j;
        j.entity = &entity;
        j.parent = parent;
        j.child = child;
        j.properties = &p;
        m_joins.push_back( j );
    }

    // Slot 6 0x5F54F0 on every VuAttachEntity, in the order given.
    void game_initialize( )
    {
        for ( join_t& j : m_joins )
            if ( j.properties->attach_at_start )
                attach( j );
    }

    bool owns( const entity_t* e ) const
    {
        for ( const join_t& j : m_joins )
            if ( j.entity == e )
                return true;
        return false;
    }

    void plug_attach( const entity_t* e )
    {
        for ( join_t& j : m_joins )
            if ( j.entity == e )
                attach( j );
    }

    // The plug "Detach" 0x5F5690 / slot 7 0x5F5520.
    void plug_detach( const entity_t* e )
    {
        for ( join_t& j : m_joins )
            if ( j.entity == e )
                attach_entity_detach( j.parent, j.child, hooks( ) );
    }

    // A parent's slot 14 (its owner's calls: 0x546D66 / 0x546BC0 the prop's, 0x545EA5 the dynamic prop's ...).
    void parent_moved( const entity_t& parent, const float* world, const float* linear, const float* angular )
    {
        const auto it = m_hosts.find( &parent );
        if ( it == m_hosts.end( ) || it->second.c.records.empty( ) )
            return;
        if ( it->second.animated )
        {
            if ( it->second.instance == nullptr )
            {
                ++m_unknown;
                return;
            }
            it->second.c.bones = it->second.instance->matrices.data( );
            attach_animated_update( it->second.c, world, linear, angular );
            return;
        }
        attach_offset_update( it->second.c, world, linear, angular );
    }

    // 0x5F5810 for a motion entity's target too; null: no component.
    motion_t* find_motion( entity_t* e )
    {
        const auto it = m_motions.find( e );
        if ( it != m_motions.end( ) )
            return &it->second;
        for ( const entity_component_t& c : e->m_components )
            if ( c.m_vtable == 0x74E984u )
            {
                motion_t& m = m_motions[ e ];
                m.entity = e;
                m.owner = this;
                m.m.user = &m;
                m.m.updated = []( void* u ) {
                    motion_t& x = *static_cast< motion_t* >( u );
                    if ( x.owner->m_moved )
                        x.owner->m_moved( *x.entity, x.m.matrix, x.m.linear, x.m.angular );
                };
                // Slots 1 / 2: the props' 0x546D80 / 0x546DB0 (race_session_motion_claim names its classes; the nullsub's answer nothing there).
                m.m.attached = []( void* u ) {
                    motion_t& x = *static_cast< motion_t* >( u );
                    if ( x.owner->m_claim )
                        x.owner->m_claim( *x.entity, true );
                };
                m.m.detached = []( void* u ) {
                    motion_t& x = *static_cast< motion_t* >( u );
                    if ( x.owner->m_claim )
                        x.owner->m_claim( *x.entity, false );
                };
                return &m;
            }
        return nullptr;
    }

    const std::vector< join_t >& joins( ) const { return m_joins; }
    int unknown( ) const { return m_unknown; }
    int attached( ) const
    {
        int n = 0;
        for ( const auto& p : m_hosts )
            n += static_cast< int >( p.second.c.records.size( ) );
        return n;
    }

private:
    using hooks_t = attach_entity_hooks_t< entity_t, host_t, motion_t >;

    hooks_t hooks( )
    {
        hooks_t h;
        h.user = this;
        h.find_attach = []( void* u, entity_t* e ) { return static_cast< c_joints* >( u )->find_attach( e ); };
        h.find_motion = []( void* u, entity_t* e ) { return static_cast< c_joints* >( u )->find_motion( e ); };
        h.attach = []( void*, host_t* c, motion_t* m, const float* relative, const std::string& node ) {
            // Slot 12; findNodeByName 0x52F3C0 on the animated parent's skeleton (no instance: UNKNOWN, node 0; its update is not run).
            if ( c->animated )
                attach_animated_attach( c->c, m->m, relative, c->instance ? skel_instance_find_node( *c->instance, node.c_str( ) ) : 0 );
            else
                attach_offset_attach( c->c, m->m, relative );
        };
        h.detach = []( void*, host_t* c, motion_t* m ) { attach_detach( c->c, m->m ); };
        h.update_from_owner = []( void* u, host_t* c ) {
            const transform_component_t* t = entity_transform( *c->entity );
            if ( !t )
                return;
            const float zero[ 4 ] = { };
            static_cast< c_joints* >( u )->parent_moved( *c->entity, t->world, zero, zero );
        };
        return h;
    }

    void attach( join_t& j )
    {
        const attach_properties_t& p = *j.properties;
        attach_entity_attach( p.relative_position, p.relative_rotation, p.node_name, j.parent, j.child, hooks( ) );
    }

    host_t* find_attach( entity_t* e )
    {
        const auto it = m_hosts.find( e );
        if ( it != m_hosts.end( ) )
            return &it->second;
        for ( const entity_component_t& c : e->m_components )
            if ( c.m_vtable == 0x75182Cu || c.m_vtable == 0x75829Cu )
            {
                host_t& h = m_hosts[ e ];
                h.entity = e;
                h.animated = c.m_vtable == 0x75829Cu;
                h.c.entity = e;
                if ( h.animated && m_instance )
                    h.instance = m_instance( *e );
                return &h;
            }
        return nullptr;
    }

    std::vector< join_t > m_joins;
    std::map< const entity_t*, host_t > m_hosts;
    std::map< const entity_t*, motion_t > m_motions;
    moved_t m_moved;
    instance_t m_instance;
    std::function< void( entity_t&, bool ) > m_claim;
    int m_unknown = 0;
};
