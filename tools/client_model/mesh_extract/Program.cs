// mesh_extract -- read the VOTV cooked, unencrypted v11 pak via CUE4Parse.
// See mesh_extract.csproj header for the WHY + RULES (dev/RE only, never ships).
//
// Verbs:
//   export  <pakDir> <outDir> [assetSubstr ...]
//       Export SkeletalMesh(es) whose .uasset path contains a substring to
//       ActorX .psk + glTF .glb (Blender-openable, carries the rig). Default
//       substr: "meshes/kel/kel_lmao.uasset".
//   scan    <pakDir> <substr>
//       List every USkeletalMesh / USkeleton whose .uasset path contains substr,
//       with bone count + whether hand_R/hand_L exist. No export.
//   imports <pakDir> <pkgSubstr> [classFilter]
//       Load the package whose path contains pkgSubstr and print its import table
//       (referenced assets), optionally filtered to ClassName containing classFilter.
//       Use this to learn which SkeletalMesh asset a BP (e.g. mainPlayer) actually
//       references.

using System;
using System.IO;
using System.Linq;
using CUE4Parse.Encryption.Aes;
using CUE4Parse.FileProvider;
using CUE4Parse.UE4.Assets;
using CUE4Parse.UE4.Assets.Exports;
using CUE4Parse.UE4.Assets.Exports.SkeletalMesh;
using CUE4Parse.UE4.Assets.Exports.Animation;
using CUE4Parse.UE4.Objects.UObject;
using CUE4Parse.UE4.Objects.Core.Misc;
using CUE4Parse.UE4.Versions;
using CUE4Parse_Conversion;
using CUE4Parse_Conversion.Meshes;

if (args.Length < 2)
{
    Console.Error.WriteLine("usage: mesh_extract <export|scan|imports> <pakDir> ...");
    return 2;
}

string verb = args[0].ToLowerInvariant();
string pakDir = args[1];

var provider = new DefaultFileProvider(
    pakDir, SearchOption.AllDirectories, isCaseInsensitive: true,
    new VersionContainer(EGame.GAME_UE4_27));
provider.Initialize();
provider.SubmitKey(new FGuid(), new FAesKey("0x0000000000000000000000000000000000000000000000000000000000000000"));
Console.WriteLine($"[mesh_extract] verb={verb} pakDir={pakDir} mounted={provider.Files.Count}");

static bool PathHas(string key, string needle) =>
    key.Replace('\\', '/').ToLowerInvariant().Contains(needle.Replace('\\', '/').ToLowerInvariant());

static void PrintRig(string tag, System.Collections.Generic.IReadOnlyList<FMeshBoneInfo> bones)
{
    if (bones == null) { Console.WriteLine($"    {tag}: (no bone info)"); return; }
    bool r = bones.Any(b => b.Name.Text == "hand_R");
    bool l = bones.Any(b => b.Name.Text == "hand_L");
    Console.WriteLine($"    {tag}: {bones.Count} bones  hand_R={r} hand_L={l}");
}

if (verb == "imports")
{
    if (args.Length < 3) { Console.Error.WriteLine("imports needs <pkgSubstr>"); return 2; }
    string pkgSubstr = args[2];
    string classFilter = args.Length > 3 ? args[3] : null;
    var key = provider.Files.Keys.FirstOrDefault(k => k.EndsWith(".uasset", StringComparison.OrdinalIgnoreCase) && PathHas(k, pkgSubstr));
    if (key == null) { Console.Error.WriteLine($"no package matching {pkgSubstr}"); return 1; }
    string pkgPath = key[..^".uasset".Length];
    Console.WriteLine($"  package: {pkgPath}");
    var pkg = provider.LoadPackage(pkgPath) as Package;
    if (pkg == null) { Console.Error.WriteLine("  not a legacy Package"); return 1; }

    string Resolve(FPackageIndex idx)
    {
        if (idx == null || idx.IsNull) return "";
        if (idx.IsImport)
        {
            int ai = -idx.Index - 1;
            if (ai < 0 || ai >= pkg.ImportMap.Length) return "?";
            var imp = pkg.ImportMap[ai];
            string outer = Resolve(imp.OuterIndex);
            return outer.Length == 0 ? imp.ObjectName.Text : outer + "." + imp.ObjectName.Text;
        }
        return "(export)";
    }

    int n = 0;
    foreach (var imp in pkg.ImportMap)
    {
        string cls = imp.ClassName.Text;
        if (classFilter != null && !cls.Contains(classFilter, StringComparison.OrdinalIgnoreCase)) continue;
        string outer = Resolve(imp.OuterIndex);
        string full = outer.Length == 0 ? imp.ObjectName.Text : outer + "." + imp.ObjectName.Text;
        Console.WriteLine($"    [{cls}] {full}");
        n++;
    }
    Console.WriteLine($"  {n} import(s){(classFilter != null ? $" matching '{classFilter}'" : "")}");
    return 0;
}

