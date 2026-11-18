# Windows ARM64 audio driver analysis — Surface Pro 11 (X1E80100 / Snapdragon X Elite)

Source package: `SurfacePro11_ARM_Win11_26100_26.041.12746.0/SurfaceUpdate/` (26.041.12746.0)
Purpose: evidence for Linux qcaudio/SoundWire bring-up; context for OE issue #66
("current noise after firmware+audio install").

> **Method note (tooling limitation):** the REA MCP reverse-engineering tools were
> **not available** in this session (no `rea` namespace registered with the MCP
> broker; verified via `search` and `list_mcp_resources`). Analysis was therefore
> performed with PE inspection, offset-annotated `strings`, UTF-16LE string
> extraction with a Python script, and INF/XML/JSON config parsing. No disassembly
> was possible, so all findings are from static strings/data blobs and driver
> configuration, not from decoded instructions. Confidence marks reflect this.

---

## 1. Drivers analyzed

| Driver | File(s) | Ver | Notes |
|---|---|---|---|
| Qualcomm audio WaveRT miniport | `qcaudminiportbase/qcaudminiport8380.sys` (703,200 B, PE32+ AArch64 native) | 1.0.4281.8500 (2025-04-08) | PDB path `Z:\b\WP\AudioMiniport\rel\10.9\ARM64\Release\qcaudminiport8380.pdb` @ file off `0x1cf18`. Matches `AUCD\QCOM0C29` (Snapdragon X Elite audio device) |
| Audio miniport extension (Surface) | `audiominiext/surface_audiominiext8380.inf` (+.cat) | 8100.2025.6.11 | Per-REV HW config for MSHW0462/0486/0542/0566 |
| Surface APO extension | `apoextension/SurfaceAPO_0A/0B/0C/0D.json` | "Colchuck A/B/…" (codename) | Surface-custom MFX EQ coefficients |
| Qualcomm ASD/SoundWire APO | `qcasdapo/AcxProxyAPO.dll`, `qcasd_apo8380.inf` | 1.0.4281.8500 | AudioProcessingObject; `SWC\VEN_QCOM&DEV_ASDAPO_8380` |
| QC miniport APO | `qcaudminiportapo/ProxyAPO.dll`, `qcaudminiport_apo8380.inf` | — | APO proxy |
| IHV audio filter | `ihvaudiofilter/SurfaceIhvAudioFilterDriver.sys` | — | Upper-filter on `AUCD\VEN_QCOM&DEV_0C29` (DMF-based); no audio strings found — likely registry/policy filter |
| Dolby DAX3 (APO svc, SDW variant) | `dax3swcaposvcsdw/` (DolbyAPOv2*.dll etc.), `dax3extqc/` XMLs | — | `dax3_swc_aposvc_sdw_arm64.inf` — the "sdw" suffix matches SoundWire hardware revs |
| MS voice clarity APO | `mepaudiocomponent/voiceclarityapo.dll` | 2.0.13 (2024-10-02) | MS Voice Clarity, extension INF on EP_AUD |
| Resource constraints | `qcaudminiportbase/AudioResourceConstraints_8380.xml` | — | Endpoint exclusivity + resource consumption model |

---

## 2. SoundWire configuration

**Finding: the Windows driver stack does *not* contain SoundWire port/row/col
tables.** Confidence: **high** (exhaustive UTF-16 + ASCII string scan).

- `qcaudminiport8380.sys` contains **no** `swr`, `soundwire`, `wcd`, `slave`,
  `row/col`, frame-shape strings (strings dump of all 1710+ ASCII and all
  UTF-16LE strings ≥6 chars scanned).
- This is expected: on Windows the SoundWire master/codec bring-up lives in the
  **Qualcomm audio DSP firmware (ADSP)** and in the `qcasd` (Qualcomm Audio
  SoundWire) lower stack / ASIC-specific drivers not shipped in this package.
  The miniport talks to the DSP via GLink/AudioReach (`qcadsprpcd/qcadsprpcd8380.sys`,
  `qcglink/qcglink8380.sys` exist in the package). The host driver is a WaveRT
  topology driver only.
- Endpoint naming evidence for the SoundWire codec chain:
  - INF devnode: `AUCD\QCOM0C29` (`qcaudminiport_Base8380.inf` line 33) — 0C29 is
    the X1E audio controller function.
  - `qcasd\StaticEndpointDevice`, `qcasd\DynamicEnumExternalDisplay` HWIDs appear
    in `AudioResourceConstraints_8380.xml` (dynamic endpoint enumeration).
  - Dolby package variant is named `...sdw` (SoundWire) confirming codec attach.
