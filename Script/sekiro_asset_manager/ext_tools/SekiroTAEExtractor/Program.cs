using System.Text.Json;
using System.Text.Json.Serialization;
using SoulsFormats;

#nullable enable

if (args.Length < 1)
{
    Console.WriteLine("Usage: SekiroTAEExtractor <tae_dir> [output.json] [--template path]");
    Console.WriteLine("  tae_dir       : Directory containing .tae files (e.g. Extracted/c0000-anibnd-dcx/chr/c0000/tae/)");
    Console.WriteLine("  output.json   : Output JSON file (default: Sekiro_TAE_Logic.json)");
    Console.WriteLine("  --template    : Path to TAE.Template.SDT.xml (default: auto-detect)");
    return;
}

string taeDir = args[0];
string outputPath = args.Length >= 2 && !args[1].StartsWith("--") ? args[1] : "Sekiro_TAE_Logic.json";
string templatePath = "TAE.Template.SDT.xml";

for (int i = 1; i < args.Length; i++)
{
    if (args[i] == "--template" && i + 1 < args.Length)
        templatePath = args[++i];
}

// Resolve template path
if (!File.Exists(templatePath))
{
    // Try relative to executable
    string exeDir = AppContext.BaseDirectory;
    string altPath = Path.Combine(exeDir, "TAE.Template.SDT.xml");
    if (File.Exists(altPath))
        templatePath = altPath;
    else
    {
        Console.WriteLine($"ERROR: Template not found at '{templatePath}' or '{altPath}'");
        return;
    }
}

Console.WriteLine($"Loading SDT template: {templatePath}");
TAE.Template template = TAE.Template.ReadXMLFile(templatePath);
Console.WriteLine($"Template loaded: {template.Count} banks, Game={template.Game}");

// Find all .tae files. anibnd 解包目录可能包含 chr/<id>/tae 子目录，因此递归扫描。
var taeFiles = Directory.GetFiles(taeDir, "*.tae", SearchOption.AllDirectories)
    .OrderBy(f => f)
    .ToList();

Console.WriteLine($"Found {taeFiles.Count} TAE files");

var output = new SekiroTaeOutput
{
    SourceDirectory = taeDir,
    TemplateFile = templatePath,
    TotalTaeFiles = taeFiles.Count,
    TAE_Files = new List<TaeFileEntry>()
};

int totalAnims = 0;
int totalEvents = 0;

foreach (string taePath in taeFiles)
{
    string fileName = Path.GetFileName(taePath);
    Console.Write($"  {fileName}... ");

    try
    {
        byte[] taeBytes = File.ReadAllBytes(taePath);
        TAE tae = TAE.Read(taeBytes);

        // Determine event bank - Sekiro SDT Character TAEs use bank 14
        if (tae.Format == TAE.TAEFormat.SDT)
        {
            // Most character TAEs use bank 14 (Characters_SDT)
            tae.EventBank = tae.EventBank == 0 ? 14 : tae.EventBank;
        }

        // Apply template with strict=false to avoid crashing on unmapped event types
        try
        {
            TAE.ValidateEventBank = false;
            tae.ApplyTemplate(template, strict: false);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"WARN: template apply partial: {ex.Message}");
        }

        var taeEntry = new TaeFileEntry
        {
            FileName = fileName,
            TAE_ID = tae.ID,
            AnimationCount = tae.Animations.Count,
            Animations = new List<AnimationEntry>()
        };

        foreach (var anim in tae.Animations)
        {
            int animID = (int)anim.ID;
            string referenceType = "Direct";
            int motionSourceAnimID = animID;
            int eventSourceAnimID = animID;

            switch (anim.MiniHeader)
            {
                case TAE.Animation.AnimMiniHeader.Standard standard when standard.ImportsHKX:
                    referenceType = "ImportHKX";
                    motionSourceAnimID = standard.ImportHKXSourceAnimID;
                    break;
                case TAE.Animation.AnimMiniHeader.ImportOtherAnim imported:
                    referenceType = "ImportOtherAnim";
                    motionSourceAnimID = imported.ImportFromAnimID;
                    eventSourceAnimID = imported.ImportFromAnimID;
                    break;
            }

            var animEntry = new AnimationEntry
            {
                AnimID = animID,
                AnimFileName = anim.AnimFileName ?? "",
                ReferenceType = referenceType,
                MotionSourceAnimID = motionSourceAnimID,
                EventSourceAnimID = eventSourceAnimID,
                Events = new List<EventEntry>()
            };

            foreach (var evt in anim.Events)
            {
                // Clamp EndTime for JSON serialization (float.MaxValue = infinite)
                float endTime = evt.EndTime;
                int endFrame = endTime >= 99.0f ? -1 : (int)Math.Round(endTime * 30.0f);  // -1 = lasts until anim end

                var evtEntry = new EventEntry
                {
                    Type = evt.Type,
                    TypeName = evt.TypeName ?? $"UnknownType_{evt.Type}",
                    StartTime = evt.StartTime,
                    EndTime = endTime >= 99.0f ? -1.0f : endTime,
                    StartFrame = (int)Math.Round(evt.StartTime * 30.0f),
                    EndFrame = endFrame
                };

                // Extract named parameters if template was applied
                if (evt.Parameters != null && evt.Parameters.Values.Count > 0)
                {
                    evtEntry.Parameters = new Dictionary<string, object?>();
                    foreach (var kvp in evt.Parameters.Values)
                    {
                        object? val = kvp.Value;
                        if (val == null)
                        {
                            evtEntry.Parameters[kvp.Key] = null;
                            continue;
                        }
                        // Handle enum entry types (they have Name properties)
                        if (val.GetType().Name == "EntryAsValue")
                        {
                            val = val.ToString();
                        }
                        // Sanitize float infinity/NaN
                        if (val is float f)
                        {
                            if (float.IsInfinity(f) || float.IsNaN(f))
                                val = f > 0 ? -1.0 : 0.0;
                        }
                        evtEntry.Parameters[kvp.Key] = val;
                    }
                }

                animEntry.Events.Add(evtEntry);
            }

            taeEntry.Animations.Add(animEntry);
            totalAnims++;
        }

        ResolveImportedAnimationEvents(taeEntry);
        totalEvents += taeEntry.Animations.Sum(anim => anim.Events.Count);

        output.TAE_Files.Add(taeEntry);
        Console.WriteLine($"{taeEntry.AnimationCount} anims, OK");
    }
    catch (Exception ex)
    {
        Console.WriteLine($"ERROR: {ex.Message}");
    }
}

