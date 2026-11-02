#pragma once

#include <workspace/core/render/device.hxx>

class c_shader
{
public:
    ~c_shader( );
    bool create( );
    void bind_mesh( );
    void bind_textured( );
    void bind_blit( );
    void constants( const void* data, int bytes );

private:
    void release( );

#if HYDRO_HOST_WINDOWS
    ID3D11VertexShader* m_mesh_vs{};
    ID3D11PixelShader*  m_mesh_ps{};
    ID3D11InputLayout*  m_layout{};
    ID3D11Buffer*       m_constants{};
    int                 m_constant_bytes{};
    ID3D11VertexShader* m_textured_vs{};
    ID3D11PixelShader*  m_textured_ps{};
    ID3D11InputLayout*  m_textured_layout{};
    ID3D11VertexShader* m_blit_vs{};
    ID3D11PixelShader*  m_blit_ps{};
    ID3D11SamplerState* m_sampler{};
    ID3D11SamplerState* m_wrap{};
#endif
};

c_shader& g_shader( );

#include <workspace/utility/logger.hxx>
#include <cstring>

#if HYDRO_HOST_WINDOWS
#  include <d3dcompiler.h>
#  include <d3d11shader.h>
#  pragma comment( lib, "d3dcompiler.lib" )
#endif

namespace
{
    c_shader g_shader_instance{};

    const char* mesh_hlsl = R"(
cbuffer constants : register(b0)
{
    float4x4 world;
    float4x4 view;
    float4x4 projection;
    float4 light_dir;
    float4 color;
};
struct input_t
{
    float3 position : POSITION;
    float3 normal : NORMAL;
};
struct output_t
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
};
output_t main_vs(input_t input)
{
    output_t output;
    float4 world_pos = mul(world, float4(input.position, 1));
    output.position = mul(projection, mul(view, world_pos));
    output.normal = mul(world, float4(input.normal, 0)).xyz;
    return output;
}
float4 main_ps(output_t input) : SV_Target
{
    float3 normal = normalize(input.normal);
    float shade = saturate(dot(normal, -light_dir.xyz));
    return float4(color.rgb * (0.45 + 0.55 * shade), 1);
}
)";

    const char* textured_hlsl = R"(
