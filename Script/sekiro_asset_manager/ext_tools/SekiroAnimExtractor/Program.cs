using System.Numerics;
using System.Text.Json;
using SoulsAssetPipeline.Animation;
using HKX = SoulsAssetPipeline.Animation.HKX;

if (args.Length < 3)
{
    Console.WriteLine("Usage: SekiroAnimExtractor <skeleton.hkx> <anim_dir> <output.json> [--sample-rate 30] [--filter name]");
    Console.WriteLine("  skeleton.hkx  : HKX skeleton file (from anibnd)");
    Console.WriteLine("  anim_dir      : Directory containing .hkx animation files (searched recursively)");
    Console.WriteLine("  output.json   : Output JSON file with all animation data");
    Console.WriteLine("  --sample-rate : Frames per second for sampling (default: 30)");
    Console.WriteLine("  --filter      : Optional substring or exact animation name to export");
    return;
}

string skeletonPath = args[0];
string animDir = args[1];
string outputPath = args[2];
int sampleRate = 30;
string? filter = null;

for (int i = 3; i < args.Length; i++)
{
    if (args[i] == "--sample-rate" && i + 1 < args.Length)
        sampleRate = int.Parse(args[++i]);
    else if (args[i] == "--filter" && i + 1 < args.Length)
        filter = args[++i];
}

// ---- Load skeleton ----
Console.WriteLine($"Loading skeleton: {skeletonPath}");
byte[] skelBytes = File.ReadAllBytes(skeletonPath);

// Find compendium for skeleton if exists
string? skelCompPath = Directory.GetFiles(Path.GetDirectoryName(skeletonPath)!, "*.compendium").FirstOrDefault();
byte[]? skelCompBytes = skelCompPath != null ? File.ReadAllBytes(skelCompPath) : null;

// Sekiro uses TAG0 format, try GenFakeFromTagFile first
HKX skelHkx;
try
{
    skelHkx = HKX.GenFakeFromTagFile(skelBytes, skelCompBytes);
}
catch
{
    skelHkx = HKX.Read(skelBytes, false);
}
HKX.HKASkeleton? skeleton = null;
foreach (var obj in skelHkx.DataSection.Objects)
{
    if (obj is HKX.HKASkeleton s) { skeleton = s; break; }
}

if (skeleton == null)
{
    Console.WriteLine("ERROR: No skeleton found in HKX file!");
    return;
}

int boneCount = (int)skeleton.Bones.Size;
Console.WriteLine($"Skeleton loaded: {boneCount} bones");

// Build bone name list
var boneNames = new List<string>();
var boneParents = new List<int>();
var boneLocalTransforms = new List<object>();
for (int i = 0; i < boneCount; i++)
{
    boneNames.Add(skeleton.Bones[i].Name.GetString());
    boneParents.Add(skeleton.ParentIndices[i].data);
    var t = skeleton.Transforms[i];
    boneLocalTransforms.Add(new
    {
        P = new[] { t.Position.Vector.X, t.Position.Vector.Y, t.Position.Vector.Z },
        R = new[] { t.Rotation.Vector.X, t.Rotation.Vector.Y, t.Rotation.Vector.Z, t.Rotation.Vector.W },
        S = new[] { t.Scale.Vector.X, t.Scale.Vector.Y, t.Scale.Vector.Z },
    });
}

// ---- Find all animation HKX files ----
var animFiles = Directory.GetFiles(animDir, "*.hkx", SearchOption.AllDirectories)
    .Where(f => !Path.GetFileName(f).Equals("skeleton.hkx", StringComparison.OrdinalIgnoreCase))
    .Where(f => filter == null
        || Path.GetFileNameWithoutExtension(f).Equals(filter, StringComparison.OrdinalIgnoreCase)
        || Path.GetFileNameWithoutExtension(f).Contains(filter, StringComparison.OrdinalIgnoreCase))
    .OrderBy(f => f)
    .ToList();

Console.WriteLine($"Found {animFiles.Count} animation files in {animDir}");
if (filter != null)
    Console.WriteLine($"Filter: {filter}");

// Find compendium files (one per directory)
var compendiumCache = new Dictionary<string, byte[]?>();
byte[]? GetCompendium(string animPath)
{
    string dir = Path.GetDirectoryName(animPath)!;
    if (!compendiumCache.ContainsKey(dir))
    {
        var compFile = Directory.GetFiles(dir, "*.compendium").FirstOrDefault();
        compendiumCache[dir] = compFile != null ? File.ReadAllBytes(compFile) : null;
    }
    return compendiumCache[dir];
}

