#pragma once

#include <workspace/core/assets/scene.hxx>
#include <workspace/core/boat/fluid.hxx>
#include <workspace/core/dynamics/row.hxx>

#include <cstdint>
#include <cstring>
#include <xmmintrin.h>

// VuBoatHull's force methods, recreated from retail's disassembly
// (memory/audit.md "Batch 9d"; scratchpad dis_ff.txt). Each is thiscall
// (hull; matrix*, force*, torque*; retn 0Ch) and ADDS into the caller's
// force / torque - even a zero, so -0.0 there becomes +0.0, as retail's.
//
// The hull fields read here: +0 the boat (its +540 the body, +1173 a byte
// that turns the aero force off), +44 .. +52 the aero point (body space),
// +60 .. +68 the per-axis angular damping, +84 the aero factor, +120 the
// slide factor, +148 the aero scale, +460 the fluid solver. The matrix is the
// body's in Vu rows (0x410B40's layout).
struct hull_t
{
    float aero_point[ 3 ];
    float damping[ 3 ];
    float aero;
    float slide;
    float aero_scale;
    bool aero_off;
};

// The VuRigidBody fields these read: +16 the transform, +272 .. +312 the
// world inverse inertia (three 16-byte rows), +320 / +336 the velocities,
// +352 the inverse mass.
struct hull_body_t
{
    float transform[ 16 ];
    float inverse_inertia[ 12 ];
    float linear[ 3 ];
    float angular[ 3 ];
    float inverse_mass;
};

inline float hull_sqrt( float x )   // VuMath_sqrt 0x40B5A0
{
    return _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( x ) ) );
}

// VuBoatHull_applyAeroForces @0x418510 (the matrix argument unread). With
// the solver's volume ORDERED > 0 and the boat's +1173 clear: the wind is
// the body's velocity less the solver's mean flow (+240); F = c * k / |c|
// with c built from the wind by retail's own products with 0.0 (kept: they
// carry NaN / inf), k = |wind|^2 * (aero * 0.5) * aero_scale, F = 0 when
// |c|^2 <= FLT_EPSILON; the torque is r x F with r the aero point turned by
// the body (0x410B40) - retail adds the origin and takes it away again.
inline void hull_aero( const hull_t& h, const hull_body_t& b, const fluid_solver_t& s, float* force, float* torque )
{
    float fx = 0.0f, fy = 0.0f, fz = 0.0f, tx = 0.0f, ty = 0.0f, tz = 0.0f;
    if ( _mm_comigt_ss( _mm_set_ss( s.volume ), _mm_setzero_ps( ) ) && !h.aero_off )
    {
        const float* t = b.transform;
        const float m[ 16 ] = { t[ 0 ], t[ 4 ], t[ 8 ], 0.0f, t[ 1 ], t[ 5 ], t[ 9 ], 0.0f, t[ 2 ], t[ 6 ], t[ 10 ], 0.0f, t[ 12 ], t[ 13 ],
            t[ 14 ], 1.0f };
        const float* flow = s.acc + 36;   // +240
        const float wz = b.linear[ 2 ] - flow[ 2 ];
        const float wx = b.linear[ 0 ] - flow[ 0 ];
        const float wy = b.linear[ 1 ] - flow[ 1 ];
        const float c17 = wz * 0.0f - wx;
        const float ca = wx * 0.0f - wy * 0.0f;
        const float cb = wy - wz * 0.0f;
        const float c18 = ca * wx - cb * wz;
        const float c37 = c17 * wz - ca * wy;
        const float c19 = cb * wy - c17 * wx;
        const float l2 = ( c18 * c18 + c37 * c37 ) + c19 * c19;
        if ( _mm_comigt_ss( _mm_set_ss( l2 ), _mm_set_ss( water_bits( 0x34000000u ) ) ) )
        {
            const float len = hull_sqrt( l2 );
            const float k = ( ( ( wy * wy + wx * wx ) + wz * wz ) * ( h.aero * 0.5f ) ) * h.aero_scale;
            fx = ( c37 * ( 1.0f / len ) ) * k;
            fy = ( c18 * ( 1.0f / len ) ) * k;
            fz = ( c19 * ( 1.0f / len ) ) * k;
        }
        const float p0 = h.aero_point[ 0 ], p1 = h.aero_point[ 1 ], p2 = h.aero_point[ 2 ];
        const float rx = ( ( ( p0 * m[ 0 ] + p1 * m[ 4 ] ) + p2 * m[ 8 ] ) + m[ 12 ] ) - m[ 12 ];
        const float ry = ( ( ( p0 * m[ 1 ] + p1 * m[ 5 ] ) + p2 * m[ 9 ] ) + m[ 13 ] ) - m[ 13 ];
        const float rz = ( ( ( p0 * m[ 2 ] + p1 * m[ 6 ] ) + p2 * m[ 10 ] ) + m[ 14 ] ) - m[ 14 ];
        tx = ry * fz - rz * fy;
        ty = rz * fx - rx * fz;
        tz = rx * fy - ry * fx;
    }
    force[ 0 ] = force[ 0 ] + fx;
    force[ 1 ] = force[ 1 ] + fy;
    force[ 2 ] = force[ 2 ] + fz;
    torque[ 0 ] = torque[ 0 ] + tx;
    torque[ 1 ] = torque[ 1 ] + ty;
    torque[ 2 ] = torque[ 2 ] + tz;
}

