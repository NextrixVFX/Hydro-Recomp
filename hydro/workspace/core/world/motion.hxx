#pragma once

// c_world's motion - member definitions moved out of world.hxx (one class, split by concern).

// One of an entity's refs, as its load bound it: the target's path, or ""
// when the ref is unbound. The resolution is retail's (VuScriptRef's load
// 0x542830 / RefConnections 0x5049E0 through the path lookup 0x500F60, in
// the tree - engine entity/script.hxx); the template-relative rule that
// stood here was a measured inference, and matched it on all 219 bound refs
// of Hydroelectric before it was deleted.
inline std::string c_world::ref_target( const std::string& owner, const std::string& name ) const
{
    const auto found = m_refs.find( std::make_pair( owner, name ) );
    return found != m_refs.end( ) ? found->second : std::string( );
}

// The "Motion" tick 0x61A6E0 on each rotator (world/rotate.hxx), then the target's IF slot 0 with M1, linear 0 and the angular
// velocity. FLAGGED: run from the scene frame, not a tick manager's "Motion" phase; a rotator's order against the others' as loaded.
inline void c_world::update_rotators( float dt )
{
    for ( rotator_t& motion : m_rotators )
    {
        if ( !motion.active )
            continue;
        float matrix[ 16 ], angular[ 4 ];
        rotate_motion_tick( motion.value, dt, matrix, angular );
        const float linear[ 4 ] = { };
        attach_motion_t& m = motion.motion->m;
        std::memcpy( m.matrix, matrix, 64 );
        std::memcpy( m.linear, linear, 16 );
        std::memcpy( m.angular, angular, 16 );
        bool stepped = true;
        race_session_motion_update( m_race, *motion.target, stepped, matrix, linear, angular );
    }
}

// VuMotionEntity "Activate" 0x61A4F0 (keymotion.hxx key_motion_activate's shape): with a target, no latch and its +0x14 free: +0x14
// = the entity, the target's IF slot 1 (0x546D80 on a prop: its rigid component's flag - no siren holds a body; a body's is
// UNKNOWN here), active, One Shot -> latch, slot 12 0x5D0FE0, "OnActivated".
inline void c_world::rotator_activate( rotator_t& motion )
{
    if ( motion.motion == nullptr || motion.latch || motion.motion->m.parent != nullptr )
        return;
    motion.motion->m.parent = motion.entity;
    motion.active = true;
    if ( motion.one_shot )
        motion.latch = true;
    const transform_component_t* t = entity_transform( *motion.target );
    rotate_motion_enable( motion.value, t ? t->world : nullptr );
    fire( motion.path, "OnActivated" );
}

// "Deactivate" 0x61A5D0: when active: inactive, slot 13 (the nullsub), "OnDeactivated", +0x14 = 0, the target's IF slot 2.
inline void c_world::rotator_deactivate( rotator_t& motion )
{
    if ( !motion.active )
        return;
    motion.active = false;
    fire( motion.path, "OnDeactivated" );
    motion.motion->m.parent = nullptr;
}

// VuAttachEntity's slot 7 0x5F5520 / VuRotateMotionEntity's 0x61A4B0 (Deactivate; its tick unregistered: the scene frame's loop
// sees it inactive).
inline bool c_world::motion_release( const entity_t& e )
{
    if ( e.m_type == "VuAttachEntity" )
    {
        m_joints.plug_detach( &e );
        return true;
    }
    if ( e.m_type == "VuRotateMotionEntity" )
    {
        for ( rotator_t& motion : m_rotators )
            if ( motion.entity == &e )
                rotator_deactivate( motion );
        return true;
    }
    return false;
}

// Slot 6 of the VuAttachEntity and VuRotateMotionEntity of the level (FLAGGED: at the end of the load, in the walk's order - each
// type's own list, not 0x500B10's single walk; the worlds they read are the loaded ones either way).
inline void c_world::motions_game_initialize( )
{
    m_joints.on_moved( [ this ]( entity_t& child, const float* matrix, const float* linear, const float* angular ) {
        motion_child( child, matrix, linear, angular );
    } );
    m_joints.game_initialize( );
    for ( rotator_t& motion : m_rotators )
    {
        if ( motion.target != nullptr )
            motion.motion = m_joints.find_motion( motion.target );
        if ( motion.initially_active )
            rotator_activate( motion );
    }
}

// A child's motion IF slot 0 by class: VuCoronaEntity 0x5DC3B0 setWorldTransform( the matrix, notify 1 ), the corona's draw reads the
// world; VuStaticPfxEntity 0x5EB4C0 the world (0x534C00( 0 )) then its +0x94 system's setMatrix; VuAudioEmitterEntity 0x5F3D80. Any
// other class's is not read: UNKNOWN, not run, counted.
inline void c_world::motion_child( entity_t& child, const float* matrix, const float* linear, const float* angular )
{
    ( void ) angular;
    const std::string path = entity_path( child );
    if ( child.m_type == "VuCoronaEntity" )
    {
        if ( entity_set_world( child, matrix, true, { } ) )
            m_coronas.place( path, matrix );
        else
            ++m_motion_unknown;
        return;
    }
    if ( child.m_type == "VuStaticPfxEntity" )
    {
        if ( !entity_set_world( child, matrix, false, { } ) )
        {
            ++m_motion_unknown;
            return;
        }
        const auto it = m_static_pfx.find( path );
        if ( it != m_static_pfx.end( ) )
            m_pfx.set_matrix( it->second, matrix );
        return;
    }
    if ( child.m_type == "VuAudioEmitterEntity" && child.m_state )
    {
        if ( !entity_set_world( child, matrix, true, { } ) )
        {
            ++m_motion_unknown;
            return;
        }
        sound_hooks_t h = m_sound;
        h.set_world = nullptr;   // done above (the same call, first)
        float record[ 20 ];
        std::memcpy( record, matrix, 64 );
        std::memcpy( record + 16, linear, 16 );
        audio_emitter_motion( static_cast< audio_emitter_state_t* >( child.m_state.get( ) )->value, record, h );
        return;
    }
    ++m_motion_unknown;
}
