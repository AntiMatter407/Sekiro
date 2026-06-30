using SoulsFormats;
using LuaDecompilerCore;
using LuaDecompilerCore.IR;
using LuaDecompilerCore.LanguageDecompilers;
using LuaDecompilerCore.Utilities;

namespace LuaBndExtractor;

public class Program
{
    public static int Main(string[] args)
    {
        if (args.Length < 1) {
            Console.Error.WriteLine("Usage: LuaBndExtractor <luabnd.dcx> [output_dir]");
            return 1;
        }
        string inputPath = args[0];
        string outputDir = args.Length > 1 ? args[1] 
            : Path.Combine(Path.GetDirectoryName(inputPath) ?? ".", 
                Path.GetFileNameWithoutExtension(inputPath) + "_extracted");

        try
        {
            // Step 1: DCX decompress
            Console.Error.WriteLine($"[1/3] Decompressing DCX: {Path.GetFileName(inputPath)}");
            byte[] decompressed;
            if (DCX.Is(inputPath))
                decompressed = DCX.Decompress(inputPath);
            else
                decompressed = File.ReadAllBytes(inputPath);
            Console.Error.WriteLine($"  Decompressed: {decompressed.Length} bytes");

            // Step 2: BND4 unpack
            Console.Error.WriteLine($"[2/3] Unpacking BND4...");
            BND4 bnd;
            try {
                bnd = BND4.Read(decompressed);
            } catch {
                // Try writing to temp file and reading
                var tmp = Path.GetTempFileName();
                File.WriteAllBytes(tmp, decompressed);
                bnd = BND4.Read(tmp);
                File.Delete(tmp);
            }
            Console.Error.WriteLine($"  Files: {bnd.Files.Count}");
            Directory.CreateDirectory(outputDir);

            // Save all files
            var luaFiles = new List<string>();
            for (int i = 0; i < bnd.Files.Count; i++) {
                var file = bnd.Files[i];
                var rawName = string.IsNullOrEmpty(file.Name) ? $"unnamed_{i}.bin" : file.Name;
                // Sanitize: remove absolute path prefixes, keep only filename or relative part
                var name = rawName.Replace('\\', '/');
                // If it's an absolute path like N:\NTC\...\script\ai\out\bin\xxx.lua, extract just the filename
                if (name.Contains(":/") || name.Contains(":\\")) {
                    name = Path.GetFileName(name);
                }
                // If it has multiple directory levels, just use the filename
                if (name.Contains('/')) {
                    name = Path.GetFileName(name);
                }
                if (string.IsNullOrEmpty(name)) name = $"unnamed_{i}.bin";
                var outPath = Path.Combine(outputDir, name);
                // Ensure output dir exists
                Directory.CreateDirectory(outputDir);
                File.WriteAllBytes(outPath, file.Bytes);
                
                // Detect Lua bytecode
                if (file.Bytes.Length > 4 && file.Bytes[0] == 0x1B && file.Bytes[1] == 0x4C 
                    && file.Bytes[2] == 0x75 && file.Bytes[3] == 0x61) {
                    luaFiles.Add(outPath);
                    Console.Error.WriteLine($"  [{i}] {name} ({file.Bytes.Length} bytes) [LUA]");
                } else {
                    Console.Error.WriteLine($"  [{i}] {name} ({file.Bytes.Length} bytes)");
                }
            }

            // Step 3: Decompile Lua files
            if (luaFiles.Count > 0) {
                Console.Error.WriteLine($"\n[3/3] Decompiling {luaFiles.Count} Lua files...");
                var decompiler = new LuaDecompiler(new DecompilationOptions());
                foreach (var lf in luaFiles) {
                    Console.Error.WriteLine($"  {Path.GetFileName(lf)}");
                    try {
                        using var stream = File.OpenRead(lf);
                        var br = new LuaDecompilerCore.Utilities.BinaryReaderEx(false, stream);
                        var lua = new LuaFile(br);
                        DecompilationResult result;
                        string outLua = Path.ChangeExtension(lf, ".dec.lua");
                        System.Text.Encoding outEnc = System.Text.Encoding.UTF8;
                        var main = new Function(lua.MainFunction.FunctionId);
                        
                        if (lua.Version == LuaFile.LuaVersion.Lua51Hks) {
                            result = decompiler.DecompileLuaFunction(new HksDecompiler(), main, lua.MainFunction);
                        } else if (lua.Version == LuaFile.LuaVersion.Lua50) {
                            result = decompiler.DecompileLuaFunction(new Lua50Decompiler(), main, lua.MainFunction);
                            outEnc = System.Text.Encoding.GetEncoding("shift_jis");
                        } else if (lua.Version == LuaFile.LuaVersion.Lua53Smash) {
                            result = decompiler.DecompileLuaFunction(new Lua53Decompiler(), main, lua.MainFunction);
                        } else {
                            Console.Error.WriteLine($"    Skip: unsupported version {lua.Version}");
                            continue;
                        }
                        File.WriteAllText(outLua, result.DecompiledSource, outEnc);
                        Console.Error.WriteLine($"    → {outLua} ({result.DecompiledSource.Length} chars)");
                    } catch (Exception ex) {
                        Console.Error.WriteLine($"    Error: {ex.Message}");
                    }
                }
            }

            Console.Error.WriteLine($"\nDone! Output: {outputDir}");
            return 0;
        }
        catch (Exception ex) {
            Console.Error.WriteLine($"Error: {ex}");
            return 1;
        }
    }
}