// 0x418860 (the force argument unread): the angular velocity taken into the
// matrix's rows, damped per axis (-damping), back to the world, then through
// the INVERSE of the body's world inverse inertia (its cofactors over its
// determinant) into the torque.
inline void hull_damping( const hull_t& h, const hull_body_t& b, const float* a, float* torque )
{
    const float wx = b.angular[ 0 ], wy = b.angular[ 1 ], wz = b.angular[ 2 ];
    const float d0 = -( ( ( a[ 1 ] * wy + wx * a[ 0 ] ) + a[ 2 ] * wz ) * h.damping[ 0 ] );
    const float d1 = -( ( ( a[ 5 ] * wy + wx * a[ 4 ] ) + a[ 6 ] * wz ) * h.damping[ 1 ] );
    const float d2 = -( ( ( a[ 9 ] * wy + wx * a[ 8 ] ) + a[ 10 ] * wz ) * h.damping[ 2 ] );
    const float x = ( d0 * a[ 0 ] + d1 * a[ 4 ] ) + d2 * a[ 8 ];
    const float y = ( a[ 5 ] * d1 + d0 * a[ 1 ] ) + a[ 9 ] * d2;
    const float z = ( a[ 6 ] * d1 + d0 * a[ 2 ] ) + a[ 10 ] * d2;
    const float* r = b.inverse_inertia;
    const float c30 = r[ 5 ] * r[ 10 ] - r[ 6 ] * r[ 9 ];
    const float c32 = r[ 8 ] * r[ 6 ] - r[ 4 ] * r[ 10 ];
    const float c31 = r[ 4 ] * r[ 9 ] - r[ 8 ] * r[ 5 ];
    const float inv = 1.0f / ( ( r[ 0 ] * c30 + r[ 1 ] * c32 ) + r[ 2 ] * c31 );
    const float c28 = ( r[ 0 ] * r[ 5 ] - r[ 1 ] * r[ 4 ] ) * inv;
    const float c22 = ( r[ 1 ] * r[ 8 ] - r[ 0 ] * r[ 9 ] ) * inv;
    const float c23 = ( r[ 2 ] * r[ 4 ] - r[ 0 ] * r[ 6 ] ) * inv;
    const float c34 = ( r[ 0 ] * r[ 10 ] - r[ 2 ] * r[ 8 ] ) * inv;
    const float c24 = ( r[ 1 ] * r[ 6 ] - r[ 2 ] * r[ 5 ] ) * inv;
    const float cx = ( r[ 2 ] * r[ 9 ] - r[ 1 ] * r[ 10 ] ) * inv;
    const float t1 = ( ( y * c34 + cx * x ) + z * c22 ) + torque[ 1 ];
    torque[ 0 ] = torque[ 0 ] + ( ( y * ( c32 * inv ) + ( c30 * inv ) * x ) + z * ( c31 * inv ) );
    torque[ 1 ] = t1;
    torque[ 2 ] = ( ( y * c23 + c24 * x ) + z * c28 ) + torque[ 2 ];
}