- `UseInternalCodec` reg key string @ `0xa1910` in the .sys (UTF-16) implies the
  miniport can select internal codec vs. external path — evidence the codec
  config is registry/ACPI-driven, not embedded in the binary.

Implication for Linux: SoundWire port config must come from ACPI DSD/`sdw-slave`
properties + `qcaudio` driver tables, as done upstream for sc8280xp/x1e80100;
nothing to transplant from this Windows binary. Confidence: medium-high.

## 3. Sample rate / format tables

From `surface_audiominiext8380.inf` `FormatsAndModes` registry blobs (authoritative
host-side format tables; the .sys only holds generic parsing code):

- Sample rates enumerated per endpoint (`nSamplesPerSec` REG_DWORD):
  - `0xBB80` (48000) — 50 occurrences (default rate for essentially all pins/modes)
  - `0xAC44` (44100) — 8 occurrences
  - `0x5622` (22050) — 8 occurrences
  - No 96k/192k in INF (host endpoints are 48k-centric).
- Container: `container_size`/`bit_depth` 32, `data_type: float`, interleaved
  (from SurfaceAPO JSON `InputFormats`).
- `MaxFramesPerIteration: 480` (10 ms @48k) — processing quantum.
- Raw binary 32-bit-LE hits of 48000/96000/192000 in `qcaudminiport8380.sys`
  (e.g. 0x2073c, 0x2376c, 0x9e53c) are consistent with internal rate tables but
  could not be attributed without disassembly. Confidence: low-medium.
- Hardware latency (INF `QCAUD\StreamLatency`): CommonLatencyInMs=9,
  RenderLatencyInMs=9, capture latencies 0, capture pipeline latency 3 ms.
- Buffer sizes: PullModeRender min 6–10 ms per mode; PushModeCapture min 6–40 ms.

## 4. ANC / noise / calibration data

- **ANC (feedforward/feedback active noise cancellation): no evidence found.**
  No ANC coefficient blobs, no `ANC` strings anywhere in the audio .sys/.dlls.
  These devices use *speaker protection* + *noise suppression*, not ANC. Confidence: high.
- **Speaker protection calibration** (this is the "calibration" on this platform):
  - .sys UTF-16 strings @ `0x33db0` "Speaker protection recalibration",
    `0x33ef8` "Speaker protection ftm" (FTM = test mode), work-item context
    `SpkrProtCalibProcWorkItemContext` @ `0x1cb60`.
  - INF knobs (`surface_audiominiext8380.inf`):
    - `QCAUD\Feature\SpeakerProtection,SpeakerCoolDownTimerInSec = 0xB4` (180 s)
    - `QCAUD\Feature\SpeakerProtection,IsSpeakerProtQuickCalib = 0` (full calib)
    - Corresponding strings in .sys @ `0xa27d8`–`0xa2838`.
  - The calibration *coefficients themselves* (speaker Thiele-Small / excursion
    limits) live in the ADSP, not in this driver — no large float tables found
    in .rdata beyond the SurfaceAPO EQ (below). Confidence: medium.
- **Noise suppression** (mic NS): string "Noise suppression" @ `0x34188`,
  "Acoustic Echo Cancellation" @ `0x34040`, "Sound Focus" @ `0x342d0`,
  "Equalizer" @ `0x34418`, "QssPopup" @ `0x34560` (KS topology node names for
  the audio module UI). INF: `AudioModule\NoiseSuppression,QueryDebounceTimeInSec=5`.
- **SurfaceAPO EQ coefficients** (per-mode render MFX, `apoextension/SurfaceAPO_0?.json`):
  - DEFAULT/RAW/MOVIE/MEDIA/COMMS modes: identity 16×(1,0,0,0,0) matrices (bypass).
  - NOTIFICATION mode: actual biquad high-pass-ish coefficients, e.g.
    `[0.9856165, -1.93888521, 0.956712663, -1.93888521, 0.942329168]` ×2 ch,
    with a second variant `[0.984386146, -1.93332613, 0.9530095, -1.93332613, 0.937395632]`.
    (5-coefficient biquad `b0,b1,b2,a1,a2` — pole/zero pair near ~-0.97…-0.94,
    characteristic of a low-frequency notch/high-pass around 100–200 Hz @48k.)
  - Per-board default speaker volume (per-revision tuning, `Speaker_REV_xx`):
    - REV_0A: `DefaultDeviceVolume = 0xFFF10000` = **−15.0 dBFS**
    - REV_0B: `0xFFF03334` = **−15.8 dBFS**
    - REV_0C: `0xFFF3999A` = **−12.4 dBFS**
    - REV_0D: `0xFFF40000` = **−12.0 dBFS**
    - Range: MaxEpVolume 0 dBFS, MinEpVolume `0xffb50000` (−75 dBFS), step `0x8000` (0.5 dB)
  - `BoostEnabled` string @ `0xa1938` (mic HSM boost, codec-side).
  - Volume-limit: `EnableVolumeLimit`/`VolumeLimit`/`VolumeThresholdPlayTimeLimitinSec` @ `0xa2108`–`0xa2148` (safe-listening limiter).