cbuffer constants : register(b0)
{
    float4x4 world;
    float4x4 view;
    float4x4 projection;
    float4 light_dir;
    float4 color;
    float4 flags;
    float4 eye;
    float4 surface;
    float4 amb_light;
    float4 front_light;
    float4 back_light;
    float4 material;
    float4 spec_light;
};
Texture2D mesh_tex : register(t0);
Texture2D mesh_tex2 : register(t1);
TextureCube mesh_cube : register(t6);
SamplerState mesh_samp : register(s0);
struct input_t
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
};
struct output_t
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
    float3 world : TEXCOORD1;
};
output_t main_vs(input_t input)
{
    output_t output;
    float3 local = input.position;
    float4 world_pos;
    if (flags.w > 0.001 && flags.x < 3.5)
    {
        float sx = length(float3(world._11, world._21, world._31));
        float sz = length(float3(world._13, world._23, world._33));
        float3 center = float3(world._14, world._24, world._34);
        float3 right = float3(view._11, view._12, view._13);
        float3 up = float3(view._21, view._22, view._23);
        world_pos.xyz = center + right * local.x * sx + up * local.z * sz;
        float bend = sin(flags.y * 1.4 + center.x * 0.17) * flags.w * local.z;
        world_pos.xyz += right * bend;
        world_pos.w = 1;
    }
    else
    {
        world_pos = mul(world, float4(local, 1));
        if (flags.z > 0.5)
            world_pos.z += sin(flags.y * 1.3) * 0.35;
    }
    output.position = mul(projection, mul(view, world_pos));
    output.normal = mul(world, float4(input.normal, 0)).xyz;
    output.uv = input.uv;
    output.color = input.color;
    output.world = world_pos.xyz;
    return output;
}
float4 main_ps(output_t input) : SV_Target
{
    if (light_dir.w > 4)
        clip(input.world.z);
    float2 uv = input.uv;
    if (flags.x < -0.5)
        uv.y += flags.y * 0.2;
    float4 texel = mesh_tex.Sample(mesh_samp, uv);
    if (flags.x > 3.5 && flags.x < 4.5)
    {
        float2 tile_uv = uv * max(flags.w, 0.001);
        tile_uv.x += flags.y * surface.z + light_dir.w;
        tile_uv.y += flags.y * flags.z + eye.w;
        float4 tile = mesh_tex2.Sample(mesh_samp, tile_uv);
        texel.rgb *= tile.rgb;
        texel.a *= tile.a;
    }
    if (flags.x > 2.5 && flags.x < 3.5)
        texel = lerp(texel, mesh_tex2.Sample(mesh_samp, uv), input.color.a);
    if (flags.x > 0.5 && flags.x < 2.5)
        clip(texel.a - 0.5);
    float len2 = dot(input.normal, input.normal);
    float3 normal = len2 > 0.0001 ? input.normal * rsqrt(len2) : float3(0, 0, 1);
    // The authored rig: VuAmbientLightEntity plus the two directional
    // colours. The original vertex shader declares gDirLightFrontColor and
    // gDirLightBackColor as separate constants, so the light is two-sided -
    // a surface facing away from the sun takes the back colour rather than
    // going flat ambient.
    float raw = dot(normal, -light_dir.xyz);
    float3 lit = amb_light.rgb + front_light.rgb * saturate(raw) + back_light.rgb * saturate(-raw);
    float3 shade = input.color.rgb * lit;
    if ((flags.x > 1.5 && flags.x < 2.5) || flags.x < -0.5 || (flags.x > 3.5 && flags.x < 4.5))
        shade = input.color.rgb;
    // material.yzw is the material table's DiffuseColor. The retail pixel
    // shader has it folded into the per-vertex diffuse term:
    //   o1.xyz = lit * DiffuseColor * gColor
    // so it tints every lit material, not only the reflective ones.
    float3 rgb = texel.rgb * color.rgb * shade * material.yzw;
    // Recovered from the retail pixel shader (11 instructions, disassembled
    // from the SM4 stream in the model asset):
    //
    //   colour = diffuse.rgb * vertexDiffuse + diffuse.a * vertexSpecular
    //   colour = lerp(colour, cube.rgb, diffuse.a * EnvironmentAmount)
    //   colour = lerp(colour, gFogColor, vertexFog)
    //
    // The blend weight is the diffuse texture's ALPHA times the authored
    // amount - the alpha channel is a gloss mask, driving the reflection and
    // the specular together. That is why EnvironmentAmount is allowed to
    // exceed 1.0 (Cables_whole and zone 09b author 1.10): alpha scales it
    // back under 1 wherever the surface is not fully glossy.
    //
    // SUBSTITUTION, flagged: the retail shader computes the reflection vector
    // and the specular lobe PER VERTEX and interpolates them. Both are per
    // pixel here, which is strictly better looking but not what shipped.
    if (material.x > 0.001 || any(spec_light.rgb))
    {
        float3 incident = normalize(input.world - eye.xyz);
        float3 reflected = reflect(incident, normal);

        // Recovered exactly: max(dot(R, -L), 0) raised to 32 by five
        // successive squarings, scaled by SpecularAmount and tinted by
        // gDirLightSpecularColor * SpecularColor. The diffuse alpha is the
        // gloss mask, so the highlight only appears where the texture says
        // the surface is shiny.
        float lobe = max(dot(reflected, -light_dir.xyz), 0);
        lobe *= lobe;
        lobe *= lobe;
        lobe *= lobe;
        lobe *= lobe;
        lobe *= lobe;
        rgb += texel.a * spec_light.rgb * lobe;

        if (material.x > 0.001)
        {
            float gloss = saturate(texel.a * material.x);
            rgb = lerp(rgb, mesh_cube.Sample(mesh_samp, reflected).rgb, gloss);
        }
    }
    float dist = length(eye.xyz - input.world);
    // Recovered from the retail vertex shader:
    //   o0.z = (length(world - gEyeWorld) - gFogStart) * gFogInvRange
    // computed per vertex and NOT clamped - there is no saturate anywhere in
    // the retail pair, and the pixel shader lerps on the raw interpolated
    // value. Geometry nearer than Fog Start therefore extrapolates slightly
    // AWAY from the fog colour rather than merely being unfogged.
    // INFERRED, flagged: gFogInvRange is taken to be 1/(End - Start). The name
    // and the shape of the formula say so, but the value is set by engine code
    // that has not been read.
    float fog = (dist - surface.x) / max(surface.y - surface.x, 1);
    if ((flags.x > 1.5 && flags.x < 2.5) || (flags.x > 3.5 && flags.x < 4.5))
        fog = 0;
    rgb = lerp(rgb, float3(surface.z, surface.w, eye.w), fog);
    float alpha = flags.x > 3.5 && flags.x < 4.5 ? texel.a * color.a : flags.x > 2.5 && flags.x < 3.5 ? 1.0 : flags.x < -0.5 ? texel.a * input.color.a * 0.55 : input.color.a;
    if (flags.x > 0.15 && flags.x < 0.45)
    {
        clip(texel.a - 0.08);
        alpha = texel.a;
    }
    if (flags.x > 3.5 && flags.x < 4.5)
        alpha *= saturate((dist - surface.x) / max(surface.y - surface.x, 0.001));
    return float4(rgb, alpha);
}
)";

    const char* blit_hlsl = R"(