// VuBoatHull_applyPowerSlideForces @0x418BD0 (the torque argument unread):
// with the solver's volume ORDERED > 0, the velocity and the matrix's row 1
// both flattened onto the water plane (the solver's normal, +224); c = n x
// the flat velocity; when |c|^2 > FLT_EPSILON the force is c / |c| scaled by
// (c . flat row 1) * slide / the inverse mass, less a quarter of its length
// along the flat velocity's direction.
inline void hull_slide( const hull_t& h, const hull_body_t& b, const fluid_solver_t& s, const float* a, float* force )
{
    if ( !_mm_comigt_ss( _mm_set_ss( s.volume ), _mm_setzero_ps( ) ) )
        return;
    const float* n = s.acc + 32;   // +224
    const float n0 = n[ 0 ], n1 = n[ 1 ], n2 = n[ 2 ];
    const float vx = b.linear[ 0 ], vy = b.linear[ 1 ], vz = b.linear[ 2 ];
    const float d = ( n1 * vy + n0 * vx ) + n2 * vz;
    const float ux = vx - n0 * d, uy = vy - n1 * d, uz = vz - n2 * d;
    const float e = ( n0 * a[ 4 ] + n1 * a[ 5 ] ) + n2 * a[ 6 ];
    const float qx = a[ 4 ] - n0 * e, qy = a[ 5 ] - n1 * e, qz = a[ 6 ] - n2 * e;
    const float c12 = n0 * uz - n2 * ux;
    const float c13 = n2 * uy - n1 * uz;
    const float c14 = n1 * ux - n0 * uy;
    const float l2 = ( c12 * c12 + c13 * c13 ) + c14 * c14;
    if ( !_mm_comigt_ss( _mm_set_ss( l2 ), _mm_set_ss( water_bits( 0x34000000u ) ) ) )
        return;
    const float k = ( ( ( c12 * qy + c13 * qx ) + c14 * qz ) * h.slide ) / b.inverse_mass;
    const float inv = 1.0f / hull_sqrt( l2 );
    const float sy = ( inv * ( n0 * uz - n2 * ux ) ) * k;
    const float sx = ( inv * c13 ) * k;
    const float sz = ( inv * c14 ) * k;
    const float lu = hull_sqrt( ( uy * uy + ux * ux ) + uz * uz );
    const float dx = ( 1.0f / lu ) * ux, dy = ( 1.0f / lu ) * uy, dz = ( 1.0f / lu ) * uz;
    const float q = hull_sqrt( ( sy * sy + sx * sx ) + sz * sz ) * 0.25f;
    force[ 0 ] = force[ 0 ] + ( sx - q * dx );
    force[ 1 ] = force[ 1 ] + ( sy - q * dy );
    force[ 2 ] = force[ 2 ] + ( sz - q * dz );
}

// The hull fields applyFluidForces reads: +28 .. +36 the solver's offset
// (less the boat's +656 .. +664), +76 / +80 the drag / buoyancy bases, +88
// the least factor and +92 the reference speed (mph) of the planar-speed
// factor, +96 .. +108 the velocity-scale curve (two mph / value pairs),
// +112 / +116 the per-axis drag.
struct hull_fluid_t
{
    float offset[ 3 ];
    float boat_centre[ 3 ];
    float drag;
    float buoyancy;
    float factor_min;
    float factor_speed;
    float curve[ 4 ];
    float axis[ 2 ];
};

