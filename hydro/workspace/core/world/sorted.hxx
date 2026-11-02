#pragma once

#include <impl/engine/engine.hxx>
#include <workspace/core/render/direct.hxx>
#include <workspace/core/world/gfxsort.hxx>
#include <workspace/core/world/instance.hxx>

#include <map>
#include <memory>
#include <string>

// The render seam (world batch 36): gfx_sort_hooks_t on the recreated device, gfx_device_t over gfx_direct_t, so the placements
// of c_geometry::draw's blend passes go through retail's sort: its 64-bit keys, radix sort, walk 0x53ADC0 and layer table 0x735A00.
//
// What is retail's: the key (0x429010), the depth 0x520E70 (gfx_mesh_depth on the mesh's aabb), the order, every depth and blend
// state the walk sets, gfx_device_t's commit 0x5AD3F0.
//
// What is OURS, flagged:
// - The programs. device( k_gfx_program ) and friends answer nothing: the draw is c_geometry's, with the hand-written shaders of
//   engine render/shader.hxx, its buffers, its textures and its cull. Retail's programs are render/program.hxx in agent/water.
// - A material here is (the placement's texture) through the real cache 0x53A440, with one program id: retail's tie-break inside
//   a key differs. No chunks.
// - One sort per c_geometry::draw, so a reflection pass has its own flip; retail has one flip a frame with the passes in the
//   key's screen field. The record is 4 bytes (the placement's index), not the 0x50-byte record of 0x521220.
// - Only these placements: the opaque pass, the sky, water and spray stay c_geometry's.
struct gfx_sorted_t final : gfx_sort_hooks_t
{
    gfx_sorted_t( void* device, void* context )
        : m_gfx( m_direct ), m_sort( *this )
    {
#if HYDRO_HOST_WINDOWS
        m_direct.m_device = static_cast< ID3D11Device* >( device );
        m_direct.m_context = static_cast< ID3D11DeviceContext* >( context );
#endif
        m_sort.reserve( );
    }

    // ---- gfx_sort_hooks_t ---------------------------------------------------------------------------------------------------
    std::uint32_t device( int slot, std::uint32_t a, std::uint32_t b, std::uint32_t ) override
    {
        switch ( slot )
        {
        case k_gfx_begin: return 1;
        case k_gfx_depth: m_gfx.depth_enable( static_cast< std::uint8_t >( a ) ); break;
        case k_gfx_depth_write: m_gfx.depth_write( static_cast< std::uint8_t >( a ) ); break;
        case k_gfx_depth_func: m_gfx.depth_func( a ); break;
        case k_gfx_blend: m_gfx.blend_enable( static_cast< std::uint8_t >( a ) ); break;
        case k_gfx_blend_func: m_gfx.blend_func( a, b ); break;
        case k_gfx_colour_write: m_gfx.colour_write( static_cast< std::uint8_t >( a ) ); break;
        case k_gfx_cull: m_gfx.cull( a ); break;
        default: break;
        }
        return 0;
    }
    void program( std::uint32_t, int, std::uint32_t, std::uint32_t, const std::uint32_t* ) override { }
    std::uint32_t thread( int, std::uint32_t, std::uint32_t ) override { return 0; }
    void profile( int ) override { }
    void composer( int, std::uint32_t, std::uint32_t ) override { }
    void water_flip( ) override { }
    void light_flip( ) override { }
    std::uint32_t texture_handle( std::uint32_t ) override { return 0; }
    void draw( std::uint32_t, std::uint8_t* record ) override
    {
        int index;
        std::memcpy( &index, record, 4 );
        m_gfx.commit( );
        m_pass->draw( m_pass->draw_user, index );
    }
    const gfx_material_t* material( std::uint32_t id ) override { return m_ids[ id - 1 ]; }
    const gfx_chunk_t* chunk( std::uint32_t ) override { return nullptr; }

    // ---- the pass -----------------------------------------------------------------------------------------------------------
    std::uint32_t material_of( int texture )
    {
        const auto at = m_by_texture.find( texture );
        if ( at != m_by_texture.end( ) )
            return at->second;
        gfx_material_desc_t desc;
        gfx_material_desc_t::texture_t t;
        t.m_name = "DiffuseTexture";
        t.m_asset = std::to_string( texture );
        desc.m_textures.push_back( t );
        m_ids.push_back( m_sort.material_create( 1u, 1u, desc ) );
        const std::uint32_t id = static_cast< std::uint32_t >( m_ids.size( ) );
        m_by_texture.emplace( texture, id );
        return id;
    }

    void run( const sorted_pass_t& pass, float far_plane )
    {
        gfx_camera_t camera{ };
        std::memcpy( camera.m_eye, pass.eye, 12 );
        camera.m_far = far_plane;
        for ( int k = 0; k < pass.count; ++k )
        {
            const sorted_item_t& item = pass.items[ k ];
            gfx_scene_mesh_t mesh;
            if ( item.box )
            {
                std::memcpy( mesh.m_min, item.box, 16 );
                std::memcpy( mesh.m_max, item.box + 4, 16 );
            }
            const float depth = gfx_mesh_depth( mesh, item.world, camera );
            std::uint8_t* record = m_sort.allocate( 4 );
            std::memcpy( record, &item.index, 4 );
            m_sort.submit( static_cast< std::uint32_t >( item.type ), material_of( item.texture ), 0u, k_gfx_cb_model, depth );
        }
        m_pass = &pass;
        m_sort.flip( );
        m_pass = nullptr;
    }

#if HYDRO_HOST_WINDOWS
    gfx_direct_t m_direct;
#endif
    gfx_device_t m_gfx;
    gfx_sort_t m_sort;
    const sorted_pass_t* m_pass = nullptr;
    std::vector< gfx_material_t* > m_ids;
    std::map< int, std::uint32_t > m_by_texture;
};