// ---- Process animations ----
var animationsOut = new List<object>();
int processed = 0, failed = 0;

foreach (var animPath in animFiles)
{
    string animName = Path.GetFileNameWithoutExtension(animPath);

    try
    {
        byte[] animBytes = File.ReadAllBytes(animPath);
        byte[]? compBytes = GetCompendium(animPath);

        // Parse HKX - try TAG format first (Sekiro uses TAG0)
        HKX animHkx;
        try
        {
            animHkx = HKX.GenFakeFromTagFile(animBytes, compBytes);
        }
        catch
        {
            try { animHkx = HKX.Read(animBytes, false); }
            catch { failed++; continue; }
        }

        // Extract animation components
        HKX.HKASplineCompressedAnimation? splineAnim = null;
        HKX.HKAInterleavedUncompressedAnimation? interleavedAnim = null;
        HKX.HKAAnimationBinding? binding = null;
        HKX.HKADefaultAnimatedReferenceFrame? refFrame = null;

        foreach (var obj in animHkx.DataSection.Objects)
        {
            if (obj is HKX.HKASplineCompressedAnimation s) splineAnim = s;
            else if (obj is HKX.HKAInterleavedUncompressedAnimation u) interleavedAnim = u;
            else if (obj is HKX.HKAAnimationBinding b) binding = b;
            else if (obj is HKX.HKADefaultAnimatedReferenceFrame r) refFrame = r;
        }

        if (binding == null) { Console.WriteLine($"  SKIP [{animName}]: no binding"); failed++; continue; }

        // Create animation data wrapper
        HavokAnimationData? animData = null;
        if (splineAnim != null)
        {
            animData = new HavokAnimationData_SplineCompressed(
                0, animName, skeleton, refFrame, binding, splineAnim);
        }
        else if (interleavedAnim != null)
        {
            animData = new HavokAnimationData_InterleavedUncompressed(
                0, animName, skeleton, refFrame, binding, interleavedAnim);
        }

        if (animData == null || animData.FrameCount <= 0) { Console.WriteLine($"  SKIP [{animName}]: no anim data (frames={animData?.FrameCount})"); failed++; continue; }

        // Sample animation at target rate
        float duration = animData.Duration;
        int totalSamples = Math.Max(2, (int)(duration * sampleRate) + 1);
        float frameDuration = animData.FrameDuration;

        var frames = new List<object>();
        for (int s = 0; s < totalSamples; s++)
        {
            float time = (s * duration) / (totalSamples - 1);
            float hkxFrame = time / frameDuration;

            var boneTransforms = new List<object>();
            for (int b = 0; b < boneCount; b++)
            {
                var t = animData.GetTransformOnFrameByBone(b, hkxFrame, false);
                boneTransforms.Add(new
                {
                    P = new[] { MathF.Round(t.Translation.X, 6), MathF.Round(t.Translation.Y, 6), MathF.Round(t.Translation.Z, 6) },
                    R = new[] { MathF.Round(t.Rotation.X, 6), MathF.Round(t.Rotation.Y, 6), MathF.Round(t.Rotation.Z, 6), MathF.Round(t.Rotation.W, 6) },
                    S = new[] { MathF.Round(t.Scale.X, 6), MathF.Round(t.Scale.Y, 6), MathF.Round(t.Scale.Z, 6) },
                });
            }

            frames.Add(new { BoneTransforms = boneTransforms });
        }

        animationsOut.Add(new
        {
            Name = animName,
            Duration = duration,
            FrameCount = totalSamples,
            SampleRate = sampleRate,
            Frames = frames,
        });

        processed++;
        if (processed % 50 == 0)
            Console.WriteLine($"  Processed {processed}/{animFiles.Count} animations...");
    }
    catch (Exception ex)
    {
        Console.Error.WriteLine($"  FAIL [{animName}]: {ex.Message}");
        Console.WriteLine($"  FAIL [{animName}]: {ex.Message}");
        failed++;
    }
}

Console.WriteLine($"Processed: {processed}, Failed: {failed}");

// ---- Output ----
var output = new
{
    BoneCount = boneCount,
    BoneNames = boneNames,
    BoneParents = boneParents,
    BoneLocalTransforms = boneLocalTransforms,
    AnimationCount = animationsOut.Count,
    Animations = animationsOut,
};

Console.WriteLine($"Writing output to: {outputPath}");
using var stream = File.Create(outputPath);
JsonSerializer.Serialize(stream, output, new JsonSerializerOptions { WriteIndented = false, MaxDepth = 256 });
Console.WriteLine($"Done! {animationsOut.Count} animations exported.");
