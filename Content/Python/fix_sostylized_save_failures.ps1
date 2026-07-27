# Re-copy assets that failed to save from original pack.
# Close ALL Unreal Editor windows first.
#
#   powershell -ExecutionPolicy Bypass -File "E:/Unreal/MarineDemolition/Content/Python/fix_sostylized_save_failures.ps1"

$Pack = "D:\UnrealPack\Stylize\SoStylized\Content\SoStylized"
$Dst  = "E:\Unreal\MarineDemolition\Content\Developers\TaitoP\SoStylized"

$Rels = @(
    "Demo\Pawn\Mannequin\Character\Materials\M_UE4Man_Body.uasset",
    "Demo\Pawn\Mannequin\Character\Materials\MaterialLayers\ML_GlossyBlack_Latex_UE4.uasset",
    "Demo\Pawn\Mannequin\Character\Materials\MaterialLayers\ML_Plastic_Shiny_Beige.uasset",
    "Demo\Pawn\Mannequin\Character\Materials\MaterialLayers\ML_Plastic_Shiny_Beige_LOGO.uasset",
    "Demo\Pawn\Mannequin\Character\Materials\MaterialLayers\ML_SoftMetal_UE4.uasset",
    "Demo\Pawn\Mannequin\Character\Materials\MI_UE4Man_ChestLogo.uasset",
    "Demo\Pawn\Mannequin\Character\Mesh\SK_Mannequin.uasset",
    "Demo\Pawn\Mannequin\Character\Mesh\SK_Mannequin_PhysicsAsset.uasset",
    "Demo\Pawn\Mannequin\Character\Mesh\UE4_Mannequin_Skeleton.uasset",
    "Environment\Sky\Textures\T_SkylightReflection_HDR.uasset",
    "Environment\Sky\Textures\T_WaterRipples.uasset",
    "Textures\Noise\T_ChromaNoise_Blurred.uasset",
    "Textures\Noise\T_NoisePuddles.uasset"
)

$procs = Get-Process -Name "UnrealEditor","UnrealEditor-Cmd" -ErrorAction SilentlyContinue
if ($procs) {
    Write-Error "Close Unreal Editor first (PIDs: $($procs.Id -join ', '))"
    exit 1
}

$fail = 0
foreach ($rel in $Rels) {
    $src = Join-Path $Pack $rel
    $out = Join-Path $Dst $rel
    if (-not (Test-Path -LiteralPath $src)) {
        Write-Warning "pack missing $rel"
        $fail++
        continue
    }
    $dir = Split-Path $out -Parent
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    Copy-Item -LiteralPath $src -Destination $out -Force
    if ((Get-Item $out).Length -lt 1000) {
        Write-Warning "BAD size $rel"
        $fail++
    } else {
        Write-Host "OK $rel $((Get-Item $out).Length)"
    }
}

if ($fail -gt 0) { exit 2 }
Write-Host "FIX_SOSTYLIZED_SAVE_FAILURES_OK"