// VuBoatHull_applyFluidForces @0x418220 (thiscall hull; matrix*, force*,
// torque*; retn 0Ch), from the disassembly:
//   f = 1; when +92 > 0 and 1 > +88 (both ordered): t = |v.xy| / (+92 *
//   0.44704), f = maxss( +88, minss( 1, +88 * t + (1 - t) ) );
//   solver drag / buoyancy = +76 * f / +80 * f;
//   mph = |v| * 2.2369363; velocity scale = +100 while +96 >= mph, else
//   +108 while mph >= +104, else the lerp ((1 - t) * +100 + +108 * t) - a
//   NaN speed takes the lerp (comiss; jb twice);
//   solver offset = +28.. - boat +656..; the per-axis drag is (+112, +116,
//   +112) - retail stores +112 TWICE;
//   prepare( matrix ); solve( body ) when the solver has a mesh; the
//   solver's force (+96) and torque (+112) added into the caller's.
inline bool hull_fluid( const hull_fluid_t& h, const fluid_body_t& body, fluid_solver_t& s, const water_t& water, const float* m,
    float* force, float* torque, water_kernel_fn kernel, void* kernel_user, fluid_gravity_fn gravity, void* gravity_user )
{
    float f = 1.0f;
    if ( _mm_comigt_ss( _mm_set_ss( h.factor_speed ), _mm_setzero_ps( ) ) && _mm_comigt_ss( _mm_set_ss( 1.0f ), _mm_set_ss( h.factor_min ) ) )
    {
        const float planar = hull_sqrt( body.linear[ 0 ] * body.linear[ 0 ] + body.linear[ 1 ] * body.linear[ 1 ] );
        const float t = planar / ( h.factor_speed * water_bits( 0x3EE4E26Du ) );
        f = fluid_max( h.factor_min, fluid_min( 1.0f, h.factor_min * t + ( 1.0f - t ) ) );
    }
    s.drag = h.drag * f;
    s.buoyancy = h.buoyancy * f;
    const float speed = hull_sqrt( ( body.linear[ 0 ] * body.linear[ 0 ] + body.linear[ 1 ] * body.linear[ 1 ] ) + body.linear[ 2 ] * body.linear[ 2 ] );
    const float mph = speed * water_bits( 0x400F29F7u );
    float scale;
    if ( _mm_comige_ss( _mm_set_ss( h.curve[ 0 ] ), _mm_set_ss( mph ) ) )
        scale = h.curve[ 1 ];
    else if ( _mm_comige_ss( _mm_set_ss( mph ), _mm_set_ss( h.curve[ 2 ] ) ) )
        scale = h.curve[ 3 ];
    else
    {
        const float t = ( mph - h.curve[ 0 ] ) / ( h.curve[ 2 ] - h.curve[ 0 ] );
        scale = ( 1.0f - t ) * h.curve[ 1 ] + h.curve[ 3 ] * t;
    }
    s.velocity_scale = scale;
    for ( int k = 0; k < 3; ++k )
        s.offset[ k ] = h.offset[ k ] - h.boat_centre[ k ];
    s.axis_drag[ 0 ] = h.axis[ 0 ];
    s.axis_drag[ 1 ] = h.axis[ 1 ];
    s.axis_drag[ 2 ] = h.axis[ 0 ];
    if ( !fluid_prepare( s, water, m, kernel, kernel_user ) )
        return false;
    if ( s.mesh && !fluid_solve( s, body, gravity, gravity_user ) )
        return false;
    for ( int k = 0; k < 3; ++k )
        force[ k ] = s.acc[ k ] + force[ k ];
    for ( int k = 0; k < 3; ++k )
        torque[ k ] = s.acc[ 4 + k ] + torque[ k ];
    return true;
}

