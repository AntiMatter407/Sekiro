#r "SoulsFormats.dll"
using SoulsFormats;
var flver = FLVER2.Read(@"F:\ProjectAI\Sekiro\Extracted\fc_m_0100-partsbnd\parts\Face\FC_M_0100\FC_M_0100.flver");
Console.WriteLine($"Meshes: {flver.Meshes.Count}, Materials: {flver.Materials.Count}");
Console.WriteLine("\nMesh -> Material mapping:");
for (int i = 0; i < flver.Meshes.Count; i++)
{
    var m = flver.Meshes[i];
    int mi = m.MaterialIndex;
    string matName = mi >= 0 && mi < flver.Materials.Count ? flver.Materials[mi].Name : "???";
    int triCount = 0;
    foreach (var fs in m.FaceSets)
        triCount += fs.Indices.Count / 3;
    Console.WriteLine($"  Mesh[{i}]: MatIdx={mi} -> \"{matName}\", Tris={triCount}, BoneIndices={m.BoneIndices.Count}");
}
Console.WriteLine("\nAll materials:");
for (int i = 0; i < flver.Materials.Count; i++)
{
    var mat = flver.Materials[i];
    Console.WriteLine($"  Mat[{i}]: Name=\"{mat.Name}\", MTD=\"{mat.MTD}\"");
}
