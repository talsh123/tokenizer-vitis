# apply_phy_patch.ps1  (#10 -- durable RTL8211E PHY patch)
# Re-applies the RTL8211E PHY patches to the lwIP BSP after a BSP regen / .xsa re-read wipes them.
# The patched files (xaxiemacif_physpeed.c, xadapter.c) are kept in this folder as the canonical,
# version-controlled copies; this script copies them back into the generated BSP and verifies them.
#
# Run from anywhere (no args):
#     powershell -ExecutionPolicy Bypass -File apply_phy_patch.ps1
# Then rebuild the BSP + application in Vitis.

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = (Resolve-Path (Join-Path $here '..\..\..')).Path   # workspace root (…\final_project_eth_nexys_video)
$platform = Join-Path $root 'platform'
$files = @('xaxiemacif_physpeed.c', 'xadapter.c')

if (-not (Test-Path $platform)) { Write-Error "No platform\ under $root -- generate the BSP first."; exit 1 }

# Find the BSP netif dir by content (robust to the domain name, e.g. standalone_microblaze_0).
$netif = Get-ChildItem -Path $platform -Recurse -Directory -Filter 'netif' -ErrorAction SilentlyContinue |
         Where-Object { Test-Path (Join-Path $_.FullName 'xaxiemacif_physpeed.c') } |
         Select-Object -First 1
if (-not $netif) { Write-Error "Could not find the lwIP BSP netif dir under $platform. Generate the BSP first."; exit 1 }
Write-Host "BSP netif: $($netif.FullName)"

foreach ($f in $files) {
    $src = Join-Path $here $f
    $dst = Join-Path $netif.FullName $f
    if (-not (Test-Path $src)) { Write-Error "Missing canonical copy: $src"; exit 1 }
    if (Test-Path $dst) { Copy-Item $dst "$dst.stock_bak" -Force }   # keep what Vitis just generated
    Copy-Item $src $dst -Force
    if (Select-String -Path $dst -Pattern 'MY CODE' -Quiet) {
        Write-Host "  patched: $f   (stock backed up to $f.stock_bak)"
    } else {
        Write-Error "  FAILED to verify the patch marker in $dst"; exit 1
    }
}
Write-Host "PHY patches re-applied. Now rebuild the BSP + application in Vitis."
