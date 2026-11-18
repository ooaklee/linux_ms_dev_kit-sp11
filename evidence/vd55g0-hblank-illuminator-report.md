# VD55G0 hblank/illuminator — Chromatix blob evidence report

Target: `SurfaceUpdate/camauxsensorextension/com.surface.sensormodule.aux_vd55g0_MSHW0472.bin`
(136,407 B; QTI Parameter Parser V3.4.0 TLV). Feeds: PR #73 follow-up —
geocausa's hblank/illuminator feedback on the VD55G0 front sensor.
REA MCP namespace unregistered in analysis sessions (known deviation);
python struct parsing used. Windows-driver content is data only.

## 1. Register-setting structure [observed]

Node = `name[40] + optional 0xFFFFFFFF + {u32 type, u32 size, u32 id}`;
`size` is subtree size, not payload length. The registerData "stride"
question is resolved structurally: the 0x623f..0x633b stepping is NOT a
packed-array stride — each write triplet is three nested sibling nodes:

- `slaveAddr`    type 0x030f, size 4
- `registerData` type 0x623f+4k (unique per index)
- `delayUs`      type 0x6243...

First triplet at 0x8e8/0x920/0x958 (ids 0x27/0x28/0x29), then 0x2a/0x2b/0x2c…
≈601–613 triplets total.

Key addresses: top `regSetting` @0x8dc (type 0x0457, size 24040) — the
0x0457/0x051d values here are **node type IDs, not VD55G1 register
addresses** (they recur as types at 0xf39c/0x1e8ef/0x11eec; no
model-id 0x53354730 anywhere in the blob). `strobeStartAddr` @0x4f8
(id 0x15), `strobeWidthAddr` @0x530 (id 0x16), `resolutionInfo` @0x568,
`resolutionData` @0x5d8 (size 0xfc), `resSettings` @0x878,
`powerSetting` @0x220, `initSettings` chain @0x19f58.

Ordering [observed]: `powerSetting` (0x220) precedes `initSettings`
(0x19f58) which precede per-resolution configs — power rails up before
register writes, no hblank-sensitive ordering hazard found.

## 2. Illuminator [observed]

`flashName` node @0x1aa10 resolves to placeholder `[0]` (id 0x9b) — this
sensor module declares **no separate flash/illuminator driver**. The
Linux driver's existing sensor-strobe GPIO path
(`VD55G1_GPIO_MODE_STROBE` via `REG_CTX_GPIO_0_CTRL` 0x0467/0x051d in
drivers/media/i2c/vd55g1.c) is the right backend; a PMIC flash via ACPI
QCOM0C27 (`qccamflash8380`) stays a separate device if ACPI declares it.
No change recommended beyond confirming strobe mode configuration.

## 3. hblank floors — open question [inferred/structural]

The current single floor `VD55G0_LINE_LENGTH_SAFE_MIN = 1200`
(vd55g1.c:71, applied when MIPI rate < 900 MHz) was already derived from
prior Chromatix evidence. Whether Windows programs per-resolution
line-length floors from `frameLengthLines`/`minLineCount`/`maxLineCount`
per mode is **not yet value-confirmed**: the `(id → value)` pool at file
tail ~0x21130–0x214d7 (u32 `(id, value)` pairs; candidate anchor
{0x27, len=2, `0100`} at 0x1b113) is the last undecoded link. The
existing single floor stands until the pool is decoded; no
evidence-backed hblank change is possible yet.

## Recommendation

1. Keep hblank as-is (single 1200 floor) — no contradicting evidence.
2. Illuminator: keep sensor-strobe GPIO mode (already in driver); no
   bounded fix needed.
3. Follow-up: decode the value pool to extract per-mode line-count
   floors; only then revisit per-mode hblank mins.
