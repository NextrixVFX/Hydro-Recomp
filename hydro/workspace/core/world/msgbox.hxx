#pragma once

// c_world's message boxes (world batch 41): game/boxes.hxx's VuMessageBoxManager g 0x758FD4 on the Screens/MessageBox project. Nothing here is a retail function but the
// ones it names: it binds the manager's hooks to OUR objects.
//   boxes_start   slot 5 0x4B11F0: the project "Screens/MessageBox", its root held, the Decision tick and the draw hook. Runs once, at the first scene (retail: app init).
//   boxes_tick    0x4B14D0, from scene_frame ahead of the mode. FLAGGED: retail's place in the Decision phase is its registration order, not read.
//   boxes_draw    0x4B1520 on the render hook list g 0x759024, after the mode's draw: the sort key (layer 5) decides what lands on top, so the order here does not show.
// HOOKS: the touch stack's focus answers true (0x53FB90 .. push the box's entry on top while it is up and no touch reaches this platform); FMOD is ui_play's.

// The input manager that is live: the race's, else the front end's. Null before either starts.
inline platform_input_t* c_world::boxes_input( )
{
    if ( m_race.active && m_race.input )
        return m_race.input.get( );
    return m_menu_input.get( );
}

// VuControlMethodManager's "OnGamePadDisconnected" 0x4AEA00: pad 0 lost while it is the control method and no such box is up: the box, then "OnPauseActionGame". The box's callback +0xC
// 0x4AEC30 clears the byte. OnGamePadConnected: no listener here.
inline void c_world::boxes_pad( bool connected, int pad )
{
    const platform_input_t* in = boxes_input( );
    if ( connected || pad != 0 || m_pad_box || !in || in->device.control_method( ) != 0 )
        return;
    msgbox_ref_t box = boxes_show( "GamePadDisconnected_Heading", "GamePadDisconnected_Body", "MessageBox_OK" );
    if ( box )
        box->m_callback = [ this ]( msgbox_t& ) { m_pad_box = false; };
    m_pad_box = true;
    if ( m_race.active )
    {
        const ui_params_t none;
        race_broadcast( "OnPauseActionGame", &none );
    }
}

inline void c_world::boxes_start( )
{
    if ( m_boxes_tried )
        return;
    m_boxes_tried = true;
    g_platform_input.any_window = c_engine::option( "background" ) != nullptr;
    g_platform_input.pad = [ this ]( bool connected, int pad ) { boxes_pad( connected, pad ); };
    m_box_root = menu_load_root( "Screens/MessageBox" );
    if ( !m_box_root || m_box_root->m_type != "VuMessageBoxScreenEntity" )   // 0x4B12A0: the root's type chain must hold 0x736590
    {
        LOG_WARNING( "message boxes: Screens/MessageBox has no VuMessageBoxScreenEntity root" );
        m_box_root.reset( );
        return;
    }
    ui_bind_hooks_t hooks = menu_hooks( 0 );
    hooks.m_external = [ this ]( entity_t& e, const char* plug, const ui_params_t& p ) { return screen_external( m_box_state, *m_box_binding, e, plug, p ); };
    m_box_binding = std::make_unique< c_ui_binding >( *m_box_root, hooks );
    if ( const ui_slot_t* s = ui_bind_slot( *m_box_root, "Screen Scale" ) )
        std::memcpy( m_box_view.m_screen_scale, s->m_bits, 8 );
    for ( int k = 0; k < 2; ++k )
    {
        static const char* const names[ 2 ] = { "ButtonA Touch", "ButtonB Touch" };
        static const char* const xywh[ 4 ] = { " X", " Y", " Width", " Height" };
        for ( int j = 0; j < 4; ++j )
            if ( const ui_slot_t* s = ui_bind_slot( *m_box_root, ( std::string( names[ k ] ) + xywh[ j ] ).c_str( ) ) )
                std::memcpy( &m_box_view.m_touch[ k ][ j ], s->m_bits, 4 );
    }
    m_box_screen.m_box = &m_box_view;
    for ( ui_message_block_t& block : m_box_view.m_blocks )   // the block's ctor 0x464340: +0x80 = 1 (0x466E40 never writes block 0's)
        block.m_enabled = 1;
    if ( const ui_slot_t* s = ui_bind_slot( *m_box_root, "Animated Rotation Speed" ) )
        std::memcpy( &m_box_screen.m_spin, s->m_bits, 4 );
    msgbox_hooks_t h;
    h.text = [ this ]( const std::string& id ) { return string_db_get( m_string_db, id ); };
    h.missing = [ this ] { return string_db_get( m_string_db, "" ); };
    h.ui_tick = [ this ]( float dt ) { m_box_binding->hud_tick( dt ); };
    h.ui_event = [ this ]( const char* name ) { m_box_binding->hud_event( name ); };
    h.pressed = [ this ]( const char* name ) {
        const platform_input_t* in = boxes_input( );
        return in && in->manager.pressed( 0, name );
    };
    h.focus = [ ] { return true; };
    h.play = [ this ]( const char* name ) { ui_play( name ); };
    h.pause = [ this ] { m_race.ticks.pause_count += 1; };
    h.resume = [ this ] { m_race.ticks.pause_count -= 1; };
    // VuMessageBoxScreenEntity slot 6 0x466CA2 / 0x466CBE: { this + 0x5C, 0 } on the touch device, then priority 0x8000, so +0x1C reads 0x8000 while a box is up: what stops 0x4BBE30's Pause
    // and the HUD's 0x4262A0 under a box. Slot 7 0x466E0F takes it off.
    h.project_on = [ this ] {
        m_box_binding->project_initialize( );   // 0x500B10 and 0x501260 are one call there
        ui_touch_add( m_touch, &m_box_screen, 0u );
        ui_touch_set_priority( m_touch, &m_box_screen, 0x8000u );
    };
    h.project_off = [ this ] {
        ui_touch_remove( m_touch, &m_box_screen );
        m_box_binding->project_release( );
    };
    h.project_ready = [ ] { };
#if HYDRO_UI_PASS
    h.texture_load = [ this ]( const std::string& name ) {
        const ui_texture_t* t = m_ui_pass ? m_ui_pass->texture_asset( name ) : nullptr;
        m_box_image = t ? t->m_handle : 0u;
    };
#else
    h.texture_load = [ ]( const std::string& ) { };
#endif
    h.texture_free = [ this ] { m_box_image = 0u; };
    m_boxes.init( &m_box_screen, h );
    m_boxes_ready = true;
    LOG_INFO( "message boxes: Screens/MessageBox loaded" );
}

