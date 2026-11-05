#pragma once

#include "CoreMinimal.h"

struct FPostProcessSettings;

/**
 * Settings that make Unreal's output match the CodeParade NonEuclidean engine:
 * materials output the original's display-space colours (sRGB-decoded to linear), and the
 * post-process chain is reduced to a plain linear -> sRGB conversion (manual exposure 1,
 * no filmic tone curve, no bloom/vignette/grain/motion blur).
 *
 * Applied to the player camera now, and to every portal capture in Phase 1, so both
 * sides of a portal go through the identical pipeline.
 */
namespace NEVideoLook
{
	NONEUCLIDEANTEST_API void Apply(FPostProcessSettings& Settings);
}
