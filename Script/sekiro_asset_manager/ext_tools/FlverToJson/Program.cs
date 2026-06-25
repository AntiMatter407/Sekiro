using System.Numerics;
using System.Text.Json;
using System.Linq;
using SoulsFormats;
using SoulsAssetPipeline.Animation;

// ---- Parse arguments ----
if (args.Length < 2)
{
    Console.WriteLine("Usage: FlverToFbx <skeleton.flver> <body1.flver> [body2.flver ...] [-o output.json] [--skeleton-hkx <path>]");
    Console.WriteLine("  Merges skeleton hierarchy with multiple body part meshes into JSON for Blender.");
    Console.WriteLine("  -o              Output path (default: Sekiro_model.json in current directory)");
    Console.WriteLine("  --skeleton-hkx  Path to HKX skeleton file for quaternion-based reference pose");
    return;
}

string skeletonPath = args[0];
string outputPath = "Sekiro_model.json";
string? skeletonHkxPath = null;
var bodyPaths = new List<string>();

for (int a = 1; a < args.Length; a++)
{
    if (args[a] == "-o" && a + 1 < args.Length)
    {
        outputPath = args[++a];
    }
    else if (args[a] == "--skeleton-hkx" && a + 1 < args.Length)
    {
        skeletonHkxPath = args[++a];
    }
    else
    {
        bodyPaths.Add(args[a]);
    }
}

// Exclude HD_M parts — hollow head models; real head geometry comes from FC_M (Face)
bodyPaths.RemoveAll(bp => Path.GetFileNameWithoutExtension(bp).StartsWith("HD_M", StringComparison.OrdinalIgnoreCase));

if (bodyPaths.Count == 0)
{
    Console.WriteLine("ERROR: At least one body FLVER file is required.");
    return;
}

// ---- Load skeleton ----
var skeletonFlver = FLVER2.Read(skeletonPath);
Console.WriteLine($"Skeleton: {skeletonFlver.Nodes.Count} bones");

// ---- Load HKX skeleton (optional, for quaternion reference pose) ----
var hkxTransforms = new Dictionary<string, (Vector3 pos, Quaternion rot, Vector3 scale)>();
var hkxParentMap = new Dictionary<string, string?>();
bool useHkx = false;

if (skeletonHkxPath != null && File.Exists(skeletonHkxPath))
{
    Console.WriteLine($"Loading HKX skeleton: {skeletonHkxPath}");
    try
    {
        byte[] skelBytes = File.ReadAllBytes(skeletonHkxPath);
        string? skelCompPath = Directory.GetFiles(Path.GetDirectoryName(skeletonHkxPath)!, "*.compendium").FirstOrDefault();
        byte[]? skelCompBytes = skelCompPath != null ? File.ReadAllBytes(skelCompPath) : null;

        HKX skelHkx;
        try { skelHkx = HKX.GenFakeFromTagFile(skelBytes, skelCompBytes); }
        catch { skelHkx = HKX.Read(skelBytes, false); }

        HKX.HKASkeleton? hkxSkeleton = null;
        foreach (var obj in skelHkx.DataSection.Objects)
        {
            if (obj is HKX.HKASkeleton s) { hkxSkeleton = s; break; }
        }

        if (hkxSkeleton != null)
        {
            int hkxBoneCount = (int)hkxSkeleton.Bones.Size;
            Console.WriteLine($"HKX skeleton loaded: {hkxBoneCount} bones");

            var hkxIdxToName = new List<string>();
            for (int i = 0; i < hkxBoneCount; i++)
                hkxIdxToName.Add(hkxSkeleton.Bones[i].Name.GetString());

            for (int i = 0; i < hkxBoneCount; i++)
            {
                string name = hkxIdxToName[i];
                var t = hkxSkeleton.Transforms[i];
                hkxTransforms[name] = (
                    new Vector3(t.Position.Vector.X, t.Position.Vector.Y, t.Position.Vector.Z),
                    new Quaternion(t.Rotation.Vector.X, t.Rotation.Vector.Y, t.Rotation.Vector.Z, t.Rotation.Vector.W),
                    new Vector3(t.Scale.Vector.X, t.Scale.Vector.Y, t.Scale.Vector.Z)
                );
                int parentIdx = hkxSkeleton.ParentIndices[i].data;
                hkxParentMap[name] = parentIdx >= 0 && parentIdx < hkxBoneCount ? hkxIdxToName[parentIdx] : null;
            }

            useHkx = true;
        }
        else
        {
            Console.WriteLine("WARNING: No HKASkeleton found in HKX file, using FLVER transforms.");
        }
    }
    catch (Exception ex)
    {
        Console.WriteLine($"WARNING: Failed to load HKX skeleton: {ex.Message}. Using FLVER transforms.");
    }
}

// ---- Build combined bone lookup from skeleton + all body parts ----
var combinedParentIdx = new Dictionary<string, string?>();
var combinedLocalTransform = new Dictionary<string, (Vector3 pos, Vector3 rot, Vector3 scale)>();

// Skeleton FLVER takes priority
for (int i = 0; i < skeletonFlver.Nodes.Count; i++)
{
    var n = skeletonFlver.Nodes[i];
    combinedParentIdx[n.Name] = n.ParentIndex >= 0 && n.ParentIndex < skeletonFlver.Nodes.Count
        ? skeletonFlver.Nodes[n.ParentIndex].Name : null;
    combinedLocalTransform[n.Name] = (n.Translation, n.Rotation, n.Scale);
}

// ---- Load all body parts ----
var allBodyFlvers = new List<(string partName, FLVER2 flver)>();
foreach (var bp in bodyPaths)
{
    var flver = FLVER2.Read(bp);
    string partName = Path.GetFileNameWithoutExtension(bp);
    allBodyFlvers.Add((partName, flver));
    Console.WriteLine($"Part [{partName}]: {flver.Nodes.Count} bones, {flver.Meshes.Count} meshes, {flver.Materials.Count} materials");
}

