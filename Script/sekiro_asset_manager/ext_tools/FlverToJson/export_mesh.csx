// Quick check: what does AssimpNet support?
var asm = System.Reflection.Assembly.LoadFrom("F:/ProjectAI/Sekiro/Tools/FLVER_Editor/AssimpNet.dll");
foreach (var t in asm.GetExportedTypes().Take(30))
    Console.WriteLine(t.FullName);
