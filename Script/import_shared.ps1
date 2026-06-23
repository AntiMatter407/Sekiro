$texDir = "F:\ProjectAI\Sekiro\Extracted\Textures"
$python = "F:/UnrealEngine-5.2/Engine/Binaries/ThirdParty/Python3/Win64/python.exe"
$bridge = "F:/ProjectAI/Sekiro/.codex/skills/aibridge/bridge.py"
$targetDir = "/Game/Sekiro/Character/c0000/Textures"
$env:MSYS2_ARG_CONV_EXCL = '*'

$files = Get-ChildItem $texDir -Filter "FC_A_0000_*.png" | ForEach-Object { $_.Name -replace '\.png$', '' }
$total = $files.Count
$i = 0
foreach ($name in $files) {
    $i++
    $assetPath = "$targetDir/$name"
    Write-Output "[$i/$total] $name"
    & $python $bridge asset import_file $assetPath "$texDir\$name.png" 2>&1 | Out-Null
}
Write-Output "Done: $total textures"
