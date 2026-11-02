#pragma once

// VuDrawManager handler. Tick takes no arguments, same as the original virtual +8.
class c_draw
{
public:
    virtual ~c_draw( ) = default;
    virtual void tick( ) = 0;
};

enum class shape_t
{
    box,
    plane
};

struct mesh_vertex_t
{
    float position[ 3 ];
    float normal[ 3 ];
    float uv[ 2 ];
    float color[ 4 ];
};

class c_node
{
public:
    virtual ~c_node( ) = default;

    shape_t m_shape = shape_t::box;
    float m_position[ 3 ]{};
    float m_yaw = 0.0f;
    float m_scale[ 3 ]{ 1.0f, 1.0f, 1.0f };
    float m_color[ 3 ]{ 1.0f, 1.0f, 1.0f };
};

// One placement the sorted pass draws (render/mesh.hxx): its index for `draw`, retail's sort type (the shader's +0x20, the layer
// of the table 0x735A00), the texture, its world matrix and the mesh's aabb (min xyzw, max xyzw; null: none).
struct sorted_item_t
{
    int index;
    int type;
    int texture;
    const float* world;
    const float* box;
};

// The pass handed to the game's sort. `draw` draws one item on the state the caller has committed.
struct sorted_pass_t
{
    const sorted_item_t* items;
    int count;
    const float* eye;
    void* draw_user;
    void ( *draw )( void* draw_user, int index );
};

class c_engine
{
public:
    static void attach( c_draw& draw );
    // The D3D11 device and context as opaque pointers (null with no device): hydro's render/direct.hxx backend.
    static void api( void*& device, void*& context );
    // Installs the game's sorted pass, called once per c_geometry::draw in place of the blend passes.
    static void sorted( void ( *pass )( void* user, const sorted_pass_t& pass_data ), void* user );
    // Retail's sort type of a placement's material, its shading (0 lit, 1 the vertex colour, unlit), the shader's DiffuseAlpha
    // and the mesh's aabb (8 floats, or null).
    static bool layer( int placement, int type, int shading, float diffuse_alpha, const float* box );
    static void add( c_node& node );
    static void look( const float position[ 3 ] );
    static void camera( const float* eye, const float* target, float far_plane );
    static void sun( const float* direction );
    // The authored light rig: ambient, directional front, directional back.
    static void rig( const float* ambient, const float* front, const float* back );
    static void fog( float start, float end, float red, float green, float blue );
    static void clock( float seconds );
    static void guard( );
    static void screen( const unsigned char* rgba, int width, int height );
    static int geometry( const mesh_vertex_t* vertices, int vertex_count, const unsigned short* indices, int index_count );
    static int texture( const unsigned char* rgba, int width, int height );
    // Six square RGBA faces in asset order; returns a cube handle for place().
    static int cube( const unsigned char* const* faces, int face );
    // Attach a material's authored shading constants to a placement.
    static bool shade( int placement, int cube, const float* specular, float specular_amount, float environment_amount, const float* diffuse = nullptr );
    // Returns a stable placement handle for move(), or -1 if rejected.
    static int place( int geometry, int texture, int index_start, int index_count, const float* world, const float* color, int alpha_test, const float* surface = nullptr, int detail = -1, float scroll = 0.0f, const float* bias = nullptr );
    static void drop( int alpha_test );
    static void lift( int alpha_test, float extra );
    // Repoint a placement at a new world matrix: the cheap way to animate a
    // rigid object, instead of CPU-transforming and re-uploading its vertices.
    static bool move( int placement, const float* world );
    static bool show( int placement, bool visible );
    static mesh_vertex_t* edit( int geometry, int& count );
    static void refresh( int geometry );
    static void stream( int geometry );
    static void revise( int texture, const unsigned char* rgba, int width, int height );
    static void arguments( int count, char** values );
    static const char* option( const char* name );
    static int run( const char* title );
};