if (verb == "scan")
{
    if (args.Length < 3) { Console.Error.WriteLine("scan needs <substr>"); return 2; }
    string substr = args[2];
    var keys = provider.Files.Keys
        .Where(k => k.EndsWith(".uasset", StringComparison.OrdinalIgnoreCase) && PathHas(k, substr))
        .OrderBy(k => k).ToList();
    Console.WriteLine($"  {keys.Count} .uasset match '{substr}'");
    foreach (var key in keys)
    {
        string pkgPath = key[..^".uasset".Length];
        try
        {
            var pkg = provider.LoadPackage(pkgPath);
            foreach (var exp in pkg.GetExports())
            {
                if (exp is USkeletalMesh sm)
                {
                    Console.WriteLine($"  SkeletalMesh: {pkgPath}");
                    PrintRig("rig", sm.ReferenceSkeleton?.FinalRefBoneInfo);
                }
                else if (exp is USkeleton sk)
                {
                    Console.WriteLine($"  Skeleton:     {pkgPath}");
                    PrintRig("rig", sk.ReferenceSkeleton?.FinalRefBoneInfo);
                }
            }
        }
        catch (Exception ex) { Console.Error.WriteLine($"  (skip {pkgPath}: {ex.GetType().Name})"); }
    }
    return 0;
}

// verb == "export"
if (args.Length < 3) { Console.Error.WriteLine("export needs <outDir>"); return 2; }
string outDir = args[2];
string[] needles = args.Skip(3).ToArray();
if (needles.Length == 0) needles = new[] { "meshes/kel/kel_lmao.uasset" };
Directory.CreateDirectory(outDir);
Console.WriteLine($"  outDir={outDir}");

int exported = 0;
foreach (string needle in needles)
{
    var matches = provider.Files.Keys
        .Where(k => k.EndsWith(".uasset", StringComparison.OrdinalIgnoreCase) && PathHas(k, needle))
        .OrderBy(k => k.Length).ToList();
    if (matches.Count == 0) { Console.Error.WriteLine($"  NOT FOUND: {needle}"); continue; }

    foreach (string key in matches)
    {
        string pkgPath = key[..^".uasset".Length];
        Console.WriteLine($"  loading: {pkgPath}");
        try
        {
            var pkg = provider.LoadPackage(pkgPath);
            var mesh = pkg.GetExports().OfType<USkeletalMesh>().FirstOrDefault();
            if (mesh == null) { Console.Error.WriteLine($"    no USkeletalMesh export"); continue; }
            PrintRig("rig", mesh.ReferenceSkeleton?.FinalRefBoneInfo);

            foreach (EMeshFormat fmt in new[] { EMeshFormat.ActorX, EMeshFormat.Gltf2 })
            {
                try
                {
                    var exporter = new MeshExporter(mesh, new ExporterOptions { MeshFormat = fmt });
                    if (exporter.TryWriteToDir(new DirectoryInfo(outDir), out _, out string saved))
                    {
                        Console.WriteLine($"    EXPORTED [{fmt}] -> {saved}");
                        exported++;
                    }
                    else Console.Error.WriteLine($"    export [{fmt}] returned false");
                }
                catch (Exception fex) { Console.Error.WriteLine($"    export [{fmt}] FAILED: {fex.GetType().Name}: {fex.Message}"); }
            }
        }
        catch (Exception ex) { Console.Error.WriteLine($"    ERROR: {ex.GetType().Name}: {ex.Message}"); }
    }
}
Console.WriteLine($"[mesh_extract] done -- {exported} export(s)");
return exported > 0 ? 0 : 1;
