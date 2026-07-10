using System.Collections.Generic;
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
    // Build skeleton reference transforms as NewBlendableTransform for additive baking
    var skeletonRefTransforms = new List<NewBlendableTransform>();
    for (int ri = 0; ri < boneCount; ri++)
    {
        var rt = skeleton.Transforms[ri];
        skeletonRefTransforms.Add(new NewBlendableTransform(
            new Vector3(rt.Position.Vector.X, rt.Position.Vector.Y, rt.Position.Vector.Z),
            new Vector3(rt.Scale.Vector.X, rt.Scale.Vector.Y, rt.Scale.Vector.Z),
            new Quaternion(rt.Rotation.Vector.X, rt.Rotation.Vector.Y, rt.Rotation.Vector.Z, rt.Rotation.Vector.W)
        ));
    }
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
var scanOnly = args.Contains("--scan");
if (scanOnly)
{
    Console.WriteLine("name,blend_hint,is_additive,frame_count,track_count,raw_bytes");
}

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
            string objType = obj.GetType().Name;
            if (obj is HKX.HKASplineCompressedAnimation s) { splineAnim = s; }
            else if (obj is HKX.HKAQuantizedAnimation q) { Console.WriteLine($"  [{animName}] HKAQuantizedAnimation (NOT SUPPORTED)"); }
            else if (obj is HKX.HKAInterleavedUncompressedAnimation u) { interleavedAnim = u; }
            else if (obj is HKX.HKAAnimationBinding b) { binding = b; }
            else if (obj is HKX.HKADefaultAnimatedReferenceFrame r) { refFrame = r; }
            else { Console.WriteLine($"  [{animName}] Unknown object: {objType}"); }
        }

        if (binding == null) { Console.WriteLine($"  SKIP [{animName}]: no binding (spline={splineAnim != null}, interleaved={interleavedAnim != null}, refFrame={refFrame != null})"); failed++; continue; }
        Console.WriteLine($"  [{animName}] spline={splineAnim != null}, interleaved={interleavedAnim != null}, binding={binding != null}");

        // Create animation data wrapper (or skip for scan mode)
        bool isAdd = binding.BlendHint == HKX.AnimationBlendHint.ADDITIVE_CHILD_SPACE
                  || binding.BlendHint == HKX.AnimationBlendHint.ADDITIVE_PARENT_SPACE;
        if (scanOnly) {
            int fc = splineAnim?.FrameCount ?? 0;
            int trk = splineAnim?.TransformTrackCount ?? 0;
            float dur = splineAnim?.Duration ?? 0;
            Console.WriteLine($"{animName},{binding.BlendHint},{isAdd},{fc},{trk},{dur:F3}");
            continue;
        }

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
        Console.WriteLine($"  [{animName}] FrameCount={animData.FrameCount}, Duration={animData.Duration}, FrameDuration={animData.FrameDuration}");
        Console.WriteLine($"  [{animName}] HkxBoneIndexToTransformTrackMap Length={animData.HkxBoneIndexToTransformTrackMap?.Length}");
        int mappedBones = animData.HkxBoneIndexToTransformTrackMap?.Count(x => x >= 0) ?? 0;
        Console.WriteLine($"  [{animName}] Mapped bone tracks: {mappedBones}/{animData.HkxBoneIndexToTransformTrackMap?.Length}");
        if (splineAnim != null) Console.WriteLine($"  [{animName}] Spline: BlockCount={splineAnim.BlockCount}, FramesPerBlock={splineAnim.FramesPerBlock}, TransformTrackCount={splineAnim.TransformTrackCount}");


        // Sample animation at target rate
        float duration = animData.Duration;
        int totalSamples = Math.Max(2, (int)(duration * sampleRate) + 1);
        float frameDuration = animData.FrameDuration;

        bool isAdditive = animData.IsAdditiveBlend;
        bool hasRootMotion = animData.RootMotion != null && animData.RootMotion.Frames.Length > 0;

        var frames = new List<object>();
        var rootMotionFrames = new List<object>();
        for (int s = 0; s < totalSamples; s++)
        {
            float time = (s * duration) / (totalSamples - 1);
            float hkxFrame = time / frameDuration;

            var boneTransforms = new List<object>();
            for (int b = 0; b < boneCount; b++)
            {
                var delta = animData.GetTransformOnFrameByBone(b, hkxFrame, false);
                NewBlendableTransform final;
                if (isAdditive)
                {
                    var refT = skeletonRefTransforms[b];
                    // Proper additive parent-space composition:
                    // finalPos = refPos + refRot * deltaPos
                    // finalRot = refRot * deltaRot
                    // finalScl = refScl * deltaScl
                    final.Translation = refT.Translation + Vector3.Transform(delta.Translation, refT.Rotation);
                    final.Rotation = refT.Rotation * delta.Rotation;
                    final.Scale = refT.Scale * delta.Scale;
                }
                else
                {
                    final = delta;
                }
                boneTransforms.Add(new
                {
                    P = new[] { MathF.Round(final.Translation.X, 6), MathF.Round(final.Translation.Y, 6), MathF.Round(final.Translation.Z, 6) },
                    R = new[] { MathF.Round(final.Rotation.X, 6), MathF.Round(final.Rotation.Y, 6), MathF.Round(final.Rotation.Z, 6), MathF.Round(final.Rotation.W, 6) },
                    S = new[] { MathF.Round(final.Scale.X, 6), MathF.Round(final.Scale.Y, 6), MathF.Round(final.Scale.Z, 6) },
                });
            }

            frames.Add(new { BoneTransforms = boneTransforms });

            if (hasRootMotion)
            {
                var rootMotion = animData.RootMotion.GetSampleClamped(time);
                rootMotionFrames.Add(new
                {
                    P = new[] { MathF.Round(rootMotion.X, 6), MathF.Round(rootMotion.Y, 6), MathF.Round(rootMotion.Z, 6) },
                    Yaw = MathF.Round(rootMotion.W, 6),
                });
            }
        }

        animationsOut.Add(new
        {
            Name = animName,
            Duration = duration,
            FrameCount = totalSamples,
            SampleRate = sampleRate,
            IsAdditiveBlend = isAdditive,
            HasRootMotion = hasRootMotion,
            RootMotionFrames = rootMotionFrames,
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
