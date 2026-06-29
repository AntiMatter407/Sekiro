using System.Text.Json;
using Havoc.IO.Tagfile.Binary;
using Havoc.Objects;
using Havoc.Reflection;

namespace HkbStateGraphExtractor;

public class Program
{
    public class StateEntry
    {
        public int state_id { get; set; }
        public string state_name { get; set; } = "";
        public List<ClipEntry> clips { get; set; } = new();
        public List<TransitionEntry>? transitions { get; set; }
    }

    public class ClipEntry
    {
        public string? generator_type { get; set; }
        public string? generator_name { get; set; }
        public string? animation_name { get; set; }
        public string? anim_id { get; set; }
    }

    public class TransitionEntry
    {
        public int? event_id { get; set; }
        public string? event_name { get; set; }
        public int? to_state_id { get; set; }
        public string? to_state_name { get; set; }
    }

    public class GraphOutput
    {
        public string source { get; set; } = "";
        public string root_type { get; set; } = "";
        public int event_name_count { get; set; }
        public int state_count { get; set; }
        public List<StateEntry> states { get; set; } = new();
    }

    static string? S(IHkObject? o) { while (o is HkPtr p) o = p.Value; return (o as HkString)?.Value; }
    static int? I(IHkObject? o) { while (o is HkPtr p) o = p.Value; return (o as HkInt32)?.Value; }
    static IHkObject? D(IHkObject? o) { while (o is HkPtr p) o = p.Value; return o; }
    static HkClass? C(IHkObject? o) => D(o) as HkClass;
    static IHkObject? G(IReadOnlyDictionary<HkField, IHkObject> d, string n) { foreach (var kv in d) if (kv.Key.Name == n) return kv.Value; return null; }
    static IReadOnlyList<IHkObject>? A(IHkObject? o) => (D(o) as HkArray)?.Value;

    public static int Main(string[] args)
    {
        if (args.Length < 1) { Console.Error.WriteLine("Usage: HkbStateGraphExtractor <tagfile> [compendium] [output.json]"); return 1; }
        string tagFile = args[0];
        string? comp = args.Length > 1 ? args[1] : null;
        string outPath = args.Length > 2 ? args[2] : Path.ChangeExtension(tagFile, ".graph.json");

        try
        {
            Console.Error.WriteLine($"Reading: {Path.GetFileName(tagFile)}");
            IHkObject root;
            if (comp != null && File.Exists(comp))
            {
                var tagBytes = File.ReadAllBytes(tagFile);
                var compBytes = File.ReadAllBytes(comp);
                root = HkBinaryTagfileReader.Read(tagBytes, compBytes);
            }
            else
            {
                root = HkBinaryTagfileReader.Read(tagFile);
            }

            var result = ExtractGraph(root, tagFile);
            result.state_count = result.states.Count;

            var json = JsonSerializer.Serialize(result, new JsonSerializerOptions 
            { 
                WriteIndented = true, 
                Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping 
            });
            File.WriteAllText(outPath, json);
            
            Console.Error.WriteLine($"Output: {outPath}");
            Console.Error.WriteLine($"States: {result.states.Count}, Events: {result.event_name_count}");
            
            foreach (var s in result.states.Take(15))
            {
                var clips = string.Join(", ", s.clips.Select(c => c.animation_name ?? c.generator_name ?? "?"));
                var trans = s.transitions != null ? $" ({s.transitions.Count} transitions)" : "";
                Console.Error.WriteLine($"  [{s.state_id}] {s.state_name} -> [{clips}]{trans}");
            }
            if (result.states.Count > 15) 
                Console.Error.WriteLine($"  ... and {result.states.Count - 15} more");
            
            return 0;
        }
        catch (Exception ex) { Console.Error.WriteLine($"Error: {ex}"); return 1; }
    }

