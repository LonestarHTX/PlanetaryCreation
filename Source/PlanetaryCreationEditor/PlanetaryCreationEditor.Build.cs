using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnrealBuildTool;

public class PlanetaryCreationEditor : ModuleRules
{
    public PlanetaryCreationEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        if (Target.Type != TargetType.Editor)
        {
            throw new BuildException("PlanetaryCreationEditor module only supports editor targets.");
        }

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Slate",
            "SlateCore",
            "UnrealEd",
            "EditorFramework"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "UnrealEd",
            "LevelEditor",
            "Projects",
            "RealtimeMeshComponent",
            "InputCore",
            "Blutility",
            "EditorStyle",
            "UMG",
            "WorkspaceMenuStructure",
            "ImageWrapper",
            "Json",
            "JsonUtilities",
            "RHI",                  // Milestone 6: GPU compute shader dependencies
            "RenderCore",
            "Renderer"
        });

        ConfigureStripack(Target);
        ConfigureGeogram(Target);

    }

    private void ConfigureStripack(ReadOnlyTargetRules Target)
    {
        string StripackWin64 = Path.Combine(ModuleDirectory, "../../ThirdParty/stripack/win64");
        string StripackLibraryPath = Path.Combine(StripackWin64, "stripack.lib");

        if (!File.Exists(StripackLibraryPath))
        {
            PublicDefinitions.Add("WITH_STRIPACK=0");
            Console.WriteLine($"PlanetaryCreationEditor: STRIPACK disabled (missing {StripackLibraryPath})");
            return;
        }

        PublicDefinitions.Add("WITH_STRIPACK=1");
        PublicAdditionalLibraries.Add(StripackLibraryPath);
        Console.WriteLine($"PlanetaryCreationEditor: STRIPACK enabled ({StripackLibraryPath})");

        string IntelLibDir = ResolveIntelLibDirectory();
        if (string.IsNullOrEmpty(IntelLibDir))
        {
            throw new BuildException("Unable to locate Intel oneAPI compiler library directory. Set ONEAPI_ROOT or ensure oneAPI (ifx) is installed.");
        }

        Console.WriteLine($"PlanetaryCreationEditor: Intel oneAPI library directory: {IntelLibDir}");

        string[][] RequiredLibCandidates =
        {
            new [] { "ifmodintr.lib" },
            new [] { "ifconsol.lib" },
            new [] { "libifcoremt.lib", "libifcore.lib", "libifcoremd.lib" },
            new [] { "libifportmt.lib", "libifport.lib", "libifportmd.lib" },
            new [] { "libmmt.lib", "libmmd.lib", "libmmds.lib" },
            new [] { "svml_dispmt.lib", "svml_disp.lib" },
            new [] { "libircmt.lib", "libirc.lib" },
            new [] { "libiomp5mt.lib", "libiomp5md.lib" }
        };

        var addedLibs = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        foreach (string[] candidates in RequiredLibCandidates)
        {
            string resolved = ResolveIntelLibrary(IntelLibDir, candidates);
            if (string.IsNullOrEmpty(resolved))
            {
                throw new BuildException($"Required Intel runtime library not found (searched {string.Join(", ", candidates)} under {IntelLibDir}).");
            }

            if (addedLibs.Add(resolved))
            {
                PublicAdditionalLibraries.Add(resolved);
            }
        }

        string[][] OptionalCompanionLibs =
        {
            new [] { "libirc.lib" },
            new [] { "libdecimal.lib" }
        };

        foreach (string[] candidates in OptionalCompanionLibs)
        {
            string resolved = ResolveIntelLibrary(IntelLibDir, candidates);
            if (!string.IsNullOrEmpty(resolved) && addedLibs.Add(resolved))
            {
                PublicAdditionalLibraries.Add(resolved);
            }
        }
    }

    private void ConfigureGeogram(ReadOnlyTargetRules Target)
    {
        string GeogramDir = Path.Combine(ModuleDirectory, "../../ThirdParty/Geogram");
        string GeogramInclude = Path.Combine(GeogramDir, "include");
        string GeogramLibDir = Path.Combine(GeogramDir, "lib", "Win64");
        string[] GeogramLibCandidates =
        {
            Path.Combine(GeogramLibDir, "geogram.lib"),
            Path.Combine(GeogramLibDir, "Geogram.lib"),
            Path.Combine(GeogramLibDir, "geogram_static.lib")
        };

        if (Directory.Exists(GeogramInclude))
        {
            PublicIncludePaths.Add(GeogramInclude);
            
            // Add third_party include paths for zlib and other dependencies
            string GeogramThirdParty = Path.Combine(GeogramInclude, "geogram", "third_party");
            PublicIncludePaths.Add(Path.Combine(GeogramThirdParty, "zlib"));
        }

        string GeogramLibPath = GeogramLibCandidates.FirstOrDefault(File.Exists);
        if (string.IsNullOrEmpty(GeogramLibPath))
        {
            PublicDefinitions.Add("WITH_GEOGRAM=0");
            Console.WriteLine($"PlanetaryCreationEditor: Geogram disabled (no library found under {GeogramLibDir})");
            return;
        }

        PublicDefinitions.Add("WITH_GEOGRAM=1");
        PublicAdditionalLibraries.Add(GeogramLibPath);
        Console.WriteLine($"PlanetaryCreationEditor: Geogram enabled ({GeogramLibPath})");
    }

    private string ResolveIntelLibDirectory()
    {
        var searchDirs = new List<string>();
        string oneApiRoot = Environment.GetEnvironmentVariable("ONEAPI_ROOT");
        if (!string.IsNullOrEmpty(oneApiRoot))
        {
            searchDirs.Add(Path.Combine(oneApiRoot, "compiler", "latest", "windows", "lib"));
            searchDirs.Add(Path.Combine(oneApiRoot, "compiler", "latest", "lib"));
            searchDirs.Add(Path.Combine(oneApiRoot, "compiler", "2025.2", "lib"));
            searchDirs.Add(Path.Combine(oneApiRoot, "lib"));
        }

        string programFilesX86 = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86);
        if (!string.IsNullOrEmpty(programFilesX86))
        {
            searchDirs.Add(Path.Combine(programFilesX86, "Intel", "oneAPI", "compiler", "latest", "windows", "lib"));
            searchDirs.Add(Path.Combine(programFilesX86, "Intel", "oneAPI", "compiler", "latest", "lib"));
            searchDirs.Add(Path.Combine(programFilesX86, "Intel", "oneAPI", "compiler", "2025.2", "lib"));
            searchDirs.Add(Path.Combine(programFilesX86, "Intel", "oneAPI", "2025.2", "lib"));
        }

        var unique = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (string dir in searchDirs)
        {
            if (string.IsNullOrEmpty(dir))
            {
                continue;
            }

            if (!unique.Add(dir))
            {
                continue;
            }

            if (Directory.Exists(dir))
            {
                return Path.GetFullPath(dir);
            }

            string intel64 = Path.Combine(dir, "intel64");
            if (Directory.Exists(intel64))
            {
                return Path.GetFullPath(intel64);
            }

            string intel64Win = Path.Combine(dir, "intel64_win");
            if (Directory.Exists(intel64Win))
            {
                return Path.GetFullPath(intel64Win);
            }
        }

        return string.Empty;
    }

    private string ResolveIntelLibrary(string baseDir, string[] candidateNames)
    {
        foreach (string name in candidateNames)
        {
            string direct = Path.Combine(baseDir, name);
            if (File.Exists(direct))
            {
                Console.WriteLine($"PlanetaryCreationEditor: Linking {direct}");
                return direct;
            }
        }

        foreach (string name in candidateNames)
        {
            try
            {
                string[] matches = Directory.GetFiles(baseDir, name, SearchOption.AllDirectories);
                if (matches.Length > 0)
                {
                    Console.WriteLine($"PlanetaryCreationEditor: Linking {matches[0]}");
                    return matches[0];
                }
            }
            catch (Exception)
            {
                // Ignored — directory traversal can fail on restricted paths.
            }
        }

        return string.Empty;
    }
}
