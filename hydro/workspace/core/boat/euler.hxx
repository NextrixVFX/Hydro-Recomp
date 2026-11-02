#pragma once

// 0x531850 (__usercall ecx out, edx euler a, xmm3 t; stack euler b; caller pops), the euler slerp under VuBoatEntity
// slot 0x422A10's scripted path (boat batch 29): identity x fromEuler( a ) and identity x fromEuler( b ) (0x410A40,
// 0x435640, 0x4078C0), each to a quaternion (0x531000), slerped (0x5314C0), to a matrix (0x531180), read back as
// ( atan2( m6, m10 ), asinf( clamp( -m2 ) ), atan2( m1, m0 ) ). Three floats are written. Retail stores 1.0 then -1.0 to
// a local before the clamp: a dead store. The read-back is 0x5344A0's arithmetic, so it is transform_euler: SUBSTITUTION
// (named there): the CRT's __libm_sse2_atan2 / asinf served by the host's double functions rounded once; 0x5314C0's acosf
// likewise (frontcam.hxx).
#include <workspace/core/boat/hull.hxx>
#include <workspace/core/boat/math.hxx>
#include <workspace/core/world/frontcam.hxx>
#include <workspace/core/world/transform.hxx>

inline void euler_slerp( float* out, const float* a, const float* b, float t )
{
    float identity[ 16 ], euler[ 16 ], ma[ 16 ], mb[ 16 ], qa[ 4 ], qb[ 4 ], q[ 4 ], m[ 16 ], e[ 3 ];
    vu_identity( identity );
    vu_matrix_from_euler( euler, a );
    hull_multiply( identity, ma, euler );
    vu_identity( identity );
    vu_matrix_from_euler( euler, b );
    hull_multiply( identity, mb, euler );
    quat_from_rows( qa, ma );
    quat_from_rows( qb, mb );
    front_rig_slerp( q, qa, qb, t );
    front_rig_matrix( q, m );
    transform_euler( m, e );
    std::memcpy( out, e, 12 );
}
