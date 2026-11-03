#pragma once

#define LOGGING

#include <impl/engine/engine.hxx>
// The oracle checks need the golden vectors, which live outside the
// repository (workspace/utility/golden.hxx says why); HYDRO_GOLDEN says
// whether this build has them.
#include <workspace/utility/golden.hxx>
#if HYDRO_GOLDEN
#include <workspace/utility/verify.hxx>
#endif
#include <workspace/core/assets/animation.hxx>
#include <workspace/core/game/game.hxx>
// Recreated and oracle-checked, not wired into the game yet: included here so
// they compile in every build, golden data or not.
#include <workspace/core/world/transform.hxx>
#include <workspace/core/boat/parts.hxx>
#if HYDRO_GOLDEN
#include <workspace/utility/check.hxx>
#endif