// Body FLVERs fill in gaps (bones not in skeleton, e.g. hair bones)
foreach (var (_, bodyFlver) in allBodyFlvers)
{
    for (int i = 0; i < bodyFlver.Nodes.Count; i++)
    {
        var n = bodyFlver.Nodes[i];
        if (!combinedParentIdx.ContainsKey(n.Name))
        {
            combinedParentIdx[n.Name] = n.ParentIndex >= 0 && n.ParentIndex < bodyFlver.Nodes.Count
                ? bodyFlver.Nodes[n.ParentIndex].Name : null;
        }
        if (!combinedLocalTransform.ContainsKey(n.Name))
        {
            combinedLocalTransform[n.Name] = (n.Translation, n.Rotation, n.Scale);
        }
    }
}

Console.WriteLine($"Combined bone lookup: {combinedParentIdx.Count} bones");

// ---- Collect all used bone names across all parts ----
var usedBoneNames = new HashSet<string>();
foreach (var (partName, bodyFlver) in allBodyFlvers)
{
    foreach (var mesh in bodyFlver.Meshes)
    {
        var palette = new Dictionary<int, string>();
        if (mesh.BoneIndices.Count > 0)
        {
            for (int i = 0; i < mesh.BoneIndices.Count; i++)
            {
                int ni = mesh.BoneIndices[i];
                if (ni >= 0 && ni < bodyFlver.Nodes.Count)
                    palette[i] = bodyFlver.Nodes[ni].Name;
            }
        }
        else
        {
            for (int i = 0; i < bodyFlver.Nodes.Count; i++)
                palette[i] = bodyFlver.Nodes[i].Name;
        }

        foreach (var v in mesh.Vertices)
        {
            for (int j = 0; j < 4; j++)
            {
                if (v.BoneWeights[j] > 0 && palette.TryGetValue(v.BoneIndices[j], out var name))
                    usedBoneNames.Add(name);
            }
        }
    }
}

// Walk up hierarchy to include ancestors
var allUsedWithAncestors = new HashSet<string>(usedBoneNames);
foreach (var name in usedBoneNames.ToList())
{
    var current = name;
    while (current != null && combinedParentIdx.ContainsKey(current))
    {
        allUsedWithAncestors.Add(current);
        current = combinedParentIdx[current];
    }
}

Console.WriteLine($"Bones referenced by vertices: {usedBoneNames.Count}");
Console.WriteLine($"With ancestors: {allUsedWithAncestors.Count}");

// ---- Bone transform helpers ----
(Vector3 pos, Vector3 rot, Vector3 scale) GetLocalTransform(string name)
{
    if (combinedLocalTransform.TryGetValue(name, out var lt))
        return lt;
    return (Vector3.Zero, Vector3.Zero, Vector3.One);
}

string? GetParentName(string name)
{
    if (useHkx && hkxParentMap.TryGetValue(name, out var hkxParent))
        return hkxParent;
    // Check combined lookup (skeleton + body FLVERs)
    if (combinedParentIdx.TryGetValue(name, out var parentName))
        return parentName;
    return null;
}

// ---- Compute world transforms ----
var boneWorldCache = new Dictionary<string, (Vector3 pos, Quaternion rot, Vector3 scale, Matrix4x4 mat)>();

(Vector3 pos, Quaternion rot, Vector3 scale, Matrix4x4 mat) ComputeBoneWorld(string name, HashSet<string>? visited = null)
{
    if (boneWorldCache.TryGetValue(name, out var cached))
        return cached;

    visited ??= new HashSet<string>();
    if (visited.Contains(name))
        return (Vector3.Zero, Quaternion.Identity, Vector3.One, Matrix4x4.Identity);
    visited.Add(name);

    var localMat = Matrix4x4.Identity;

    if (useHkx && hkxTransforms.TryGetValue(name, out var hkxT))
    {
        // Use HKX quaternion data — matches SekiroAnimExtractor's A_ref path
        localMat *= Matrix4x4.CreateScale(hkxT.scale.X, hkxT.scale.Y, hkxT.scale.Z);
        localMat *= Matrix4x4.CreateFromQuaternion(hkxT.rot);
        localMat *= Matrix4x4.CreateTranslation(hkxT.pos.X, hkxT.pos.Y, hkxT.pos.Z);
    }
    else
    {
        // Fallback: FLVER Euler angle path (original behavior)
        var (localPos, localRot, localScale) = GetLocalTransform(name);
        localMat *= Matrix4x4.CreateScale(localScale.X, localScale.Y, localScale.Z);
        localMat *= Matrix4x4.CreateRotationX(localRot.X);
        localMat *= Matrix4x4.CreateRotationZ(localRot.Z);
        localMat *= Matrix4x4.CreateRotationY(localRot.Y);
        localMat *= Matrix4x4.CreateTranslation(localPos.X, localPos.Y, localPos.Z);
    }

    var parentName = GetParentName(name);
    if (parentName != null)
    {
        var parentResult = ComputeBoneWorld(parentName, visited);
        localMat = localMat * parentResult.mat;
    }

    Vector3 worldPos = localMat.Translation;
    Quaternion worldRot = Quaternion.CreateFromRotationMatrix(localMat);
    Vector3 worldScale = new(
        new Vector3(localMat.M11, localMat.M12, localMat.M13).Length(),
        new Vector3(localMat.M21, localMat.M22, localMat.M23).Length(),
        new Vector3(localMat.M31, localMat.M32, localMat.M33).Length()
    );

    var result = (worldPos, worldRot, worldScale, localMat);
    boneWorldCache[name] = result;
    return result;
}

foreach (var name in allUsedWithAncestors)
    ComputeBoneWorld(name);

// ---- Output bones ----
var bonesOut = new List<object>();
foreach (var name in allUsedWithAncestors)
{
    var (lPos, lRot, lScale) = GetLocalTransform(name);
    var (wPos, wRot, wScale, _) = boneWorldCache[name];
    var parentName = GetParentName(name);

    bonesOut.Add(new
    {
        Name = name,
        ParentName = parentName,
        LocalPos = new[] { lPos.X, lPos.Y, lPos.Z },
        LocalRot = new[] { lRot.X, lRot.Y, lRot.Z },
        LocalScale = new[] { lScale.X, lScale.Y, lScale.Z },
        WorldPos = new[] { wPos.X, wPos.Y, wPos.Z },
        WorldRot = new[] { wRot.X, wRot.Y, wRot.Z, wRot.W },
        WorldScale = new[] { wScale.X, wScale.Y, wScale.Z },
    });
}

