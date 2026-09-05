using UnrealBuildTool;
public class Vroom : ModuleRules
{
    public Vroom(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "ProceduralMeshComponent", "Json", "JsonUtilities", "Projects" });
        PrivateDependencyModuleNames.Add("SlateCore");
    }
}