inline void c_world::boxes_tick( float dt )
{
    if ( !m_boxes_ready )
        return;
    m_boxes.tick( dt );
}

// A retail creator's box: heading, body, text A; the rest 0x4B0DA0's defaults.
inline msgbox_ref_t c_world::boxes_show( const std::string& heading, const std::string& body, const std::string& text_a, const std::string& text_b )
{
    if ( !m_boxes_ready )
    {
        LOG_WARNING( "message box '%s' / '%s' with no Screens/MessageBox", heading.c_str( ), body.c_str( ) );
        return nullptr;
    }
    msgbox_params_t p;
    p.m_heading = heading;
    p.m_body = body;
    p.m_text_a = text_a;
    p.m_text_b = text_b;
    return m_boxes.create( p );
}

// A held box's release at its owner's end: 0x464060, 0x4640D0.
inline void c_world::boxes_release( screen_state_t& state )
{
    for ( auto& held : state.boxes )
        msgbox_entity_destroy( held.second );
    state.boxes.clear( );
}

// What a screen's entities hold dies with its project: 0x464060 marks a held box destroyed, so the manager never runs its callback on the freed entity.
inline void c_world::screen_state_release( screen_state_t& state )
{
    boxes_release( state );
    for ( auto& held : state.sounds )   // VuAudioEventEntity's dtor 0x5F4220: the emitter's holder release
    {
        audio_emitter_t emitter;
        emitter.holder = held.second;
        audio_emitter_destroy( emitter, m_sound );
    }
    state = screen_state_t{ };
}

