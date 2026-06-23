$mainDir = "F:\ProjectAI\Sekiro\Extracted\Textures"
$subDir = "F:\ProjectAI\Sekiro\Extracted\Textures\Textures"
$seen = @{}
$files = @()
$python = "F:/UnrealEngine-5.2/Engine/Binaries/ThirdParty/Python3/Win64/python.exe"
$bridge = "F:/ProjectAI/Sekiro/.codex/skills/aibridge/bridge.py"
$targetDir = "/Game/Sekiro/Character/c0000/Textures"
$env:MSYS2_ARG_CONV_EXCL = '*'

Get-ChildItem -Path $mainDir -Filter "*.png" | ForEach-Object {
    $name = $_.Name -replace '\.png$', ''
    if (-not $seen.ContainsKey($name)) {
        $seen[$name] = $_.FullName
        $files += [PSCustomObject]@{Name=$name; Path=$_.FullName}
    }
}
Get-ChildItem -Path $subDir -Filter "*.png" | ForEach-Object {
    $name = $_.Name -replace '\.png$', ''
    if (-not $seen.ContainsKey($name)) {
        $seen[$name] = $_.FullName
        $files += [PSCustomObject]@{Name=$name; Path=$_.FullName}
    }
}

$total = $files.Count
$i = 0
$failed = @()

foreach ($f in $files) {
    $i++
    $assetPath = "$targetDir/$($f.Name)"
    Write-Output "[$i/$total] Importing $($f.Name)..."
    $result = & $python $bridge asset import_file $assetPath $f.Path 2>&1
    if ($LASTEXITCODE -ne 0 -or $result -match '"ok":\s*false') {
        Write-Output "  FAILED: $result"
        $failed += $f.Name
    } else {
        Write-Output "  OK"
    }
}

Write-Output ""
Write-Output "Done. $($total - $failed.Count)/$total succeeded."
if ($failed.Count -gt 0) {
    Write-Output "Failed: $($failed -join ', ')"
}
