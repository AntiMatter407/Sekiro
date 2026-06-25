#r "../../FLVER_Editor/SoulsFormats.dll"
using System.Reflection;
var asm = Assembly.LoadFrom("F:/ProjectAI/Sekiro/Tools/FLVER_Editor/SoulsFormats.dll");
var types = asm.GetTypes().Where(t => t.Name.ToLower().Contains("flver"));
foreach (var t in types)
{
    Console.WriteLine($"\n=== {t.FullName} ===");
    foreach (var p in t.GetProperties(BindingFlags.Public | BindingFlags.Instance).Take(15))
        Console.WriteLine($"  Prop: {p.Name} : {p.PropertyType.Name}");
    foreach (var f in t.GetFields(BindingFlags.Public | BindingFlags.Instance).Take(15))
        Console.WriteLine($"  Field: {f.Name} : {f.FieldType.Name}");
}
