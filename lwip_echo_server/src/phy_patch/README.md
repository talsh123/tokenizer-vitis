# PHY patch (RTL8211E) — durable re-apply (#10)

The Nexys Video board uses a Realtek **RTL8211E** PHY. Two lwIP-port BSP files carry hand patches
the stock Xilinx BSP lacks:

- **`xaxiemacif_physpeed.c`** — `get_IEEE_phy_speed()` gains an RTL8211E branch (Realtek OUI
  `0x001c`) that reads the negotiated speed from the PHY-Specific Status Register (page 0, reg 0x1A).
- **`xadapter.c`** — `axieth_link_status()` does full PHY setup only on the first link-up, then
  hardcodes 100 Mbps on any later re-link (`static int first_link`), which links reliably here.

## The problem
These files live in the **generated BSP** (`platform/.../libsrc/lwip220/.../netif/`). Vitis
**overwrites them every time the BSP is regenerated or the `.xsa` is re-read**, silently dropping the
patches — after which the board gets **no Ethernet link**. (They are lwIP-port files: hardware-
*independent*, they only change with the lwIP version, not with the design.)

## The durable fix (this folder)
The patched files are kept here as canonical, version-controlled copies named **`*.c.golden`**. The
`.golden` extension is deliberate: it stops the Vitis IDE from auto-adding them to the build
(`UserConfig.cmake`'s `USER_COMPILE_SOURCES`) — if they were compiled as `.c`, the app would hit
**duplicate-symbol link errors** with the BSP's lwIP lib. They are *reference* files, copied into the
BSP by the script, never compiled directly.

After any BSP regen / `.xsa` re-read, restore them with **one command** — no manual re-editing:

```powershell
powershell -ExecutionPolicy Bypass -File apply_phy_patch.ps1
```

Then rebuild the BSP + application in Vitis. The script auto-locates the BSP `netif` dir, backs up
whatever Vitis generated to `*.stock_bak`, copies the patched files in, and verifies the `MY CODE`
marker.

## Maintenance
- **If you change the patches:** edit the BSP files, verify on-board, then copy them back here
  (overwrite the canonical copies) and commit — this folder is the source of truth.
- **If the lwIP version ever changes** (a new `.xsa` pulls a newer `lwipNNN`): re-capture both files
  from the new BSP, re-apply the two `/// MY CODE` blocks, and refresh the copies here.