// 0x4157C0 (ecx = a 4x4; plain ret): the matrix inverted IN PLACE through its 2x2
// minors and ONE reciprocal of the determinant - straight-line scalar SSE
// with no branch (a singular matrix gives inf / NaN, as retail). Translated
// operation for operation by tools/sse2c.py from the capstone listing
// (scratchpad dis_sv_cs.txt; IDA hung on this run), operand order kept.
// GENERATED by tools/sse2c.py from C:\Users\john\AppData\Local\Temp\claude\D--Projects-General-Projects-HydroThunderHurricane\9f7ce957-3b8a-4c2b-bebd-9139a8663051\scratchpad\dis_sv_cs.txt - do not edit by hand.
inline void hull_invert( float* m )
{
    float o[ 16 ];
    for ( int k = 0; k < 16; ++k )
        o[ k ] = m[ k ];
    const float t0 = o[ 0 ] * o[ 5 ];
    const float t1 = o[ 1 ] * o[ 4 ];
    const float t2 = t0 - t1;
    const float t3 = o[ 2 ] * o[ 4 ];
    const float t4 = o[ 6 ] * o[ 0 ];
    const float t5 = t4 - t3;
    const float t6 = o[ 3 ] * o[ 4 ];
    const float t7 = o[ 7 ] * o[ 0 ];
    const float t8 = t7 - t6;
    const float t9 = o[ 2 ] * o[ 5 ];
    const float t10 = o[ 6 ] * o[ 1 ];
    const float t11 = t10 - t9;
    const float t12 = o[ 3 ] * o[ 5 ];
    const float t13 = o[ 7 ] * o[ 1 ];
    const float t14 = o[ 7 ] * o[ 2 ];
    const float t15 = t13 - t12;
    const float t16 = o[ 3 ] * o[ 6 ];
    const float t17 = t14 - t16;
    const float t18 = o[ 8 ] * o[ 13 ];
    const float t19 = o[ 9 ] * o[ 12 ];
    const float t20 = t18 - t19;
    const float t21 = o[ 10 ] * o[ 12 ];
    const float t22 = o[ 14 ] * o[ 8 ];
    const float t23 = t22 - t21;
    const float t24 = o[ 11 ] * o[ 12 ];
    const float t25 = o[ 15 ] * o[ 8 ];
    const float t26 = t25 - t24;
    const float t27 = o[ 10 ] * o[ 13 ];
    const float t28 = o[ 14 ] * o[ 9 ];
    const float t29 = t28 - t27;
    const float t30 = o[ 15 ] * o[ 9 ];
    const float t31 = o[ 11 ] * o[ 13 ];
    const float t32 = t30 - t31;
    const float t33 = o[ 15 ] * o[ 10 ];
    const float t34 = o[ 11 ] * o[ 14 ];
    const float t35 = t33 - t34;
    const float t36 = o[ 5 ] * t35;
    const float t37 = o[ 6 ] * t32;
    const float t38 = t36 - t37;
    const float t39 = o[ 7 ] * t29;
    const float t40 = t38 + t39;
    const float t41 = o[ 4 ] * t35;
    const float t42 = o[ 6 ] * t26;
    const float t43 = o[ 6 ] * t20;
    const float t44 = t42 - t41;
    const float t45 = o[ 7 ] * t23;
    const float t46 = o[ 7 ] * t20;
    const float t47 = t44 - t45;
    const float t48 = o[ 5 ] * t26;
    const float t49 = o[ 4 ] * t32;
    const float t50 = o[ 4 ] * t29;
    const float t51 = t49 - t48;
    const float t52 = o[ 1 ] * t35;
    const float t53 = t51 + t46;
    const float t54 = o[ 5 ] * t23;
    const float t55 = t54 - t50;
    const float t56 = t55 - t43;
    const float t57 = o[ 2 ] * t32;
    const float t58 = t57 - t52;
    const float t59 = o[ 3 ] * t29;
    const float t60 = t58 - t59;
    const float t61 = o[ 2 ] * t26;
    const float t62 = o[ 2 ] * t20;
    const float t63 = o[ 0 ] * t35;
    const float t64 = t63 - t61;
    const float t65 = o[ 3 ] * t23;
    const float t66 = o[ 3 ] * t20;
    const float t67 = t64 + t65;
    const float t68 = o[ 0 ] * t32;
    const float t69 = o[ 1 ] * t26;
    const float t70 = t69 - t68;
    const float t71 = o[ 1 ] * t23;
    const float t72 = t70 - t66;
    const float t73 = o[ 0 ] * t29;
    const float t74 = t73 - t71;
    const float t75 = t74 + t62;
    const float t76 = o[ 14 ] * t15;
    const float t77 = o[ 14 ] * t8;
    const float t78 = o[ 13 ] * t17;
    const float t79 = t78 - t76;
    const float t80 = o[ 15 ] * t11;
    const float t81 = t79 + t80;
    const float t82 = o[ 12 ] * t17;
    const float t83 = o[ 12 ] * t15;
    const float t84 = t77 - t82;
    const float t85 = o[ 15 ] * t5;
    const float t86 = o[ 15 ] * t2;
    const float t87 = t84 - t85;
    const float t88 = o[ 13 ] * t8;
    const float t89 = t83 - t88;
    const float t90 = o[ 12 ] * t11;
    const float t91 = t89 + t86;
    const float t92 = o[ 13 ] * t5;
    const float t93 = t92 - t90;
    const float t94 = o[ 14 ] * t2;
    const float t95 = t93 - t94;
    const float t96 = o[ 9 ] * t17;
    const float t97 = o[ 10 ] * t15;
    const float t98 = o[ 8 ] * t17;
    const float t99 = t97 - t96;
    const float t100 = o[ 11 ] * t11;
    const float t101 = t99 - t100;
    const float t102 = o[ 10 ] * t8;
    const float t103 = o[ 9 ] * t8;
    const float t104 = t98 - t102;
    const float t105 = o[ 11 ] * t5;
    const float t106 = o[ 11 ] * t2;
    const float t107 = t104 + t105;
    const float t108 = o[ 8 ] * t15;
    const float t109 = t103 - t108;
    const float t110 = o[ 9 ] * t5;
    const float t111 = t109 - t106;
    const float t112 = o[ 8 ] * t11;
    const float t113 = t112 - t110;
    const float t114 = o[ 10 ] * t2;
    const float t115 = t113 + t114;
    const float t116 = t32 * t5;
    const float t117 = t35 * t2;
    const float t118 = t117 - t116;
    const float t119 = t29 * t8;
    const float t120 = t118 + t119;
    const float t121 = t26 * t11;
    const float t122 = t120 + t121;
    const float t123 = t23 * t15;
    const float t124 = t122 - t123;
    const float t125 = t20 * t17;
    const float t126 = t124 + t125;
    const float t127 = water_bits( 0x3F800000u ) / t126;
    const float t128 = t127 * t40;
    m[ 0 ] = t128;
    const float t129 = t127 * t60;
    m[ 1 ] = t129;
    const float t130 = t127 * t81;
    m[ 2 ] = t130;
    const float t131 = t127 * t101;
    m[ 3 ] = t131;
    const float t132 = t127 * t47;
    m[ 4 ] = t132;
    const float t133 = t127 * t67;
    m[ 5 ] = t133;
    const float t134 = t127 * t87;
    m[ 6 ] = t134;
    const float t135 = t127 * t107;
    m[ 7 ] = t135;
    const float t136 = t127 * t53;
    m[ 8 ] = t136;
    const float t137 = t127 * t72;
    m[ 9 ] = t137;
    const float t138 = t127 * t91;
    m[ 10 ] = t138;
    const float t139 = t127 * t111;
    m[ 11 ] = t139;
    const float t140 = t127 * t56;
    m[ 12 ] = t140;
    const float t141 = t127 * t75;
    m[ 13 ] = t141;
    const float t142 = t127 * t95;
    const float t143 = t127 * t115;
    m[ 14 ] = t142;
    m[ 15 ] = t143;
}