// VuMessageBoxEntity's two inputs (0x464090 "Create", 0x4640D0 "Destroy"), its properties from the UI's loaded slots.
inline bool c_world::boxes_entity( screen_state_t& state, c_ui_binding& binding, entity_t& e, const char* plug )
{
    if ( e.m_type != "VuMessageBoxEntity" )
        return false;
    const auto at = state.boxes.find( &e );
    msgbox_entity_t& box = at != state.boxes.end( ) ? at->second : state.boxes[ &e ];
    if ( at == state.boxes.end( ) )
    {
        const auto text = [ &e ]( const char* name, std::string& out ) {
            if ( const ui_slot_t* s = ui_bind_slot( e, name ) )
                out = s->m_text;
        };
        const auto bits = [ &e ]( const char* name, void* to, std::size_t size ) {
            if ( const ui_slot_t* s = ui_bind_slot( e, name ) )
                std::memcpy( to, s->m_bits, size );
        };
        msgbox_params_t& p = box.m_params;
        text( "Heading String ID", p.m_heading );
        text( "String ID", p.m_body );
        text( "Text A", p.m_text_a );
        text( "Text B", p.m_text_b );
        text( "Texture", p.m_texture );
        bits( "Texture Flip X", &p.m_flip, 1 );
        bits( "Texture Scale X", &p.m_scale[ 0 ], 4 );
        bits( "Texture Scale Y", &p.m_scale[ 1 ], 4 );
        text( "Sfx A", p.m_sfx_a );
        text( "Sfx B", p.m_sfx_b );
        bits( "Pause Game", &p.m_pause, 1 );
        c_ui_binding* const on = &binding;
        entity_t* const owner = &e;
        box.m_fire = [ on, owner ]( const char* output ) { on->fire_plug( *owner, output, ui_params_t( ) ); };
    }
    if ( std::strcmp( plug, "Create" ) == 0 )
    {
        if ( m_boxes_ready )
            msgbox_entity_create( box, m_boxes );
        return true;
    }
    if ( std::strcmp( plug, "Destroy" ) == 0 )
    {
        msgbox_entity_destroy( box );
        return true;
    }
    return false;
}

#if HYDRO_UI_PASS
inline void c_world::boxes_draw( )
{
    if ( !m_boxes_ready || !m_boxes.drawing( ) || !m_ui_pass )
        return;
    ui_frame_inputs_t in = ui_screen_inputs( *m_box_binding, ui_screen_environment( ) );
    in.m_message_box = [ this ]( const entity_t& e, ui_message_box_t& b, ui_message_box_inputs_t& mi ) {
        static const char* const blocks[ 5 ] = { "Heading", "Body", "BodyImage", "ButtonA", "ButtonB" };
        for ( int k = 0; k < 5; ++k )
        {
            b.m_blocks[ k ].m_enabled = m_box_view.m_blocks[ k ].m_enabled;
            b.m_blocks[ k ].m_text = m_box_view.m_blocks[ k ].m_text;
            const ui_slot_t* font = ui_property_value( e, ( std::string( blocks[ k ] ) + " Font" ).c_str( ) );
            std::uint8_t params[ 60 ] = { };
            std::string asset;
            ui_font_params( m_font_db, font ? font->m_text : std::string( ), params, asset );
            ui_text_font_t& f = b.m_blocks[ k ].m_font;
#if HYDRO_UI_TEXT
            f.m_asset_font = asset.empty( ) ? 0u : m_ui_pass->font_asset( asset );
            f.m_asset = f.m_asset_font != 0u;
            f.m_default_font = m_ui_pass->font_asset( "Dev" );
            f.m_default = f.m_default_font != 0u;
#endif
        }
        b.m_image_flip = m_box_screen.m_params.m_flip;
        b.m_progress[ 0 ] = m_box_screen.m_params.m_scale[ 0 ];
        b.m_progress[ 1 ] = m_box_screen.m_params.m_scale[ 1 ];
        b.m_rotation = m_box_view.m_rotation;
        b.m_button = m_box_view.m_button;
        const auto texture = [ this, &e ]( const char* name ) {
            const ui_slot_t* s = ui_property_value( e, name );
            const ui_texture_t* t = s && !s->m_text.empty( ) ? m_ui_pass->texture_asset( s->m_text ) : nullptr;
            return t ? t->m_handle : 0u;
        };
        static const char* const names[ 6 ] = { "ButtonA Texture Asset", "ButtonA Selected Texture Asset", "ButtonA Pressed Texture Asset",
                                                "ButtonB Texture Asset", "ButtonB Selected Texture Asset", "ButtonB Pressed Texture Asset" };
        for ( int k = 0; k < 6; ++k )
            mi.m_buttons[ k ] = texture( names[ k ] );
        mi.m_spinner = texture( "Animated Texture Asset" );
        mi.m_image = m_box_image;
        return true;
    };
    ui_root_frame( *m_box_root, m_ui_pass->m_impl, m_ui_pass->m_sort, m_boxes.fade( ), &in );
}
#endif
