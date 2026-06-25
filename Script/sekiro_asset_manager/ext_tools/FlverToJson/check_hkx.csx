#r "../../FLVER_Editor/SoulsFormats.dll"
var asm = System.Reflection.Assembly.LoadFrom("F:/ProjectAI/Sekiro/Tools/FLVER_Editor/SoulsFormats.dll");
foreach (var t in asm.GetTypes())
{
    if (t.Name.ToLower().Contains("hkx") || t.Name.ToLower().Contains("havok"))
        Console.WriteLine($"{t.FullName}");
}
