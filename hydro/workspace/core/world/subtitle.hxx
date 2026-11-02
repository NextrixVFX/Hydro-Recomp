#pragma once

#include <workspace/core/assets/tree.hxx>
#include <workspace/core/entity/values.hxx>

#include <xmmintrin.h>

#include <cstdint>

// VuSubtitleManagerImpl (g 0x758FE4, vtable 0x745B8C; boat batch 31, case subtitle). The voice-over tick 0x4B8000 calls slot 7
// with "Copilot_%03d"; the line shows for its SubtitleDB entry's Time in the "Build" phase's tick 0x4B5C10, drawn by the
// draw method 0x4B5C50 (registered on g 0x759024 vt+0x1C).
//
// HOOKS (subtitle_hooks_t): the string table g 0x759078 vt+0x18, the font object at +8 (ctor 0x626900, "Subtitle" by 0x626B00),
// the font params 0x626B80 + the font draw 0x536C10, the rect 0x507A00 (the UI lane's), "Subtitles/Show Rect" the dev menu's
// bool (the caller sets m_show_rect). Not here: the registrations of 0x4B5960, the release slot 3 0x4B5B40, dtor 0x4B58F0
// (0x626A40). Time's int64 tag 7 is recreated and NOT exercised by the case (no such entry in the shipped DB).
struct subtitle_hooks_t
{
    void* user = nullptr;
    const char* ( *text )( void* user, const char* id ) = nullptr;
    void ( *font )( void* user, const char* text, const float rect[ 4 ], std::uint32_t flags, float alpha, const float offset[ 2 ] ) = nullptr;
    void ( *box )( void* user, std::uint32_t color, const float rect[ 4 ] ) = nullptr;
};

struct subtitle_manager_t
{
    subtitle_hooks_t hooks{};
    const value_t* m_document = nullptr;  // +4's +0x58
    float m_rect[ 4 ] = { 0.0f, 0.0f, 1.0f, 1.0f };   // +0x6C
    std::uint8_t m_show_rect = 0;         // +0x7C
    const char* m_text = nullptr;         // +0x80
    float m_time = 0.0f;                  // +0x84

    // Slot 5 0x4B5960, past the registrations: VuDBAsset.Config.DisplayRect into the rect (0x4FDFE0).
    void init( const value_t& document )
    {
        m_document = &document;
        static const char* const lanes[ 4 ] = { "X", "Y", "W", "H" };
        property_lanes( member( member( member( document, "VuDBAsset" ), "Config" ), "DisplayRect" ), lanes, 4, m_rect );
    }

    // Slot 6 0x4B5B70.
    void clear( )
    {
        m_text = nullptr;
    }

    // Slot 7 0x4B5B80: no entry, or a Time that is not a number: nothing changes.
    void show( const char* id )
    {
        const value_t& entry = member( member( *m_document, "VuDBAsset" ), id );
        if ( entry.tag == 0 )
            return;
        const value_t& time = member( entry, "Time" );
        if ( time.tag == 1 )
            m_time = static_cast< float >( time.integer );
        else if ( time.tag == 2 )
            std::memcpy( &m_time, &time.real, sizeof( float ) );
        else if ( time.tag == 7 )
            m_time = static_cast< float >( time.int64 );
        else
            return;
        m_text = hooks.text( hooks.user, id );
    }

    // The tick 0x4B5C10: past zero (below, ordered) the line goes.
    void tick( float dt )
    {
        if ( m_text == nullptr )
            return;
        m_time -= dt;
        if ( _mm_comigt_ss( _mm_setzero_ps( ), _mm_set_ss( m_time ) ) != 0 )
            m_text = nullptr;
    }

    // 0x4B5C50: `enabled` g_vusettingsmanager 0x758FE0 +0x5C. The sort's key +0x74 takes its fields inline.
    void draw( bool enabled, std::uint32_t& key_hi )
    {
        if ( m_text == nullptr || !enabled )
            return;
        key_hi = ( ( key_hi & 0x07FFFFFFu ) | 0x18000000u ) & 0xFCFFFFFFu & 0xFF7FFFFFu;
        key_hi = ( key_hi & 0xFFC3FFFFu ) | 0x240000u;
        const float offset[ 2 ] = { 0.0f, 0.0f };
        hooks.font( hooks.user, m_text, m_rect, 0x86u, 1.0f, offset );
        if ( m_show_rect != 0 )
            hooks.box( hooks.user, 0x804040FFu, m_rect );
    }
};
