# Audio current-noise sequences — evidence report (OE issue #66)

Packages: `SurfaceUpdate` audio set — `ihvaudiofilter` (SurfaceIhvAudioFilterDriver
v1.15.4.0), `audiominiext` (surface_audiominiext8380.inf v8100.2025.6.11),
`apoextension` (SurfaceAPO_0A..0D.json). Verification: registry paths read
verbatim from INFs; APO JSON parsed directly. REA MCP namespace unregistered
in analysis sessions (known deviation); miniport disassembly deferred —
string offsets already in audio-report.md. Windows-driver content is data only.

## 1. ihvaudiofilter — SKIP for pop-noise [observed]

`AUCD\VEN_QCOM&DEV_0C29` extension; only per-REV endpoint flags:
`ENDPOINTReg_MSHW0462`: `HeadsetMicPresent=1, OnBoardMicPresent=0`;
`ENDPOINTReg_MSHW0486` (REV_0C/0D): both `=0`. No register sequences.

## 2. audiominiext — bounded-fix-worthy tunables [observed]

Verbatim from `surface_audiominiext8380.inf`:

- L174 `QCAUD\Feature\AudioModule\NoiseSuppression,QueryDebounceTimeInSec = 5`
- L175 `SpeakerProtection,SpeakerCoolDownTimerInSec = 0xB4` (180 s) — long
  speaker-protection cool-down gating amp re-enable; directly relevant to
  power-state gating for OE #66.
- L176 `IsSpeakerProtQuickCalib = 0` — full calibration passes, matching the
  180 s cool-down.
- L150–159 `QCAUD\WaveSpeaker,DefaultDeviceVolume` per REV: `0xFFF10000`
  (−15.0 dBFS, REV_0A), `0xFFF03334` (−15.8, 0B), `0xFFF3999A` (−12.4, 0C),
  `0xFFF40000` (−12.0, 0D) — REV-specific default gain applied at load; a
  plausible source of per-unit "current noise" level differences.
- L162–163 `DisableAcPowerNotifications=0`, `DisableDisplayNotifications=0`
  — power/display notification hooks enabled.

## 3. apoextension — soft-start is the anti-pop mechanism [observed/derived]

All REVs (Colchuck A–D): every EQ (`defaultEQ`, `rawEQ`, `notifEQ`, `commEQ`,
`movieEQ`, `mediaEQ`) has `Enabled=False`, identity coefficient matrices,
`BypassTransitionTimeMs=0`. `DependentParameterStore` starts
`VolumeControl/Ch0/Ch1 = −144.0 dB` — the APO ramps from a −144 dB
(near-mute) fade-in point to target volume on stream start: the only
anti-pop mechanism in these packages.

## 4. Not covered here

`SurfaceAPO.dll` / `AcxProxyAPO.dll` string pass and `qcaudminiport8380.sys`
DFC/EQ routing disassembly (string offsets 0xa2800/0xa2838 documented in
audio-report.md) — needs a session with working REA disassembly.

## Linux-side mapping notes [observed in tree]

- `sound/soc/qcom/x1e80100.c`: `x1e80100_snd_shutdown` calls
  `x1e80100_set_protection_ready` before `qcom_snd_sdw_shutdown`
  (~lines 307–325); `snd_soc_limit_volume` −3 dB digital volume caps
  (~lines 43–54); protected-speaker-feedback flag present.
- `sound/soc/qcom/lpass-hdmi.c`: `dp_audio` field enable/disable sequencing.
- Prior evidence (audio-report.md): Windows parks audio in **D1, not D3**
  after 5 s idle.

### Bounded-fix candidates (ranked)

1. Power-down sequencing: honor a speaker-protection cool-down /
   protection-ready handshake before amp path disable (x1e80100.c already
   has the hook — align timings with the 180 s Windows value as a tunable).
2. Stream-start soft-start ramp: −144 dB→target fade-in analog for the
   machine driver / dp_audio enable path (anti-pop).
3. Skip: ihvaudiofilter flags; APO EQ coefficients (identity/disabled).
4. Follow-up: qcaudminiport DFC disassembly for register-level pop
   sequences.
