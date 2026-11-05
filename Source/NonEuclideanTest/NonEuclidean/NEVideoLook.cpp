#include "NEVideoLook.h"

#include "Engine/Scene.h"

void NEVideoLook::Apply(FPostProcessSettings& S)
{
	// Exposure fixed at 1.0
	S.bOverride_AutoExposureMethod = true;
	S.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = 0.0f;
	S.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	S.AutoExposureApplyPhysicalCameraExposure = 0;

	// Tonemapper reduced to linear -> sRGB
	S.bOverride_ToneCurveAmount = true;
	S.ToneCurveAmount = 0.0f;
	S.bOverride_ExpandGamut = true;
	S.ExpandGamut = 0.0f;
	S.bOverride_BlueCorrection = true;
	S.BlueCorrection = 0.0f;
	S.bOverride_LocalExposureHighlightContrastScale = true;
	S.LocalExposureHighlightContrastScale = 1.0f; // project default is 0.8
	S.bOverride_LocalExposureShadowContrastScale = true;
	S.LocalExposureShadowContrastScale = 1.0f;

	// No lens / camera effects
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = 0.0f;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.0f;
	S.bOverride_FilmGrainIntensity = true;
	S.FilmGrainIntensity = 0.0f;
	S.bOverride_SceneFringeIntensity = true;
	S.SceneFringeIntensity = 0.0f;
	S.bOverride_LensFlareIntensity = true;
	S.LensFlareIntensity = 0.0f;
	S.bOverride_MotionBlurAmount = true;
	S.MotionBlurAmount = 0.0f;
	S.bOverride_AmbientOcclusionIntensity = true;
	S.AmbientOcclusionIntensity = 0.0f;
}