// The fields applySteering reads: hull +0x7C / +0x88 the mph range and
// +0x80 / +0x8C the steer angles (degrees) at its ends, +0xB4 the angle the
// depth blend moves to, +0xCC / +0xD0 that blend's depth range (on the
// solver's least depth +0x5C); boat +0x300 the steer input, +0x308 a flag
// that zeroes it.
struct hull_steer_t
{
    float speed_lo, angle_lo, speed_hi, angle_hi;
    float angle_deep;
    float depth_lo, depth_hi;
    float input;
    uint32_t input_off;
};

// VuBoatHull_applySteering @0x418EE0 (thiscall hull; matrix*; retn 4), from
// the disassembly: the matrix copied and inverted (0x4157C0); s = 0 when
// +0xCC >= depth, 1 when depth >= +0xD0, else the lerp (comiss / jb: a NaN
// depth lerps); u = maxss( 0, minss( 1, (mph - +0x7C) / (+0x88 - +0x7C) ) );
// steer = ((((1 - u) * +0x80) + +0x8C * u) * (1 - s) + +0xB4 * s) *
// 0.017453292; sign = 1 when -input >= 0 (ordered), else -1; the angular
// velocity into the inverse's rows with its third lane REPLACED by (sign *
// steer) * |input|^2 + (1 - |input|^2) * 0.0, then back through the matrix
// into the body's +336 (w 0).
inline void hull_steer( const hull_steer_t& h, const fluid_body_t& body, float depth, const float* a, float* angular )
{
    float c[ 16 ];
    std::memcpy( c, a, 64 );
    hull_invert( c );
    float s;
    if ( _mm_comige_ss( _mm_set_ss( h.depth_lo ), _mm_set_ss( depth ) ) )
        s = 0.0f;
    else if ( _mm_comige_ss( _mm_set_ss( depth ), _mm_set_ss( h.depth_hi ) ) )
        s = 1.0f;
    else
        s = ( depth - h.depth_lo ) / ( h.depth_hi - h.depth_lo );
    const float* v = body.linear;
    const float mph = hull_sqrt( ( v[ 0 ] * v[ 0 ] + v[ 1 ] * v[ 1 ] ) + v[ 2 ] * v[ 2 ] ) * water_bits( 0x400F29F7u );
    const float t = ( mph - h.speed_lo ) / ( h.speed_hi - h.speed_lo );
    const float u = fluid_max( 0.0f, fluid_min( 1.0f, t ) );
    const float steer = ( ( ( ( 1.0f - u ) * h.angle_lo ) + h.angle_hi * u ) * ( 1.0f - s ) + h.angle_deep * s ) * water_bits( 0x3C8EFA35u );
    const float in = h.input_off ? 0.0f : h.input;
    const float neg = -in;
    const float sign = _mm_comige_ss( _mm_set_ss( neg ), _mm_setzero_ps( ) ) ? 1.0f : -1.0f;
    const float mag = water_abs( neg );
    const float* w = body.angular;
    const float l0 = ( c[ 4 ] * w[ 1 ] + c[ 0 ] * w[ 0 ] ) + c[ 8 ] * w[ 2 ];
    const float l1 = ( c[ 5 ] * w[ 1 ] + c[ 1 ] * w[ 0 ] ) + c[ 9 ] * w[ 2 ];
    const float m2 = mag * mag;
    const float l2 = ( sign * steer ) * m2 + ( 1.0f - m2 ) * 0.0f;
    angular[ 0 ] = ( a[ 4 ] * l1 + l0 * a[ 0 ] ) + a[ 8 ] * l2;
    angular[ 1 ] = ( a[ 5 ] * l1 + a[ 1 ] * l0 ) + a[ 9 ] * l2;
    angular[ 2 ] = ( a[ 6 ] * l1 + a[ 2 ] * l0 ) + a[ 10 ] * l2;
    angular[ 3 ] = 0.0f;
}