Console.WriteLine($"\nTotal: {totalAnims} animations, {totalEvents} events across {taeFiles.Count} TAE files");

// Write output JSON
var jsonOptions = new JsonSerializerOptions
{
    WriteIndented = true,
    DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    MaxDepth = 64
};

string json = JsonSerializer.Serialize(output, jsonOptions);
File.WriteAllText(outputPath, json);

FileInfo fi = new FileInfo(outputPath);
Console.WriteLine($"Output written: {outputPath} ({fi.Length / 1024.0 / 1024.0:F1} MB)");

// ImportOtherAnim 同时继承来源动画的动作与事件。展开事件后，下游无需理解 TAE MiniHeader
// 也能获得完整事件轨；同时保留引用元数据，供动画曲线管线物化逻辑动画资产。
static void ResolveImportedAnimationEvents(TaeFileEntry taeEntry)
{
    var animationsByID = taeEntry.Animations.ToDictionary(anim => anim.AnimID);

    foreach (AnimationEntry anim in taeEntry.Animations)
    {
        if (anim.ReferenceType != "ImportOtherAnim")
            continue;

        var visited = new HashSet<int>();
        AnimationEntry? source = ResolveEventSource(anim, animationsByID, visited);
        if (source == null)
        {
            Console.WriteLine($"WARN: animation {anim.AnimID} event source could not be resolved");
            continue;
        }

        anim.EventSourceAnimID = source.AnimID;
        anim.Events = source.Events;
    }
}

static AnimationEntry? ResolveEventSource(
    AnimationEntry anim,
    IReadOnlyDictionary<int, AnimationEntry> animationsByID,
    HashSet<int> visited)
{
    if (!visited.Add(anim.AnimID))
        return null;

    if (anim.ReferenceType != "ImportOtherAnim" || anim.EventSourceAnimID == anim.AnimID)
        return anim;

    if (!animationsByID.TryGetValue(anim.EventSourceAnimID, out AnimationEntry? source))
        return null;

    return ResolveEventSource(source, animationsByID, visited);
}

// ============================================================================
// Output model classes
// ============================================================================

public class SekiroTaeOutput
{
    public string SourceDirectory { get; set; } = "";
    public string TemplateFile { get; set; } = "";
    public int TotalTaeFiles { get; set; }
    public List<TaeFileEntry> TAE_Files { get; set; } = new();
}

public class TaeFileEntry
{
    public string FileName { get; set; } = "";
    public int TAE_ID { get; set; }
    public int AnimationCount { get; set; }
    public List<AnimationEntry> Animations { get; set; } = new();
}

public class AnimationEntry
{
    public int AnimID { get; set; }
    public string AnimFileName { get; set; } = "";
    public string ReferenceType { get; set; } = "Direct";
    public int MotionSourceAnimID { get; set; }
    public int EventSourceAnimID { get; set; }
    public List<EventEntry> Events { get; set; } = new();
}

public class EventEntry
{
    public int Type { get; set; }
    public string TypeName { get; set; } = "";
    public float StartTime { get; set; }
    public float EndTime { get; set; }
    public int StartFrame { get; set; }
    public int EndFrame { get; set; }
    public Dictionary<string, object?>? Parameters { get; set; }
}