Texture2D scene_tex : register(t0);
SamplerState scene_samp : register(s0);
struct output_t
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
output_t main_vs(uint id : SV_VertexID)
{
    float2 pos = float2(float((id << 1) & 2), float(id & 2));
    output_t output;
    output.position = float4(pos * float2(2, -2) + float2(-1, 1), 0, 1);
    output.uv = pos;
    return output;
}
float4 main_ps(output_t input) : SV_Target
{
    return scene_tex.Sample(scene_samp, input.uv);
}
)";
}

c_shader& g_shader( )
{
    return g_shader_instance;
}

c_shader::~c_shader( )
{
    release( );
}

void c_shader::release( )
{
#if HYDRO_HOST_WINDOWS
    if ( m_wrap ) m_wrap->Release( );
    if ( m_sampler ) m_sampler->Release( );
    if ( m_blit_ps ) m_blit_ps->Release( );
    if ( m_blit_vs ) m_blit_vs->Release( );
    if ( m_textured_layout ) m_textured_layout->Release( );
    if ( m_textured_ps ) m_textured_ps->Release( );
    if ( m_textured_vs ) m_textured_vs->Release( );
    if ( m_constants ) m_constants->Release( );
    if ( m_layout ) m_layout->Release( );
    if ( m_mesh_ps ) m_mesh_ps->Release( );
    if ( m_mesh_vs ) m_mesh_vs->Release( );
    m_wrap = nullptr;
    m_sampler = nullptr;
    m_blit_ps = nullptr;
    m_blit_vs = nullptr;
    m_textured_layout = nullptr;
    m_textured_ps = nullptr;
    m_textured_vs = nullptr;
    m_constants = nullptr;
    m_constant_bytes = 0;
    m_layout = nullptr;
    m_mesh_ps = nullptr;
    m_mesh_vs = nullptr;
#endif
}

#if HYDRO_HOST_WINDOWS
static bool compile_shader( const char* source, const char* entry, const char* profile, ID3DBlob** blob )
{
    ID3DBlob* errors = nullptr;
    HRESULT result = D3DCompile(
        source,
        strlen( source ),
        nullptr,
        nullptr,
        nullptr,
        entry,
        profile,
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        blob,
        &errors
    );
    if ( FAILED( result ) )
    {
        const char* message = errors ? static_cast< const char* >( errors->GetBufferPointer( ) ) : "unknown";
        LOG_ERROR( "%s %s failed: %s", profile, entry, message );
        if ( errors )
            errors->Release( );
        return false;
    }
    if ( errors )
        errors->Release( );

    // Report where the compiled shader actually places each cbuffer variable.
    // The C++ struct's layout is asserted at compile time, but that says
    // nothing about what HLSL decided, and a mismatch between the two shows
    // up only as a constant that silently reads zero. Asking the shader is
    // the only way to know; guessing the packing already cost two rounds.
    ID3D11ShaderReflection* reflection = nullptr;
    if ( SUCCEEDED( D3DReflect( ( *blob )->GetBufferPointer( ), ( *blob )->GetBufferSize( ),
            __uuidof( ID3D11ShaderReflection ), reinterpret_cast< void** >( &reflection ) ) ) )
    {
        D3D11_SHADER_DESC shader{};
        reflection->GetDesc( &shader );
        for ( UINT index = 0; index < shader.ConstantBuffers; ++index )
        {
            ID3D11ShaderReflectionConstantBuffer* buffer = reflection->GetConstantBufferByIndex( index );
            D3D11_SHADER_BUFFER_DESC described{};
            if ( buffer == nullptr || FAILED( buffer->GetDesc( &described ) ) )
                continue;
            LOG_VERBOSE( "cbuffer %s %s  %u bytes  %u vars", entry, described.Name, described.Size, described.Variables );
            for ( UINT slot = 0; slot < described.Variables; ++slot )
            {
                ID3D11ShaderReflectionVariable* variable = buffer->GetVariableByIndex( slot );
                D3D11_SHADER_VARIABLE_DESC field{};
                if ( variable == nullptr || FAILED( variable->GetDesc( &field ) ) )
                    continue;
                LOG_VERBOSE( "  %-16s offset %3u size %3u %s", field.Name, field.StartOffset, field.Size,
                    ( field.uFlags & D3D_SVF_USED ) != 0 ? "USED" : "unused" );
            }
        }
        reflection->Release( );
    }
    return true;
}
#endif