// 0x410B40 (fastcall out, btTransform*): the basis transposed into Vu rows,
// w 0, and the origin as row 3 with w 1.
inline float* hull_vu_from_bt( float* out, const float* t )
{
    const float m[ 16 ] = { t[ 0 ], t[ 4 ], t[ 8 ], 0.0f, t[ 1 ], t[ 5 ], t[ 9 ], 0.0f, t[ 2 ], t[ 6 ], t[ 10 ], 0.0f, t[ 12 ], t[ 13 ],
        t[ 14 ], 1.0f };
    std::memcpy( out, m, 64 );
    return out;
}

// 0x410AB0 (fastcall out, vec3*): identity with row 3 = (v, 1).
inline float* hull_translation( float* out, const float* v )
{
    const float m[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, v[ 0 ], v[ 1 ], v[ 2 ], 1 };
    std::memcpy( out, m, 64 );
    return out;
}

// VuMatrix_multiply 0x4078C0 (thiscall A; out*, B*; retn 8): out[4i+j] =
// ((A[4i]*B[j] + A[4i+1]*B[4+j]) + A[4i+2]*B[8+j]) + A[4i+3]*B[12+j]
// (the form fluid_solve's oracle verified). out may alias neither input.
// In retail's operand order (scene_multiply, read from 0x4078C0), so a NaN payload matches too; finite results are unchanged.
inline float* hull_multiply( const float* a, float* out, const float* b )
{
    scene_multiply( a, out, b );
    return out;
}

// The views the force methods take, read from the rigid body WHEN THEY ARE
// CALLED (each is a copy made at its call, as retail reads the body then).
inline hull_body_t hull_body_view( const solver_rigid_t& b )
{
    hull_body_t v;
    std::memcpy( v.transform, b.basis, 48 );
    std::memcpy( v.transform + 12, b.origin, 16 );
    std::memcpy( v.inverse_inertia, b.inv_inertia_world, 48 );
    std::memcpy( v.linear, b.linear_velocity, 12 );
    std::memcpy( v.angular, b.angular_velocity, 12 );
    v.inverse_mass = b.inverse_mass;
    return v;
}

inline fluid_body_t fluid_body_view( const solver_rigid_t& b )
{
    fluid_body_t v;
    std::memcpy( v.transform, b.basis, 48 );
    std::memcpy( v.transform + 12, b.origin, 16 );
    std::memcpy( v.linear, b.linear_velocity, 12 );
    std::memcpy( v.angular, b.angular_velocity, 12 );
    v.inverse_mass = b.inverse_mass;
    return v;
}

// What VuBoatHull_update @0x416D20 reaches, for the generated update
// (update.hxx): the hull's own fields by OFFSET where their meaning is not
// read (+0x15C / +0x160 / +0x164 floats, +0x174 a byte flag, +0x178 written),
// the field sets of the five force methods, the boat's rigid body (a real
// solver_rigid_t: retail reads and writes it in place), the solver, the
// water and the hooks; tick_value is the float tickThunk @0x41E5E0 passes
// in XMM1 (its own stack argument - what the tick registration hands it is
// NOT yet read).
struct hull_ctx_t
{
    hull_t hull;
    hull_fluid_t fluid;
    hull_steer_t steer;
    float f15C, f160, f164, f178;
    uint8_t f174;
    solver_rigid_t& b;
    fluid_solver_t& solver;
    const water_t& water;
    water_kernel_fn kernel;
    void* kernel_user;
    fluid_gravity_fn gravity;
    void* gravity_user;
    float tick_value;
};

