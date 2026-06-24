using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Text.Json;

class Program
{
    static void Main(string[] args)
    {
        string paramPath = args.Length >= 1 ? args[0] : null;
        string outDir = args.Length >= 2 ? args[1] : null;

        if (string.IsNullOrEmpty(paramPath))
        {
            // Default: find BehaviorParam_PC.param relative to script dir
            var scriptDir = AppDomain.CurrentDomain.BaseDirectory;
            var extracted = Path.GetFullPath(Path.Combine(scriptDir, "..", "..", "..", "..", "..", "Extracted", "gameparam", "gameparam-parambnd-dcx", "param", "GameParam"));
            paramPath = Path.Combine(extracted, "BehaviorParam_PC.param");
        }
        if (string.IsNullOrEmpty(outDir))
        {
            var scriptDir = AppDomain.CurrentDomain.BaseDirectory;
            outDir = Path.GetFullPath(Path.Combine(scriptDir, "..", "..", "..", "..", "..", "Output"));
        }

        if (!File.Exists(paramPath))
        {
            Console.WriteLine($"Error: param file not found: {paramPath}");
            return;
        }

        byte[] data = File.ReadAllBytes(paramPath);
        var param = SoulsFormats.PARAM.Read(data);

        var rowType = typeof(SoulsFormats.PARAM.Row);
        var dataOffsetField = rowType.GetField("DataOffset", BindingFlags.NonPublic | BindingFlags.Instance);
        if (dataOffsetField == null)
        {
            Console.WriteLine("Error: cannot find DataOffset field");
            return;
        }

        var results = new List<Dictionary<string, object>>();

        foreach (var row in param.Rows)
        {
            long offset = (long)dataOffsetField.GetValue(row)!;
            if (offset <= 0 || offset + 30 > data.Length) continue;

            int varId = BitConverter.ToInt32(data, (int)offset);
            int judgeId = BitConverter.ToInt32(data, (int)offset + 4);
            byte ezState = data[offset + 8];
            byte refType = data[offset + 9];
            int refId = BitConverter.ToInt32(data, (int)offset + 12);
            int sfxId = BitConverter.ToInt32(data, (int)offset + 16);
            int stamina = BitConverter.ToInt32(data, (int)offset + 20);
            int mp = BitConverter.ToInt32(data, (int)offset + 24);
            byte cat = data[offset + 28];
            byte hp = data[offset + 29];

            results.Add(new Dictionary<string, object>
            {
                ["variation_id"] = varId,
                ["row_id"] = row.ID,
                ["judge_id"] = judgeId,
                ["ez_state_behavior_type"] = (int)ezState,
                ["ref_type"] = (int)refType,
                ["ref_type_name"] = refType switch { 0 => "Attack", 1 => "Bullet", 2 => "SpEffect", _ => "Unknown" },
                ["ref_id"] = refId,
                ["sfx_id"] = sfxId,
                ["stamina"] = stamina,
                ["mp"] = mp,
                ["category"] = (int)cat,
                ["hero_point"] = (int)hp
            });
        }

        var json = JsonSerializer.Serialize(results, new JsonSerializerOptions { WriteIndented = true });
        Directory.CreateDirectory(outDir);
        string outPath = Path.Combine(outDir, "BehaviorParam_PC.json");
        File.WriteAllText(outPath, json);

        Console.WriteLine($"Done: {results.Count} entries -> {outPath}");

        int atk = 0, bul = 0, spe = 0, unk = 0;
        foreach (var r in results)
        {
            switch ((int)r["ref_type"])
            {
                case 0: atk++; break;
                case 1: bul++; break;
                case 2: spe++; break;
                default: unk++; break;
            }
        }
        Console.WriteLine($"Attack={atk}, Bullet={bul}, SpEffect={spe}, Unknown={unk}");
    }
}