bool c_shader::create( )
{
#if HYDRO_HOST_WINDOWS
    release( );

    ID3DBlob* mesh_vs = nullptr;
    ID3DBlob* mesh_ps = nullptr;
    ID3DBlob* blit_vs = nullptr;
    ID3DBlob* blit_ps = nullptr;
    ID3DBlob* textured_vs = nullptr;
    ID3DBlob* textured_ps = nullptr;
    if ( !compile_shader( mesh_hlsl, "main_vs", "vs_4_0", &mesh_vs ) ||
         !compile_shader( mesh_hlsl, "main_ps", "ps_4_0", &mesh_ps ) ||
         !compile_shader( textured_hlsl, "main_vs", "vs_4_0", &textured_vs ) ||
         !compile_shader( textured_hlsl, "main_ps", "ps_4_0", &textured_ps ) ||
         !compile_shader( blit_hlsl, "main_vs", "vs_4_0", &blit_vs ) ||
         !compile_shader( blit_hlsl, "main_ps", "ps_4_0", &blit_ps ) )
    {
        if ( mesh_vs ) mesh_vs->Release( );
        if ( mesh_ps ) mesh_ps->Release( );
        if ( textured_vs ) textured_vs->Release( );
        if ( textured_ps ) textured_ps->Release( );
        if ( blit_vs ) blit_vs->Release( );
        if ( blit_ps ) blit_ps->Release( );
        return false;
    }

    ID3D11Device* device = g_device( ).device( );
    device->CreateVertexShader( mesh_vs->GetBufferPointer( ), mesh_vs->GetBufferSize( ), nullptr, &m_mesh_vs );
    device->CreatePixelShader( mesh_ps->GetBufferPointer( ), mesh_ps->GetBufferSize( ), nullptr, &m_mesh_ps );
    device->CreateVertexShader( blit_vs->GetBufferPointer( ), blit_vs->GetBufferSize( ), nullptr, &m_blit_vs );
    device->CreatePixelShader( blit_ps->GetBufferPointer( ), blit_ps->GetBufferSize( ), nullptr, &m_blit_ps );
    device->CreateVertexShader( textured_vs->GetBufferPointer( ), textured_vs->GetBufferSize( ), nullptr, &m_textured_vs );
    device->CreatePixelShader( textured_ps->GetBufferPointer( ), textured_ps->GetBufferSize( ), nullptr, &m_textured_ps );

    D3D11_INPUT_ELEMENT_DESC layout[ 2 ]{};
    layout[ 0 ].SemanticName = "POSITION";
    layout[ 0 ].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    layout[ 0 ].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    layout[ 1 ].SemanticName = "NORMAL";
    layout[ 1 ].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    layout[ 1 ].AlignedByteOffset = 12;
    layout[ 1 ].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    device->CreateInputLayout( layout, 2, mesh_vs->GetBufferPointer( ), mesh_vs->GetBufferSize( ), &m_layout );

    D3D11_INPUT_ELEMENT_DESC textured[ 4 ]{};
    textured[ 0 ].SemanticName = "POSITION";
    textured[ 0 ].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    textured[ 0 ].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    textured[ 1 ].SemanticName = "NORMAL";
    textured[ 1 ].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    textured[ 1 ].AlignedByteOffset = 12;
    textured[ 1 ].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    textured[ 2 ].SemanticName = "TEXCOORD";
    textured[ 2 ].Format = DXGI_FORMAT_R32G32_FLOAT;
    textured[ 2 ].AlignedByteOffset = 24;
    textured[ 2 ].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    textured[ 3 ].SemanticName = "COLOR";
    textured[ 3 ].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    textured[ 3 ].AlignedByteOffset = 32;
    textured[ 3 ].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    device->CreateInputLayout( textured, 4, textured_vs->GetBufferPointer( ), textured_vs->GetBufferSize( ), &m_textured_layout );

    mesh_vs->Release( );
    mesh_ps->Release( );
    textured_vs->Release( );
    textured_ps->Release( );
    blit_vs->Release( );
    blit_ps->Release( );

    // Initial size only; constants() grows this to whatever the largest
    // cbuffer upload actually needs, so no size is hardcoded twice.
    D3D11_BUFFER_DESC constants{};
    constants.ByteWidth = 288;
    constants.Usage = D3D11_USAGE_DYNAMIC;
    constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer( &constants, nullptr, &m_constants );
    m_constant_bytes = 288;

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device->CreateSamplerState( &sampler, &m_sampler );
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    device->CreateSamplerState( &sampler, &m_wrap );

    LOG_SUCCESS( "shaders mesh, textured, water, blit" );
    return m_mesh_vs && m_mesh_ps && m_textured_vs && m_textured_ps && m_textured_layout && m_blit_vs && m_blit_ps && m_layout && m_constants && m_sampler && m_wrap;
#else
    return false;
#endif
}