// What 0x414E90 (on the boat's tick path) reaches - named BY PATH OFFSET, as
// nothing in it says what they are: its own +0x18 / +0x1C; the boat's +0x32C
// byte; the hull's (boat +0x228) solver volume; the object at boat +0x22C
// (+0x4 / +0xC / +0x28) and the one at ITS +0 (+0x30C, the +0x32C byte,
// +0x470); the boat's rigid body.
struct tick_ctx_t
{
    float f18, f1C;
    uint8_t boat_32C;
    const float* volume;   // the solver's, read where 0x414E90 reads it
    float o22C_04, o22C_0C, o22C_28;
    float o22C0_30C, o22C0_470;
    uint8_t o22C0_32C;
    solver_rigid_t& b;
};

// 0x40B5B0 (usercall lo xmm0, hi xmm1, x xmm2 -> xmm0): `comiss lo, x; jb` ->
// 0 when lo >= x (ordered), else `comiss x, hi; jb` -> 1 when x >= hi
// (ordered), else (x - lo) / (hi - lo) - a NaN takes the division.
inline float vu_linstep( float lo, float hi, float x )
{
    if ( _mm_comige_ss( _mm_set_ss( lo ), _mm_set_ss( x ) ) )
        return 0.0f;
    if ( _mm_comige_ss( _mm_set_ss( x ), _mm_set_ss( hi ) ) )
        return 1.0f;
    return ( x - lo ) / ( hi - lo );
}

// What 0x41D570 reads on the boat - named BY OFFSET (nothing in it says what
// they are): the +0x2FC float, the +0x308 dword, the +0x32C / +0x32F bytes, the
// vector at +0x60 of the object at +0x50, the +0x22C object's +0x4 / +0xC and
// its +0's +0x470, the rigid body's velocity.
struct boat_view_t
{
    float f2FC;
    uint32_t i308;
    uint8_t b32C, b32F;
    float o50_60[ 3 ];
    float o22C_04, o22C_0C, o22C0_470;
    solver_rigid_t& b;
};

// The object at boat +0x22C as 0x415220 reads it - named BY OFFSET: its +0x4 ..
// +0x2C floats, the float table at +0x30 with its count at +0x34, and through
// its +0 the +0x30C float, the +0x32C byte and the +0x470 float (the same
// fields 0x414E90 reaches through boat +0x22C).
struct o22c_view_t
{
    float f04, f0C, f10, f20, f28, f2C;
    const float* table30;
    int32_t count34;
    float p30C, p470;
    uint8_t p32C;
};

// What 0x414760 reaches - named BY OFFSET. Its `this` is the object at boat
// +0x22C (0x415220's `o`): +0x14 / +0x24 floats, the container of 32-byte
// records at +0x3C .. +0x40 (retail's begin / end pointers, kept as bytes), the
// 8-byte records at +0xF8 (a byte, then a float at +4); the hull's (boat
// +0x228) +0x1DC / +0x1E0, two waves the water query excludes (an index, -1
// for retail's null); the boat as 0x41D570 sees it; the body; the water and
// the wave kernel hook.
struct b4760_ctx_t
{
    float f14, f24;
    uint8_t* points;
    uint8_t* points_end;
    uint8_t* records;
    int32_t exclude0, exclude1;
    solver_rigid_t& b;
    boat_view_t& boat;
    const o22c_view_t& o;
    const water_t& water;
    water_kernel_fn kernel;
    void* kernel_user;
};

// What 0x414010 reaches: the same object O (for 0x414760 as `rec`, for
// 0x414E90 as `tick`), the boat's +0x290 .. +0x298 (the hull fluid's boat
// centre) and, through rec, the body's btTransform.
struct b4010_ctx_t
{
    const o22c_view_t& o;
    const float* centre;
    b4760_ctx_t& rec;
    tick_ctx_t& tick;
};

// What 0x4219C0 reaches on the boat - named BY OFFSET: the +0x134, +0x3D8 and
// +0x3DC floats, the euler angles at +0x3E8 (radians, VuMatrix_fromEuler's
// input) and the body.
struct b19c0_ctx_t
{
    float f134, f3D8, f3DC;
    float euler[ 3 ];
    solver_rigid_t& b;
};

// What tickThunk 0x41E5E0 reaches (its this = boat +0x60): the hull update's
// context (+0x1C8 = boat +0x228), 0x414010's (+0x1CC = boat +0x22C) and the
// +0x374 byte (boat +0x3D4) that gates 0x4219C0 (on the boat: b19c0).
struct thunk_ctx_t
{
    hull_ctx_t& hull;
    b4010_ctx_t& body;
    b19c0_ctx_t& b19c0;
    uint8_t b374;
};
