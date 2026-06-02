using UnrealBuildTool;

public class FastDepthTap : ModuleRules
{
    public FastDepthTap(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core","CoreUObject","Engine","RenderCore","RHI","Networking","Sockets","ImageWrapper"
        });
        PrivateDependencyModuleNames.AddRange(new string[] {
            "Slate","SlateCore"
        });
        bUseUnity = true;
    }
}