// ---- Extract materials and meshes from ALL parts ----
var materialsOut = new List<object>();
var meshesOut = new List<object>();
var resolvedMaterialsOut = new List<object>();
int globalMatOffset = 0;

// Helper: convert game MTD path to local filesystem path.
// Extracted MTDs live directly in Extracted\mtd\*.mtd (no parts/ subdir for Sekiro).
string? ResolveMtdPath(string mtdPath)
{
    var parts = mtdPath.Replace('\\', '/').Split('/');
    var idx = Array.FindIndex(parts, p => p == "mtd");
    if (idx < 0) return null;
    // BaseDirectory is .../bin/Release/net9.0-windows/ — go up 6 levels to project root
    var projectRoot = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "..", "..", "..", "..", "..");
    var mtdBase = Path.Combine(projectRoot, "Extracted", "mtd");
    var fileName = parts[^1]; // P_BD_M_9000_tops1.mtd

    // Try directly: Extracted/mtd/P_*.mtd
    var direct = Path.Combine(mtdBase, fileName);
    if (File.Exists(direct)) return direct;

    // Try with subdirs: Extracted/mtd/parts/P_*.mtd
    var subPath = string.Join(Path.DirectorySeparatorChar.ToString(), parts.Skip(idx + 1));
    var nested = Path.Combine(mtdBase, subPath);
    return File.Exists(nested) ? nested : null;
}

// Helper: get available textures from a part's TPF directory
List<string> GetTpfTextures(string bodyPath, string partName)
{
    var result = new List<string>();
    var bodyDir = Path.GetDirectoryName(bodyPath);
    if (bodyDir == null) return result;

    // Try TPF subdirectory naming conventions
    // Also strip trailing _N numeric suffix for multi-part weapons (WP_A_0300_1 -> WP_A_0300)
    var candidates = new List<string> {
        Path.Combine(bodyDir, $"{partName}-tpf"),
        Path.Combine(bodyDir, "..", $"{partName}-tpf"),
    };
    int underscoreIdx = partName.LastIndexOf('_');
    if (underscoreIdx > 0 && underscoreIdx < partName.Length - 1 &&
        char.IsDigit(partName[underscoreIdx + 1]))
    {
        var baseName = partName.Substring(0, underscoreIdx);
        candidates.Add(Path.Combine(bodyDir, $"{baseName}-tpf"));
        candidates.Add(Path.Combine(bodyDir, "..", $"{baseName}-tpf"));
    }

    foreach (var dir in candidates)
    {
        if (!Directory.Exists(dir)) continue;
        foreach (var f in Directory.GetFiles(dir, "*.*"))
        {
            var ext = Path.GetExtension(f).ToLower();
            if (ext is ".dds" or ".png")
                result.Add(Path.GetFileName(f));
        }
    }

    return result;
}

// ============================================================================
// Material resolution helpers — DSAnimStudio + Blender heuristic rules
// ============================================================================

// Cloth keywords from Blender common_blender.py:393
string[] ClothKeywords = { "cloth", "fray", "tiling", "bandage", "muffler", "rope", "skirt", "cape", "hair", "fur" };

/// Map shader ParamName to texture semantic suffix
string? ParamNameToSuffix(string paramName)
{
    if (string.IsNullOrEmpty(paramName)) return null;
    var low = paramName.ToLower();
    if (low.Contains("albedomap") || low.Contains("diffusemap") || low.Contains("basecolormap")) return "_a";
    if (low.Contains("normalmap") || (low.Contains("bumpmap") && !low.Contains("detailbump"))) return "_n";
    if (low.Contains("detailbumpmap")) return "_n";
    if (low.Contains("specularmap") || low.Contains("reflectancemap") || low.Contains("metallicmap")) return "_m";
    if (low.Contains("roughnessmap") || low.Contains("shininessmap")) return "_r";
    if (low.Contains("displacementmap") || low.Contains("heightmap")) return "_d";
    if (low.Contains("mask1map") || low.Contains("g_mask1") || low.Contains("blendmask")) return "_mask";
    if (low.Contains("emissivemap") || low.Contains("ambientocclusionmap")) return "_em";
    return null;
}

/// MTD-level classification from DSAnimStudio FlverMaterial.cs:239-271
(bool isAlphaEdge, bool isDoubleFace, bool isDecal, bool isCloth, bool isHair, bool isFur) ClassifyMtd(string mtdShortName)
{
    var low = mtdShortName.ToLower();
    bool isAlphaEdge = low.EndsWith("_alp") || low.Contains("_edge") || low.Contains("_e_") || low.EndsWith("_e")
        || low.Contains("_decal") || low.Contains("_cloth") || low.Contains("_al") || low.Contains("blendopacity")
        || low.Contains("fur") || low.Contains("hair");
    bool isDoubleFace = low.Contains("_df_") || low.Contains("veil");
    bool isDecal = low.Contains("_decal");
    bool isCloth = low.Contains("_cloth") || ClothKeywords.Any(kw => low.Contains(kw));
    bool isFur = low.Contains("fur");
    bool isHair = low.Contains("hair");
    return (isAlphaEdge, isDoubleFace, isDecal, isCloth, isHair, isFur);
}

