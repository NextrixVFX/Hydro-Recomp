#pragma once

// gfx_sort_hooks_t on the real device (water batch 35): what stands between world/gfxsort.hxx's walk, which speaks retail's vtable offsets and ids, and render/gfx.hxx + render/program.hxx. Each
// slot goes to the function retail's vtable holds there (0x74D79C the device, 0x751124 the program); a slot that is retail's empty function does nothing. Ids here are table indices + 1: retail's are
// pointers. The tables are this file's only decision.
//
// A callback is registered by its retail address; a command whose callback nobody registered is counted in m_unknown and drawn as nothing, so a coverage gap is measured, not hidden.
// Retail's thread (g 0x759098) is not used: gfx_sort_t's single-threaded path.
// SUBSTITUTION: retain / release do nothing: programs and declarations live until the session's gfx_programs_t goes.
// Verified against retail: hydro checks/device.hxx verify_gfx_backend (case gfx_slots): every slot through retail's vtable and through here, the two logs compared.

#include <workspace/core/render/gfx.hxx>
#include <workspace/core/render/program.hxx>
#include <workspace/core/render/resource.hxx>
#include <workspace/core/world/gfxsort.hxx>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

struct gfx_backend_t final : gfx_sort_hooks_t
{
    explicit gfx_backend_t( gfx_device_t& device ) : m_device( device ) { }

    gfx_device_t& m_device;
    bool m_present = false;                                 // the game's engine presents for itself until the sort owns the frame
    std::vector< const gfx_program_t* > m_program_table;
    std::vector< const gfx_constant_slot_t* > m_constant_table;
    std::vector< gfx_handle_t > m_layout_table;
    std::vector< const gfx_buffer_t* > m_buffer_table;
    std::vector< const gfx_texture_t* > m_texture_table;
    std::vector< const gfx_material_t* > m_material_table;
    std::vector< const gfx_chunk_t* > m_chunk_table;
    std::map< std::uint32_t, std::function< void( std::uint8_t* ) > > m_callbacks;
    std::map< std::uint32_t, std::uint32_t > m_unknown;
    std::function< const void*( std::uint32_t ) > m_source;     // the static or light field a program call names (gfxsort.hxx k_gfx_view_matrix ...)
    std::function< std::uint32_t( std::uint32_t, const std::string& ) > m_asset;   // kind, name: the texture's id (add_texture)
    std::function< void( ) > m_water_flip, m_light_flip;

    template < typename T >
    static std::uint32_t add( std::vector< T >& table, T value )
    {
        table.push_back( value );
        return static_cast< std::uint32_t >( table.size( ) );
    }
    std::uint32_t add_program( const gfx_program_t* p ) { return add( m_program_table, p ); }
    std::uint32_t add_layout( gfx_handle_t l ) { return add( m_layout_table, l ); }
    std::uint32_t add_buffer( const gfx_buffer_t* b ) { return add( m_buffer_table, b ); }
    std::uint32_t add_texture( const gfx_texture_t* t ) { return add( m_texture_table, t ); }
    std::uint32_t add_material( const gfx_material_t* m ) { return add( m_material_table, m ); }
    std::uint32_t add_chunk( const gfx_chunk_t* c ) { return add( m_chunk_table, c ); }