    static GraphOutput ExtractGraph(IHkObject root, string source)
    {
        var output = new GraphOutput { source = source };
        var rc = C(root); if (rc == null) return output;
        output.root_type = root.Type?.Name ?? "unknown";

        var nv = A(G(rc.Value, "namedVariants"));
        if (nv == null) return output;

        IHkObject? bg = null;
        foreach (var v in nv)
        {
            var vc = C(v); if (vc == null) continue;
            if (S(G(vc.Value, "className")) == "hkbBehaviorGraph")
            { bg = D(G(vc.Value, "variant")); break; }
        }
        if (bg == null) return output;

        var bgc = C(bg); if (bgc == null) return output;
        
        var data = C(D(G(bgc.Value, "data")));
        var eventNames = new Dictionary<int, string>();
        if (data != null)
        {
            var stringData = C(D(G(data.Value, "stringData")));
            if (stringData != null)
            {
                var eventNamesArr = A(G(stringData.Value, "eventNames"));
                if (eventNamesArr != null)
                {
                    output.event_name_count = eventNamesArr.Count;
                    for (int i = 0; i < eventNamesArr.Count; i++)
                    {
                        var s = S(eventNamesArr[i]);
                        if (s != null) eventNames[i] = s;
                    }
                }
            }
        }

        var rootGen = D(G(bgc.Value, "rootGenerator"));
        if (rootGen != null)
            WalkStates(rootGen, eventNames, output.states, new HashSet<object>());

        return output;
    }

    static void WalkStates(IHkObject gen, Dictionary<int, string> eventNames, 
        List<StateEntry> states, HashSet<object> visited)
    {
        var gc = C(gen); if (gc == null) return;
        var type = gen.Type?.Name ?? "?";
        var gf = gc.Value;

        if (type == "hkbStateMachine")
        {
            var dbgSt = A(G(gf, "states"));
            Console.Error.WriteLine($"  [WalkStates] hkbStateMachine: {dbgSt?.Count ?? 0} states");
            if (dbgSt != null) {
                foreach (var ds in dbgSt) {
                    var dsc = C(ds);
                    if (dsc != null) {
                        var dsf = dsc.Value;
                        var dsid = I(G(dsf, "stateId")) ?? -1;
                        var dsname = S(G(dsf, "name")) ?? "?";
                        var dsgen = D(G(dsf, "generator"));
                        Console.Error.WriteLine($"    state id={dsid} name=\"{dsname}\" gen={dsgen?.Type?.Name ?? "NULL"}");
                    }
                }
            }
        }
        if (type == "hkbStateMachine")
        {
            if (!visited.Add(gc.Value)) return;
            
            var st = A(G(gf, "states"));
            if (st != null)
            {
                foreach (var s in st)
                {
                    var sc = C(s); if (sc == null) continue;
                    var sf = sc.Value;
                    var entry = new StateEntry
                    {
                        state_id = I(G(sf, "stateId")) ?? -1,
                        state_name = S(G(sf, "name")) ?? $"State_{I(G(sf, "stateId")) ?? -1}",
                    };

                    var sgen = D(G(sf, "generator"));
                    if (sgen != null)
                        WalkClips(sgen, entry.clips, states, eventNames, new HashSet<object>());

                    var tr = C(D(G(sf, "transitions")));
                    if (tr != null)
                    {
                        var tl = A(G(tr.Value, "transitions"));
                        if (tl != null)
                        {
                            entry.transitions = new List<TransitionEntry>();
                            foreach (var t in tl)
                            {
                                var tc = C(t); if (tc == null) continue;
                                var tf = tc.Value;
                                var ev = C(D(G(tf, "event")));
                                int? eid = null; string? ename = null;
                                if (ev != null) 
                                { 
                                    eid = I(G(ev.Value, "id")); 
                                    if (eid.HasValue && eventNames.TryGetValue(eid.Value, out var n)) ename = n; 
                                }
                                entry.transitions.Add(new TransitionEntry 
                                { 
                                    event_id = eid, event_name = ename, 
                                    to_state_id = I(G(tf, "toStateId")) 
                                });
                            }
                        }
                    }
                    states.Add(entry);
                }
            }
        }
        
        if (type == "hkbScriptGenerator" || type == "hkbModifierGenerator")
        {
            var fieldName = type == "hkbScriptGenerator" ? "child" : "generator";
            var child = D(G(gf, fieldName));
            if (child != null) WalkStates(child, eventNames, states, visited);
        }
        else if (type == "hkbLayerGenerator")
        {
            var layers = A(G(gf, "layers"));
            if (layers != null)
            {
                for (int li = 0; li < layers.Count; li++)
                {
                    var l = layers[li];
                    var lc = C(l); 
                    if (lc == null) continue;
                    var lgen = D(G(lc.Value, "generator"));
                    if (lgen != null) WalkStates(lgen, eventNames, states, visited);
                }
            }
        }
        else if (type == "hkbManualSelectorGenerator" || type == "CustomManualSelectorGenerator")
        {
            var gens = A(G(gf, "generators"));
            if (gens != null)
                foreach (var g in gens) { var dg = D(g); if (dg != null) WalkStates(dg, eventNames, states, visited); }
        }
        else if (type == "hkbBlenderGenerator")
        {
            var ch = A(G(gf, "children"));
            if (ch != null)
                foreach (var c in ch) { var dc = D(c); if (dc != null) WalkStates(dc, eventNames, states, visited); }
        }
    }