## 5. Power management / noise findings (relevant to issue #66)

Registry power config shipped by `qcaudminiport_Base8380.inf` (lines 69–71):

```
HKR,PowerSettings,ConservationIdleTime,%REG_BINARY%,05,00,00,00   ; 5 s
HKR,PowerSettings,IdlePowerState,%REG_BINARY%,01,00,00,00         ; D1 (not D3!) on idle
HKR,PowerSettings,PerformanceIdleTime,%REG_BINARY%,05,00,00,00    ; 5 s
```

- **Windows parks the audio device in D1, not D3, after 5 s idle.** If Linux
  runtime-PM fully powers down the SoundWire link/codec (D3cold / clock gating
  / LPASS reset) and then brings it back up on stream start, the resulting
  codec re-init and PA re-enable sequence is a classic source of pop/click and
  idle "current noise". Recommendation: mirror the Windows hint — keep the codec
  core resourced, park at most the SoundWire bus clock, on X1E qcaudio.
  Confidence: **medium-high** (INF is authoritative; causal link to #66 noise is hypothesis).
- `DefaultSvaPowerType = 2` (voice activation power profile; PowerType strings
  `LowPower`, `HighPower`, `NumListenPowerTypes` @ `0xa1508`–`0xa15b8` in .sys)
  — keyword detector keeps partial power in low-power states.
- `DisableAcPowerNotifications/DisableDisplayNotifications/DisableOffloadNotifications`
  all = 0 (enabled): Windows re-tunes power on AC/DC change and display on/off.
  The display-off transition triggers an audio power-state reconfiguration —
  matches user reports of noise appearing after display/sleep transitions.
- Speaker `SpeakerCoolDownTimerInSec=180` with full (not quick) calibration means
  Windows re-runs speaker-protection calibration passes periodically; spurious
  excitation during these passes is audible if not muted. Linux has no equivalent
  — not a transplantable fix, but explains behavioral differences.
- Static pop mitigation observed: EQ `BypassTransitionTimeMs` ramp parameters in
  SurfaceAPO JSON (fade transitions when toggling effects) — the Windows stack
  *always ramps* volume/EQ changes; instant gain changes on Linux will pop.
  Confidence: medium.

## 6. Notable strings (qcaudminiport8380.sys, file offsets)

| Offset | String | Meaning |
|---|---|---|
| 0x1cf18 | `Z:\b\WP\AudioMiniport\rel\10.9\ARM64\Release\qcaudminiport8380.pdb` | build path |
| 0x1cb60 | `SpkrProtCalibProcWorkItemContext` | speaker protection calib worker |
| 0x33db0/0x33ef8 | Speaker protection recalibration / ftm | audio module names |
| 0x34040/0x34188/0x342d0/0x34418 | AEC / Noise suppression / Sound Focus / Equalizer | topology nodes |
| 0xa1508–0xa15b8 | HighPower/LowPower/PowerType0-2 | power profile registry |
| 0xa1910 | `UseInternalCodec` | codec routing switch |
| 0xa1938 | `BoostEnabled` | mic boost |
| 0xa2108–0xa2148 | EnableVolumeLimit/VolumeLimit/VolumeThresholdPlayTimeLimitinSec | safe limiter |
| 0xa2800/0xa2838 | SpeakerCoolDownTimerInSec / IsSpeakerProtQuickCalib | spkr prot config |
| 0xa2920 | `MaxSampleRate` | rate limit key |

## 7. Limitations

1. No REA tools → **no disassembly/decompilation**; all .sys findings are from
   string/data scanning and INF correlation. Call graphs, IOCTL codes, and any
   compiled-in SoundWire register sequences are unrecovered.
2. Actual codec (WCD class) driver, SoundWire master programming, and DSP
   firmware (ADSP/audioreach graph) are **not in this package**; SoundWire
   port maps cannot be extracted from here.
3. `AcxProxyAPO.dll`/`ProxyAPO.dll`/Dolby DLLs were only string-scanned; their
   DSP parameter blobs (if any) are opaque without disassembly.
4. Sample-rate 32-bit hits inside the .sys could not be confidently mapped to
   tables.
5. No `close_binary` calls possible (REA toolset absent; nothing was opened in
   an REA session).