    std::uint32_t device( int slot, std::uint32_t a = 0, std::uint32_t b = 0, std::uint32_t c = 0 ) override
    {
        switch ( slot )
        {
        case k_gfx_acquire:   // 0x540860 / 0x540880: which thread owns the context, the single-threaded path has one
        case k_gfx_release:
        case k_gfx_flush:     // retail's empty 0x401560
            return 0u;
        case k_gfx_begin:
            m_device.begin( );
            return 1u;        // 0x540A90 leaves the nonzero byte the walk tests
        case k_gfx_end:
            if ( m_present )
                m_device.end( );
            return 0u;
        case k_gfx_slot_80:
            return 0x80u;     // the clip plane: k_gfx_set_value reads it from the device
        case k_gfx_program:
            m_device.set_program( m_program_table[ a - 1u ] );
            return 0u;
        case k_gfx_declaration:
            m_device.set_declaration( m_layout_table[ a - 1u ] );
            return 0u;
        case k_gfx_stream:
            m_device.set_stream( m_buffer_table[ a - 1u ], b, c );
            return 0u;
        case k_gfx_indices:
            m_device.set_index_buffer( m_buffer_table[ a - 1u ] );
            return 0u;
        case k_gfx_depth:
            m_device.depth_enable( static_cast< std::uint8_t >( a ) );
            return 0u;
        case k_gfx_depth_write:
            m_device.depth_write( static_cast< std::uint8_t >( a ) );
            return 0u;
        case k_gfx_depth_func:
            m_device.depth_func( a );
            return 0u;
        case k_gfx_blend:
            m_device.blend_enable( static_cast< std::uint8_t >( a ) );
            return 0u;
        case k_gfx_blend_func:
            m_device.blend_func( a, b );
            return 0u;
        case k_gfx_colour_write:
            m_device.colour_write( static_cast< std::uint8_t >( a ) );
            return 0u;
        case k_gfx_cull:
            m_device.cull( a );
            return 0u;
        case k_gfx_texture:
            m_device.set_texture( a, b ? m_texture_table[ b - 1u ] : nullptr );
            return 0u;
        default:
            return 0u;
        }
    }

    void program( std::uint32_t id, int slot, std::uint32_t handle, std::uint32_t value, const std::uint32_t* vector ) override
    {
        gfx_api_t& api = m_device.m_api;
        if ( slot == k_gfx_bind )
            return;     // 0x404BD0
        const gfx_program_t& p = *m_program_table[ id - 1u ];
        const gfx_constant_slot_t& c = *m_constant_table[ handle - 1u ];
        switch ( slot )
        {
        case k_gfx_set_float:
        case k_gfx_set_int:
            gfx_programs_t::set_word( api, p, c, value );
            break;
        case k_gfx_set_vector:
            gfx_programs_t::set_vec3( api, p, c, vector );
            break;
        case k_gfx_set_matrix:
            gfx_programs_t::set_matrix( api, p, c, m_source( value ) );
            break;
        case k_gfx_set_vec3:
            gfx_programs_t::set_vec3( api, p, c, m_source( value ) );
            break;
        case k_gfx_set_value:   // slot 10, 16 bytes
            gfx_programs_t::set_vec4( api, p, c, m_device.m_clip );
            break;
        case k_gfx_set_vec4:    // slot 11 0x5ABE30, a VuColor's r, g, b over 255, whatever the enum's name says
            gfx_programs_t::set_colour3( api, p, c, static_cast< const std::uint8_t* >( m_source( value ) ) );
            break;
        default:
            break;
        }
    }

    std::uint32_t thread( int, std::uint32_t, std::uint32_t ) override { return 0u; }
    void profile( int ) override { }
    void composer( int, std::uint32_t, std::uint32_t ) override { }
    void water_flip( ) override
    {
        if ( m_water_flip )
            m_water_flip( );
    }
    void light_flip( ) override
    {
        if ( m_light_flip )
            m_light_flip( );
    }
    std::uint32_t texture_handle( std::uint32_t texture ) override { return texture; }
    void draw( std::uint32_t callback, std::uint8_t* record ) override
    {
        const auto found = m_callbacks.find( callback );
        if ( found == m_callbacks.end( ) )
        {
            ++m_unknown[ callback ];
            return;
        }
        found->second( record );
    }
    const gfx_material_t* material( std::uint32_t id ) override { return m_material_table[ id - 1u ]; }
    const gfx_chunk_t* chunk( std::uint32_t id ) override { return m_chunk_table[ id - 1u ]; }
    std::uint32_t program_constant( std::uint32_t id, const std::string& name ) override
    {
        const gfx_constant_slot_t* c = m_program_table[ id - 1u ]->constant( name.c_str( ) );
        return c ? add( m_constant_table, c ) : 0u;
    }
    std::int32_t program_sampler( std::uint32_t id, const std::string& name ) override { return m_program_table[ id - 1u ]->sampler( name.c_str( ) ); }
    std::uint32_t asset_get( std::uint32_t kind, const std::string& name ) override { return m_asset ? m_asset( kind, name ) : 0u; }
};