void c_shader::bind_mesh( )
{
#if HYDRO_HOST_WINDOWS
    ID3D11DeviceContext* context = g_device( ).context( );
    context->IASetInputLayout( m_layout );
    context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    context->VSSetShader( m_mesh_vs, nullptr, 0 );
    context->PSSetShader( m_mesh_ps, nullptr, 0 );
    context->VSSetConstantBuffers( 0, 1, &m_constants );
    context->PSSetConstantBuffers( 0, 1, &m_constants );
#endif
}

void c_shader::bind_textured( )
{
#if HYDRO_HOST_WINDOWS
    ID3D11DeviceContext* context = g_device( ).context( );
    context->IASetInputLayout( m_textured_layout );
    context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    context->VSSetShader( m_textured_vs, nullptr, 0 );
    context->PSSetShader( m_textured_ps, nullptr, 0 );
    context->VSSetConstantBuffers( 0, 1, &m_constants );
    context->PSSetConstantBuffers( 0, 1, &m_constants );
    context->PSSetSamplers( 0, 1, &m_wrap );
#endif
}

void c_shader::bind_blit( )
{
#if HYDRO_HOST_WINDOWS
    ID3D11DeviceContext* context = g_device( ).context( );
    context->IASetInputLayout( nullptr );
    context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    context->VSSetShader( m_blit_vs, nullptr, 0 );
    context->PSSetShader( m_blit_ps, nullptr, 0 );
    context->PSSetSamplers( 0, 1, &m_sampler );
#endif
}

void c_shader::constants( const void* data, int bytes )
{
#if HYDRO_HOST_WINDOWS
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if ( bytes <= 0 )
        return;
    // The buffer grows to fit the upload. It used to be a hardcoded 288 bytes
    // with the request silently clamped to it, which meant that adding float4s
    // to the cbuffer dropped the ones past the cap WITHOUT ANY ERROR: the
    // light rig appeared to work because `ambient` ended at byte 287, while
    // front, back and material fell off the end and read as zero. A shading
    // term that quietly does nothing is the worst possible failure here, so
    // the size is derived now and never typed in.
    const int needed = ( bytes + 15 ) & ~15;
    if ( m_constants == nullptr || m_constant_bytes < needed )
    {
        ID3D11Device* device = g_device( ).device( );
        if ( device == nullptr )
            return;
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast< UINT >( needed );
        description.Usage = D3D11_USAGE_DYNAMIC;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ID3D11Buffer* grown = nullptr;
        if ( FAILED( device->CreateBuffer( &description, nullptr, &grown ) ) )
            return;
        if ( m_constants != nullptr )
            m_constants->Release( );
        m_constants = grown;
        m_constant_bytes = needed;
        // Rebind: the old buffer is gone from the slots that referenced it.
        g_device( ).context( )->VSSetConstantBuffers( 0, 1, &m_constants );
        g_device( ).context( )->PSSetConstantBuffers( 0, 1, &m_constants );
        LOG_INFO( "shader constant buffer grown to %d bytes", needed );
    }
    if ( FAILED( g_device( ).context( )->Map( m_constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
        return;
    memcpy( mapped.pData, data, static_cast< size_t >( bytes ) );
    g_device( ).context( )->Unmap( m_constants, 0 );
#else
    ( void )data;
    ( void )bytes;
#endif
}