/// Derive UE5 blend mode from MTD + material name + classification
string ResolveBlendMode(string? mtdBlendMode, string mtdShortName, string matName)
{
    var low = mtdShortName.ToLower();
    var matLow = matName.ToLower();

    // Priority 1: MTD explicit blend mode
    if (!string.IsNullOrEmpty(mtdBlendMode))
    {
        var bm = mtdBlendMode.Trim();
        if (bm.Equals("TexEdge", StringComparison.OrdinalIgnoreCase)) return "Masked";
        if (bm is "Blend" or "Add" or "Sub" or "Mul" or "Water" or "LSBlend" or "LSAdd") return "Translucent";
        // "Normal" — fall through to keyword rules
    }

    // Priority 2: Cloth keywords (material name or MTD name)
    if (ClothKeywords.Any(kw => matLow.Contains(kw)) || ClothKeywords.Any(kw => low.Contains(kw)))
        return "Masked";

    // Priority 3: _decal → AlphaEdge Masked (aligned with DSAnimStudio FlverMaterial.cs:239-254)
    // MTD names suffixed with _Decal use alpha-test (Masked), NOT alpha-blend (Translucent).
    // Actual translucent materials (water, glass, etc.) have g_BlendMode=Blend in MTD (Priority 1).
    if (low.Contains("_decal"))
        return "Masked";

    // Priority 4: AlphaEdge patterns
    if (low.EndsWith("_alp") || low.Contains("_edge") || low.Contains("_e_") || low.EndsWith("_e")
        || low.Contains("_al") || low.Contains("blendopacity"))
        return "Masked";

    // Priority 5: Hair/Fur
    if (low.Contains("hair") || low.Contains("fur"))
        return "Masked";

    // Default
    return "Opaque";
}

/// Derive TwoSided from MTD + material name.
/// Hair and fur cards are single-plane quads — must be TwoSided to avoid one-sided culling.
bool ResolveTwoSided(string mtdShortName, string matName)
{
    var low = mtdShortName.ToLower();
    var matLow = matName.ToLower();
    if (low.Contains("_cloth") || low.Contains("_decal") || low.Contains("_df_") || low.Contains("veil"))
        return true;
    if (low.Contains("fur") || low.Contains("hair"))
        return true;
    if (ClothKeywords.Any(kw => matLow.Contains(kw)) || ClothKeywords.Any(kw => low.Contains(kw)))
        return true;
    return false;
}