    static void WalkClips(IHkObject gen, List<ClipEntry> clips, List<StateEntry> states,
        Dictionary<int, string> eventNames, HashSet<object> visited)
    {
        var gc = C(gen); if (gc == null) return;
        if (!visited.Add(gc.Value)) return;
        var type = gen.Type?.Name ?? "?";
        var gf = gc.Value;

        if (type == "hkbClipGenerator")
        {
            var an = S(G(gf, "animationName"));
            var cn = S(G(gf, "name"));
            clips.Add(new ClipEntry { generator_type = "hkbClipGenerator", generator_name = cn, animation_name = an, anim_id = an?.Replace(".hkx", "") });
        }
        else if (type == "hkbStateMachine")
        {
            var dbgSt = A(G(gf, "states"));
            Console.Error.WriteLine($"  [WalkStates] hkbStateMachine: {dbgSt?.Count ?? 0} states");
            if (dbgSt != null) {
                foreach (var ds in dbgSt) {
                    var dsc = C(ds);
                    if (dsc != null) {
                        var dsf = dsc.Value;
                        var dsid = I(G(dsf, "stateId")) ?? -1;
                        var dsname = S(G(dsf, "name")) ?? "?";
                        var dsgen = D(G(dsf, "generator"));
                        Console.Error.WriteLine($"    state id={dsid} name=\"{dsname}\" gen={dsgen?.Type?.Name ?? "NULL"}");
                    }
                }
            }
        }
        if (type == "hkbStateMachine")
        {
            WalkStates(gen, eventNames, states, new HashSet<object>());
        }
        else if (type == "hkbScriptGenerator" || type == "hkbModifierGenerator")
        {
            var fieldName = type == "hkbScriptGenerator" ? "child" : "generator";
            var child = D(G(gf, fieldName));
            if (child != null) WalkClips(child, clips, states, eventNames, visited);
        }
        else if (type == "hkbLayerGenerator")
        {
            var layers = A(G(gf, "layers"));
            if (layers != null)
            {
                foreach (var l in layers)
                {
                    var lc = C(l); if (lc == null) continue;
                    var lgen = D(G(lc.Value, "generator"));
                    if (lgen != null) WalkClips(lgen, clips, states, eventNames, visited);
                }
            }
        }
        else if (type == "hkbManualSelectorGenerator" || type == "CustomManualSelectorGenerator")
        {
            if (type == "CustomManualSelectorGenerator")
            {
                var aid = I(G(gf, "animId"));
                var cn = S(G(gf, "name"));
                if (aid.HasValue)
                    clips.Add(new ClipEntry { generator_type = "CustomManualSelectorGenerator", generator_name = cn, anim_id = aid.Value.ToString() });
            }
            var gens = A(G(gf, "generators"));
            if (gens != null)
                foreach (var g in gens) { var dg = D(g); if (dg != null) WalkClips(dg, clips, states, eventNames, visited); }
        }
        else if (type == "hkbBlenderGenerator")
        {
            var ch = A(G(gf, "children"));
            if (ch != null)
                foreach (var c in ch) { var dc = D(c); if (dc != null) WalkClips(dc, clips, states, eventNames, visited); }
        }
    }
}
