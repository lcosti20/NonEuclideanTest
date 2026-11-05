using UnrealBuildTool;

public class PortalWorlds : ModuleRules
{
	public PortalWorlds(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore", // SetNearClipPlaneGlobals
			"RHI",        // GShaderPlatformForFeatureLevel (editor-only shader compile wait)
		});
	}
}
