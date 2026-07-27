# Restore SoStylized from original pack at D:\UnrealPack\Stylize\SoStylized
# Close ALL Unreal Editor windows first (MarineDemolition + SoStylized pack project).
#
#   powershell -ExecutionPolicy Bypass -File "E:/Unreal/MarineDemolition/Content/Python/restore_sostylized_from_pack.ps1"

$PackRoot = "D:\UnrealPack\Stylize\SoStylized\Content\SoStylized"
$DstCanonical = "E:\Unreal\MarineDemolition\Content\SoStylized"
$DstDev = "E:\Unreal\MarineDemolition\Content\Developers\TaitoP\SoStylized"

if (-not (Test-Path -LiteralPath $PackRoot)) {
    Write-Error "Pack not found: $PackRoot"
    exit 1
}

foreach ($dst in @($DstCanonical, $DstDev)) {
    New-Item -ItemType Directory -Force -Path $dst | Out-Null
    Write-Host "COPY $PackRoot -> $dst ..."
    robocopy $PackRoot $dst /E /IS /IT /R:1 /W:1 /NFL /NDL /NJH /NJS
    Write-Host "robocopy exit=$LASTEXITCODE"
}

$checks = @(
    "$DstCanonical\Environment\Sky\Meshes\SM_StylizedSkyDome.uasset",
    "$DstCanonical\Environment\Sky\Meshes\SM_StylizedSkyDome_Clouds.uasset",
    "$DstCanonical\Environment\Sky\Materials\M_StylizedSky.uasset",
    "$DstCanonical\Environment\Sky\Materials\Presets\MI_StylizedSky_Classic.uasset",
    "$DstCanonical\Environment\Sky\Materials\Presets\MI_StylizedClouds_Classic.uasset",
    "$DstCanonical\Environment\MPC_GlobalEnvironment.uasset",
    "$DstCanonical\Environment\Sky\BP_StylizedSky.uasset"
)
$ok = $true
foreach ($f in $checks) {
    if ((Test-Path -LiteralPath $f) -and (Get-Item -LiteralPath $f).Length -gt 12000) {
        Write-Host "OK $(Split-Path $f -Leaf) $((Get-Item $f).Length) bytes"
    } else {
        $size = if (Test-Path $f) { (Get-Item $f).Length } else { "MISSING" }
        Write-Warning "BAD/MISSING $f size=$size"
        $ok = $false
    }
}

if (-not $ok) {
    Write-Warning "Some files failed — close SoStylized pack editor on D: and re-run."
    exit 2
}

Write-Host "PACK_COPY_OK — run restore_sostylized_from_pack.py in MarineDemolition editor"