/// Extract meaningful keywords from material name for texture matching.
/// Derive a clean material name from MTD basename by stripping "P_{PartName}_" prefix.
/// e.g. "P_AM_M_9000_Kunai.mtd" + "AM_M_9000" → "Kunai"
string? DeriveNameFromMtd(string? mtdPath, string partName)
{
    if (string.IsNullOrEmpty(mtdPath)) return null;
    var derived = Path.GetFileNameWithoutExtension(mtdPath); // "P_AM_M_9000_Kunai"
    if (derived.StartsWith("P_")) derived = derived.Substring(2);
    var prefix = partName + "_";
    if (derived.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
        derived = derived.Substring(prefix.Length);
    return string.IsNullOrEmpty(derived) ? null : derived;
}

/// True if the material name from FLVER is generic (auto-generated index, not semantic).
bool IsGenericMatName(string? name)
{
    if (string.IsNullOrEmpty(name)) return true;
    if (name.StartsWith("Material #")) return true;
    if (name.StartsWith("#") && name.EndsWith("#")) return true;
    // Pure numeric or numeric with hyphens (e.g. "0", "10-Intensity")
    if (name.All(c => c >= '0' && c <= '9' || c == '-' || c == '#')) return true;
    return false;
}

/// Filters out numeric-only tokens and tokens that appear in the part name.
/// e.g. "FC_M_0100_Eye" → ["eye"], "BD_M_9000_fraryR_cloth" → ["fraryr", "cloth"]
List<string> GetMatKeywords(string? matShortName, string? partName = null)
{
    var kw = new List<string>();
    if (string.IsNullOrEmpty(matShortName)) return kw;
    var partLower = (partName ?? "").ToLower();
    foreach (var part in matShortName.Split('_', StringSplitOptions.RemoveEmptyEntries))
    {
        var low = part.ToLower();
        // Skip very short, purely numeric, or part-name-contained tokens
        if (low.Length < 3) continue;
        if (low.All(c => c >= '0' && c <= '9')) continue;
        if (low == "material") continue;
        if (partLower.Contains(low)) continue;
        kw.Add(low);
    }
    return kw;
}

/// Match best texture filename from AvailableTextures for a given Part+ParamName.
/// matShortName is the FLVER material name (without part prefix) — used for semantic scoring.
/// When relaxedScoring is true, cross-part matching is allowed (for dammy texture fallback).
/// globalTextures is the combined pool of ALL parts' TPF textures — searched as last resort for shared textures.
string? ResolveTextureForSlot(string paramName, string? existingPath, List<string> availableTextures, string partName, string? matShortName, Dictionary<string, string> mtdTextureSlots, bool relaxedScoring = false, List<string>? globalTextures = null)
{
    var searchPools = new List<List<string> > { availableTextures };
    if (globalTextures != null && globalTextures.Count > 0)
        searchPools.Add(globalTextures);

    // Validate existing FLVER path against available textures (with extension fallback)
    if (!string.IsNullOrEmpty(existingPath))
    {
        var fn = Path.GetFileName(existingPath);
        var stem = Path.GetFileNameWithoutExtension(existingPath);
        if (!string.IsNullOrEmpty(fn))
        {
            foreach (var pool in searchPools)
            {
                if (pool.Any(t => string.Equals(t, fn, StringComparison.OrdinalIgnoreCase)))
                    return fn;
                var altMatch = pool.FirstOrDefault(t =>
                    string.Equals(Path.GetFileNameWithoutExtension(t), stem, StringComparison.OrdinalIgnoreCase));
                if (altMatch != null) return altMatch;
            }
        }
        // FLVER path doesn't exist locally — fall through to MTD + suffix matching
    }

    var suffix = ParamNameToSuffix(paramName);
    if (suffix == null) return null;

    // Check MTD texture slots first (they have default paths).
    // MTD paths often reference .tif files, but extracted TPFs contain .dds/.png.
    // Try to find a matching texture with a different extension.
    if (mtdTextureSlots.TryGetValue(paramName, out var mtdPath) && !string.IsNullOrEmpty(mtdPath))
    {
        var mtdFn = Path.GetFileName(mtdPath);
        var mtdBase = Path.GetFileNameWithoutExtension(mtdPath);
        if (!string.IsNullOrEmpty(mtdFn))
        {
            foreach (var pool in searchPools)
            {
                if (pool.Any(t => string.Equals(t, mtdFn, StringComparison.OrdinalIgnoreCase)))
                    return mtdFn;
                var altMatch = pool.FirstOrDefault(t =>
                    string.Equals(Path.GetFileNameWithoutExtension(t), mtdBase, StringComparison.OrdinalIgnoreCase));
                if (altMatch != null) return altMatch;
            }
        }
    }

    var partLower = partName.ToLower();
    var matKeywords = GetMatKeywords(matShortName, partName);
    var candidateScores = new List<(string filename, int score)>();

    // Collect suffix variants for non-standard naming (e.g. eye_1m as metallic)
    var suffixVariants = new List<string> { suffix };
    if (suffix.Length == 2 && suffix[0] == '_')
    {
        // e.g. _m → also try _1m, _2m; _a → _1a; _n → _1n
        var c = suffix[1];
        suffixVariants.Add($"_{1}{c}");
        suffixVariants.Add($"_{2}{c}");
    }

    foreach (var texFn in availableTextures)
    {
        var stem = Path.GetFileNameWithoutExtension(texFn).ToLower();
        var matchedSuffix = suffixVariants.FirstOrDefault(s => stem.EndsWith(s));
        if (matchedSuffix == null) continue;

        int score = 0;
        if (stem.StartsWith(partLower + "_")) score += 50;
        // Also check base part name (WP_A_0300_1 → WP_A_0300)
        var basePartLower = partLower;
        int baseUnderscore = partLower.LastIndexOf('_');
        if (baseUnderscore > 0 && baseUnderscore < partLower.Length - 1 &&
            char.IsDigit(partLower[baseUnderscore + 1]))
            basePartLower = partLower.Substring(0, baseUnderscore);
        if (basePartLower != partLower && stem.StartsWith(basePartLower + "_")) score += 45;
        // Material-name semantic match: prefer textures whose stem contains mat keywords
        foreach (var kw in matKeywords)
        {
            if (stem.Contains(kw)) { score += 40; break; }
        }
        // Relaxed: prefer head/skin/face-related textures for cross-part matching
        if (relaxedScoring && score == 0)
        {
            if (stem.Contains("head")) score += 30;
            else if (stem.Contains("skin")) score += 25;
            else if (stem.Contains("face")) score += 20;
        }
        score += stem.Length switch { < 8 => -10, _ => 0 }; // Penalize too short names
        if (score > 0 || relaxedScoring) candidateScores.Add((texFn, score));
    }

    candidateScores.Sort((a, b) => {
        int cmp = b.score.CompareTo(a.score);
        if (cmp != 0) return cmp;
        var aStem = Path.GetFileNameWithoutExtension(a.filename);
        var bStem = Path.GetFileNameWithoutExtension(b.filename);
        return aStem.Length.CompareTo(bStem.Length);
    });
    return candidateScores.FirstOrDefault().filename;
}

/// Build resolved texture mapping for a material
/// When relaxedScoring is true, textures from any part are considered (for cross-part dammy fallback).
/// globalTextures is the combined pool of ALL parts' TPF textures — searched as last resort for shared textures.
Dictionary<string, string> BuildResolvedTextures(
    List<object> flverTextures,
    List<string> availableTextures,
    string partName,
    string? matShortName,
    Dictionary<string, string> mtdTextureSlots,
    bool relaxedScoring = false,
    List<string>? globalTextures = null)
{
    var result = new Dictionary<string, string>();
    var seenSemantic = new HashSet<string>();

    // For multi-part weapons (WP_A_0300_1), also try base part name (WP_A_0300)
    var basePartName = partName;
    int lastUnderscore = partName.LastIndexOf('_');
    if (lastUnderscore > 0 && lastUnderscore < partName.Length - 1 &&
        char.IsDigit(partName[lastUnderscore + 1]))
        basePartName = partName.Substring(0, lastUnderscore);

    foreach (dynamic tex in flverTextures)
    {
        string paramName = tex.ParamName ?? "";
        string path = tex.Path ?? "";
        string semantic = ParamNameToSuffix(paramName) ?? paramName;

        // Determine suffix from semantic
        var suffix = ParamNameToSuffix(paramName);
        if (suffix == null) continue;

        // Resolve texture (try partName first, then basePartName for multi-part weapons)
        var resolved = ResolveTextureForSlot(paramName, path, availableTextures, partName, matShortName, mtdTextureSlots, relaxedScoring, globalTextures);
        if (resolved == null && basePartName != partName)
            resolved = ResolveTextureForSlot(paramName, path, availableTextures, basePartName, matShortName, mtdTextureSlots, relaxedScoring, globalTextures);
        if (resolved != null)
        {
            // Use standard UE names for the key
            var ueKey = suffix switch
            {
                "_a" => "albedo",
                "_n" => "normal",
                "_m" => "metallic",
                "_r" => "roughness",
                "_mask" => "opacityMask",
                "_em" => "emissive",
                _ => suffix.TrimStart('_')
            };

            if (!seenSemantic.Contains(ueKey))
            {
                result[ueKey] = resolved;
                seenSemantic.Add(ueKey);
            }
        }
    }

    return result;
}

// ---- Build global texture pool from ALL parts (for cross-part texture sharing) ----
// HD_M parts have dammy textures in their own TPF; real face textures are in FC_M TPF.
var globalTextures = new List<string>();
foreach (var bp in bodyPaths)
{
    var dir = Path.GetDirectoryName(bp);
    if (dir == null) continue;
    var partName = Path.GetFileNameWithoutExtension(bp);
    var glCandidates = new List<string> {
        Path.Combine(dir, $"{partName}-tpf"),
        Path.Combine(dir, "..", $"{partName}-tpf"),
    };
    int glUnderscoreIdx = partName.LastIndexOf('_');
    if (glUnderscoreIdx > 0 && glUnderscoreIdx < partName.Length - 1 &&
        char.IsDigit(partName[glUnderscoreIdx + 1]))
    {
        var baseName = partName.Substring(0, glUnderscoreIdx);
        glCandidates.Add(Path.Combine(dir, $"{baseName}-tpf"));
        glCandidates.Add(Path.Combine(dir, "..", $"{baseName}-tpf"));
    }
    foreach (var candidate in glCandidates)
    {
        if (!Directory.Exists(candidate)) continue;
        foreach (var f in Directory.GetFiles(candidate, "*.*"))
        {
            var ext = Path.GetExtension(f).ToLower();
            if (ext is ".dds" or ".png")
                globalTextures.Add(Path.GetFileName(f));
        }
    }
}
Console.WriteLine($"Global texture pool: {globalTextures.Count} textures across all parts");

int partIndex = 0;
foreach (var (partName, bodyFlver) in allBodyFlvers)
{
    // Collect available TPF textures for this part
    var availableTextures = GetTpfTextures(bodyPaths[partIndex], partName);

    // Extract materials for this part
    var seenMatNames = new HashSet<string>();
    for (int mi = 0; mi < bodyFlver.Materials.Count; mi++)
    {
        var mat = bodyFlver.Materials[mi];

        // Capture ALL texture entries — Sekiro FLVER has empty Path but
        // ParamName holds the shader parameter name like
        // "Character_AMSN__DetailBlend__snp_Texture2D_7_AlbedoMap".
        var textureEntries = new List<object>();
        foreach (var tex in mat.Textures)
        {
            textureEntries.Add(new
            {
                ParamName = tex.ParamName ?? "",
                Path = tex.Path ?? "",
                TilingScale = new[] { tex.TilingScale.X, tex.TilingScale.Y },
                TilingTypeU = tex.TilingTypeU.ToString(),
                TilingTypeV = tex.TilingTypeV.ToString(),
            });
        }

        // Try to parse the referenced MTD file for real material parameters
        object? mtdInfo = null;
        var mtdRawBlend = "Unknown";
        var mtdSamplerDict = new Dictionary<string, string>(); // Type -> Path
        if (!string.IsNullOrEmpty(mat.MTD))
        {
            var localMtd = ResolveMtdPath(mat.MTD);
            if (localMtd != null)
            {
                try
                {
                    var mtd = SoulsFormats.MTD.Read(localMtd);
                    // BlendMode / LightingType are stored as params, not top-level properties
                    var blendParam = mtd.Params.FirstOrDefault(p => p.Name == "g_BlendMode");
                    var lightParam = mtd.Params.FirstOrDefault(p => p.Name == "g_LightingType");
                    if (blendParam?.Value != null) mtdRawBlend = blendParam.Value.ToString() ?? "Unknown";
                    mtdInfo = new
                    {
                        ShaderPath = mtd.ShaderPath,
                        BlendMode = mtdRawBlend,
                        LightingType = lightParam?.Value?.ToString() ?? "Unknown",
                        Params = mtd.Params.Select(p => new
                        {
                            p.Name,
                            Type = p.Type.ToString(),
                            Value = p.Value is Array arr
                                ? arr.Cast<object>().ToArray()
                                : p.Value,
                        }),
                        TextureSlots = mtd.Textures.Select(t => new
                        {
                            t.Type,
                            t.UVNumber,
                            t.Extended,
                            t.Path,
                        }),
                    };
                    // Capture MTD sampler configs for texture matching
                    foreach (var tex in mtd.Textures)
                        if (!string.IsNullOrEmpty(tex.Path))
                            mtdSamplerDict[tex.Type] = tex.Path;
                }
                catch (Exception mtdEx)
                {
                    Console.WriteLine($"  WARNING: MTD parse failed for {mat.MTD}: {mtdEx.Message}");
                }
            }
        }

        // ---- Build resolved blend/two-sided/texture data ----
        var mtdShortName = Path.GetFileNameWithoutExtension(mat.MTD ?? "");
        // Derive clean material name from MTD when FLVER name is generic
        var cleanMatName = IsGenericMatName(mat.Name)
            ? (DeriveNameFromMtd(mat.MTD, partName) ?? mat.Name)
            : mat.Name;
        var matFullName = $"{partName}_{cleanMatName}";
        // Deduplicate: append suffx if name already used in this part
        if (!seenMatNames.Add(matFullName))
        {
            int dedupIdx = 2;
            while (!seenMatNames.Add($"{matFullName}_{dedupIdx}"))
                dedupIdx++;
            matFullName = $"{matFullName}_{dedupIdx}";
        }
        var (isAlphaEdge, isDoubleFace, isDecal, isCloth, isHair, isFur) = ClassifyMtd(mtdShortName);
        var resolvedBlend = ResolveBlendMode(mtdRawBlend, mtdShortName, mat.Name);
        var resolvedTwoSided = ResolveTwoSided(mtdShortName, mat.Name);
        var resolvedTextures = BuildResolvedTextures(textureEntries, availableTextures, partName, mat.Name, mtdSamplerDict, globalTextures: globalTextures);

        // ---- Dammy texture fallback: re-resolve from global pool ----
        // HD_M parts (head mesh) only have dammy placeholder textures in their TPF;
        // real face textures live in FC_M (Face) TPF. When local resolution yields
        // only dammy textures, search across ALL parts' TPFs for non-dammy matches.
        if (resolvedTextures.Count > 0 && resolvedTextures.Values.All(v => v.Contains("dammy")))
        {
            var nonDammyGlobal = globalTextures
                .Where(t => !t.Contains("dammy", StringComparison.OrdinalIgnoreCase))
                .ToList();
            var fallbackResolved = BuildResolvedTextures(textureEntries, nonDammyGlobal, partName, mat.Name, mtdSamplerDict, relaxedScoring: true, globalTextures: null);
            if (fallbackResolved.Count > 0)
            {
                Console.WriteLine($"  [TexFallback] '{matFullName}': dammy textures replaced from global pool ({string.Join(", ", fallbackResolved.Values)})");
                resolvedTextures = fallbackResolved;
            }
        }

        materialsOut.Add(new
        {
            Name = matFullName,
            MTD = mat.MTD,
            Part = partName,
            Textures = textureEntries,
            MTDInfo = mtdInfo,
            AvailableTextures = availableTextures,
            ResolvedBlendMode = resolvedBlend,
            TwoSided = resolvedTwoSided,
            IsFur = isFur,
            IsHair = isHair,
            IsCloth = isCloth,
            IsDecal = isDecal,
            DrawStep = isAlphaEdge ? "AlphaEdge" : "Opaque",
        });

        // ---- Build resolved material entry ----

        resolvedMaterialsOut.Add(new
        {
            Name = matFullName,
            Part = partName,
            MTD = Path.GetFileName(mat.MTD ?? ""),
            RawBlendMode = mtdRawBlend,
            ResolvedBlendMode = resolvedBlend,
            TwoSided = resolvedTwoSided,
            IsCloth = isCloth,
            IsHair = isHair,
            IsFur = isFur,
            IsDecal = isDecal,
            DrawStep = isAlphaEdge ? "AlphaEdge" : "Opaque",
            Textures = resolvedTextures,
        });
    }

    // Extract meshes for this part
    foreach (var mesh in bodyFlver.Meshes)
    {
        var verts = new List<object>();
        foreach (var v in mesh.Vertices)
        {
            verts.Add(new
            {
                Pos = new[] { v.Position.X, v.Position.Y, v.Position.Z },
                Normal = new[] { v.Normal.X, v.Normal.Y, v.Normal.Z },
                BoneIndices = new[] { v.BoneIndices[0], v.BoneIndices[1], v.BoneIndices[2], v.BoneIndices[3] },
                BoneWeights = new[] { (float)Math.Round(v.BoneWeights[0], 6), (float)Math.Round(v.BoneWeights[1], 6), (float)Math.Round(v.BoneWeights[2], 6), (float)Math.Round(v.BoneWeights[3], 6) },
                UV = v.UVs.Count > 0 ? new[] { v.UVs[0].X, v.UVs[0].Y } : new[] { 0f, 0f }
            });
        }

        var triangles = new List<int[]>();
        foreach (var fs in mesh.FaceSets)
        {
            if (fs.TriangleStrip)
            {
                // Convert triangle strip to triangle list.
                // Standard strip vertex sequence [v0,v1,v2,v3,v4,...]:
                //   Triangle 0 (even): (v0, v1, v2)
                //   Triangle 1 (odd):  (v2, v1, v3)  -- flipped winding
                //   Triangle 2 (even): (v2, v3, v4)
                // 0xFFFF = strip restart index (resets accumulated vertex window).
                int stripStart = 0;
                for (int i = 0; i < fs.Indices.Count; i++)
                {
                    if (fs.Indices[i] == 0xFFFF)
                    {
                        stripStart = i + 1;
                        continue;
                    }
                    if (i - stripStart >= 2)
                    {
                        int a = fs.Indices[i - 2];
                        int b = fs.Indices[i - 1];
                        int c = fs.Indices[i];

                        // Skip degenerate triangles (duplicate vertices)
                        if (a != b && b != c && a != c)
                        {
                            // Even triangle index → keep strip order; odd → flip winding
                            if ((i - stripStart) % 2 == 0)
                                triangles.Add(new[] { a, b, c });
                            else
                                triangles.Add(new[] { b, a, c });
                        }
                    }
                }
            }
            else
            {
                // Standard triangle list
                for (int t = 0; t + 2 < fs.Indices.Count; t += 3)
                {
                    int a = fs.Indices[t];
                    int b = fs.Indices[t + 1];
                    int c = fs.Indices[t + 2];
                    if (a != b && b != c && a != c)
                        triangles.Add(new[] { a, b, c });
                }
            }
        }

        // Map palette indices to bone names
        var boneIdxToName = new Dictionary<int, string>();
        if (mesh.BoneIndices.Count > 0)
        {
            for (int i = 0; i < mesh.BoneIndices.Count; i++)
            {
                int nodeIdx = mesh.BoneIndices[i];
                if (nodeIdx >= 0 && nodeIdx < bodyFlver.Nodes.Count)
                    boneIdxToName[i] = bodyFlver.Nodes[nodeIdx].Name;
            }
        }
        else
        {
            for (int i = 0; i < bodyFlver.Nodes.Count; i++)
                boneIdxToName[i] = bodyFlver.Nodes[i].Name;
        }

        // Detect eye meshes that contain both eyes in one section.
        // Split by X-axis so each eye gets its own mesh section + material slot.
        bool bIsEyeMesh = false;
        if (mesh.MaterialIndex >= 0 && mesh.MaterialIndex < bodyFlver.Materials.Count)
        {
            var matCheck = bodyFlver.Materials[mesh.MaterialIndex];
            var matNameLower = (matCheck.Name ?? "").ToLower();
            var mtdLower = (matCheck.MTD ?? "").ToLower();
            if ((matNameLower.Contains("eye") || mtdLower.Contains("eye"))
                && !matNameLower.Contains("crystal") && !mtdLower.Contains("crystal"))
            {
                int leftCount = 0, rightCount = 0;
                foreach (var v in mesh.Vertices)
                {
                    if (v.Position.X < 0) leftCount++;
                    else rightCount++;
                }
                bIsEyeMesh = leftCount > mesh.Vertices.Count * 0.1
                          && rightCount > mesh.Vertices.Count * 0.1;
                if (bIsEyeMesh)
                    Console.WriteLine($"  [EyeSplit] '{partName}' MatIdx={mesh.MaterialIndex}: {leftCount}L / {rightCount}R vertices");
            }
        }

        if (bIsEyeMesh)
        {
            for (int sideIdx = 0; sideIdx < 2; sideIdx++)
            {
                bool bLeftSide = (sideIdx == 0);
                string sideSuffix = bLeftSide ? "_L" : "_R";

                var sideVerts = new List<object>();
                var oldToNew = new Dictionary<int, int>();

                for (int vi = 0; vi < mesh.Vertices.Count; vi++)
                {
                    var v = mesh.Vertices[vi];
                    bool onSide = bLeftSide ? v.Position.X < 0 : v.Position.X >= 0;
                    if (!onSide) continue;

                    oldToNew[vi] = sideVerts.Count;
                    sideVerts.Add(new
                    {
                        Pos = new[] { v.Position.X, v.Position.Y, v.Position.Z },
                        Normal = new[] { v.Normal.X, v.Normal.Y, v.Normal.Z },
                        BoneIndices = new[] { v.BoneIndices[0], v.BoneIndices[1], v.BoneIndices[2], v.BoneIndices[3] },
                        BoneWeights = new[] { (float)Math.Round(v.BoneWeights[0], 6), (float)Math.Round(v.BoneWeights[1], 6), (float)Math.Round(v.BoneWeights[2], 6), (float)Math.Round(v.BoneWeights[3], 6) },
                        UV = v.UVs.Count > 0 ? new[] { v.UVs[0].X, v.UVs[0].Y } : new[] { 0f, 0f }
                    });
                }

                var sideTris = new List<int[]>();
                foreach (var tri in triangles)
                {
                    if (oldToNew.ContainsKey(tri[0]) && oldToNew.ContainsKey(tri[1]) && oldToNew.ContainsKey(tri[2]))
                        sideTris.Add(new[] { oldToNew[tri[0]], oldToNew[tri[1]], oldToNew[tri[2]] });
                }

                if (sideVerts.Count > 0 && sideTris.Count > 0)
                {
                    meshesOut.Add(new
                    {
                        Part = partName + sideSuffix,
                        MaterialIndex = mesh.MaterialIndex + globalMatOffset,
                        Vertices = sideVerts,
                        Triangles = sideTris,
                        BoneIdxToName = boneIdxToName
                    });
                }
            }
        }
        else
        {
            meshesOut.Add(new
            {
                Part = partName,
                MaterialIndex = mesh.MaterialIndex + globalMatOffset,
                Vertices = verts,
                Triangles = triangles,
                BoneIdxToName = boneIdxToName
            });
        }
    }

    globalMatOffset += bodyFlver.Materials.Count;
    partIndex++;
}

// ---- Cleanup orphaned materials (referenced only by meshes with 0 triangles) ----
// Some meshes may produce no triangles after eye-split filtering or degenerate removal.
// Remove materials that are no longer referenced by any mesh.
{
    // Find which material indices are still used by meshes with > 0 triangles
    var usedMatIndices = new HashSet<int>();
    foreach (dynamic mesh in meshesOut)
    {
        var tris = (List<int[]>)mesh.Triangles;
        if (tris.Count > 0)
            usedMatIndices.Add((int)mesh.MaterialIndex);
    }

    // Build remap: old index → new index (or -1 if unused)
    var matRemap = new Dictionary<int, int>();
    var keptMaterials = new List<object>();
    var keptResolved = new List<object>();
    for (int i = 0; i < materialsOut.Count; i++)
    {
        if (usedMatIndices.Contains(i))
        {
            matRemap[i] = keptMaterials.Count;
            keptMaterials.Add(materialsOut[i]);
            if (i < resolvedMaterialsOut.Count)
                keptResolved.Add(resolvedMaterialsOut[i]);
        }
    }

    int removedCount = materialsOut.Count - keptMaterials.Count;
    if (removedCount > 0)
    {
        Console.WriteLine($"[Cleanup] Removing {removedCount} orphaned material(s) (not referenced by any mesh with triangles)");
        materialsOut = keptMaterials;
        resolvedMaterialsOut = keptResolved;

        // Update mesh MaterialIndex with remapped indices
        var updatedMeshes = new List<object>();
        foreach (dynamic mesh in meshesOut)
        {
            int oldIdx = mesh.MaterialIndex;
            if (matRemap.TryGetValue(oldIdx, out int newIdx))
            {
                updatedMeshes.Add(new
                {
                    Part = mesh.Part,
                    MaterialIndex = newIdx,
                    Vertices = mesh.Vertices,
                    Triangles = mesh.Triangles,
                    BoneIdxToName = mesh.BoneIdxToName
                });
            }
            // else: mesh references orphaned material → drop the mesh too
        }
        meshesOut = updatedMeshes;
    }
}

var output = new
{
    SkeletonName = "Sekiro",
    BoneCount = bonesOut.Count,
    Bones = bonesOut,
    MaterialCount = materialsOut.Count,
    Materials = materialsOut,
    ResolvedMaterialCount = resolvedMaterialsOut.Count,
    ResolvedMaterials = resolvedMaterialsOut,
    MeshCount = meshesOut.Count,
    Meshes = meshesOut,
};

File.WriteAllText(outputPath,
    JsonSerializer.Serialize(output, new JsonSerializerOptions { WriteIndented = true, MaxDepth = 128 }));

int totalVerts = 0, totalTris = 0;
foreach (dynamic m in meshesOut)
{
    totalVerts += ((List<object>)m.Vertices).Count;
    totalTris += ((List<int[]>)m.Triangles).Count;
}
Console.WriteLine($"Output written to: {outputPath}");
Console.WriteLine($"Bones: {bonesOut.Count}, Materials: {materialsOut.Count}, Meshes: {meshesOut.Count}");
Console.WriteLine($"Total vertices: {totalVerts}, Total triangles: {totalTris}");
// Resolved material stats
int rmOpaque = 0, rmMasked = 0, rmTranslucent = 0, rmTwoSided = 0, rmWithTex = 0;
foreach (dynamic rm in resolvedMaterialsOut)
{
    switch ((string)rm.ResolvedBlendMode)
    {
        case "Opaque": rmOpaque++; break;
        case "Masked": rmMasked++; break;
        case "Translucent": rmTranslucent++; break;
    }
    if ((bool)rm.TwoSided) rmTwoSided++;
    var texCount = ((Dictionary<string, string>)rm.Textures).Count;
    if (texCount > 0) rmWithTex++;
}
Console.WriteLine($"ResolvedMaterials: {resolvedMaterialsOut.Count} (Opaque={rmOpaque} Masked={rmMasked} Translucent={rmTranslucent} TwoSided={rmTwoSided} WithTextures={rmWithTex})");
