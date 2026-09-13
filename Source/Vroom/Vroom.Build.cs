using UnrealBuildTool;
using System.IO;
public class Vroom : ModuleRules
{
    public Vroom(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "ProceduralMeshComponent", "Json", "JsonUtilities", "Projects" });
        PrivateDependencyModuleNames.AddRange(new string[] { "SlateCore", "ImageWrapper", "RenderCore", "RHI" });
        bool WithDLSS = Target.Platform == UnrealTargetPlatform.Win64 &&
            Target.Architecture == UnrealArch.X64 &&
            File.Exists(Path.Combine(PluginDirectoryOrProject(), "Plugins", "DLSS", "DLSS.uplugin"));
        PrivateDefinitions.Add("VROOM_WITH_DLSS=" + (WithDLSS ? "1" : "0"));
        if (WithDLSS) PrivateDependencyModuleNames.Add("DLSSBlueprint");
    }
    private string PluginDirectoryOrProject() => Path.GetFullPath(Path.Combine(ModuleDirectory, "..", ".."));
}
