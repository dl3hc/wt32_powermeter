# Handoff: ADC accuracy investigation (FWD/REF voltage readings)

## Context

Two firmware bugs were already found and fixed this session (both pushed to `main` and `1.0.3`):

1. **LED bar graph lag** — 1.0.3 added an EMA (`ema_alpha = 0.3`) on top of the existing
   50-sample hardware averaging in `read_directional_couplers()`, causing the FWD/REF LED bars
   to take ~6-7s to settle after a step change. Removed; readings are now direct again.
2. **VSWR overreporting for well-matched antennas** — 1.0.3's rewrite of `millivolt_to_dbm()`
   clamped any voltage below the lowest configured calibration point to that point's dBm value,
   instead of 1.0.2's behavior of interpolating from an implicit `(0 mV, 0 dBm)` anchor. This
   floored REF power at the calibration curve's minimum (e.g. 1W) regardless of how small the
   real reflection was, driving computed VSWR to 2-4:1+ for antennas actually close to 1:1.
   Fixed by restoring zero-anchor interpolation below the lowest point (ascending tables only);
   readings above the highest point still clamp, since there's no safe anchor near saturation.

## Open issue: FWD and REF both read ~128mV with zero RF present

After fixing the above, the device still reports a ~128mV floor on **both** FWD and REF
channels even with no signal at all. This isn't a calibration table issue — it shows up before
any calibration lookup happens, in the raw ADC reading itself.

Two things point at the ESP32's ADC as the actual root cause, not the diode detectors:

- `IO2_FWD = 2` and `IO4_REF = 4` (wt32_powermeter.ino:253-254) are **ADC2** pins, not ADC1.
  ADC2 on the original ESP32 is noisier and less linear than ADC1. Both channels landing on the
  *same* floor value is consistent with both sharing the same ADC2 characteristics.
- The ESP32 ADC (`analogReadMilliVolts()`, called at wt32_powermeter.ino:678-679) is known to be
  nonlinear/inaccurate below roughly 100-150mV at the default 11dB attenuation this firmware
  uses — the chip's factory characterization curve just doesn't resolve near-0V inputs cleanly
  and reports a small nonzero floor instead. ~128mV lines up with this.
- The detectors drive the ADC pins directly with no op-amp buffer, so source-impedance loading
  during the ADC's sample-and-hold window is also a plausible contributor.

This matters because the whole "REF reads too high for a well-matched antenna" investigation
lives almost entirely below ~150mV — exactly the ADC's worst-behaved region. No calibration
table fix can compensate for the ADC being unable to resolve true near-zero voltage in the
first place.

## Diagnostic step to run before or alongside the fixes below

Probe the diode detector outputs directly with a multimeter at true no-signal, and compare
against what `/readDATA` reports for `voltage_fwd`/`voltage_ref` (raw mV) at the same instant.

- Multimeter reads ~0-10mV, ESP32 reports ~128mV → confirms this is an ADC floor/offset issue,
  not a wiring or detector problem. Proceed with the fixes below.
- Multimeter also reads ~128mV → the detector itself has a real DC bias/offset at rest; the
  fixes below (especially the offset subtraction) still apply, but the ADC1 migration becomes
  less urgent since the ADC would be reporting truthfully.

## Approach 1: Fixed offset subtraction (cheap, software-only)

Since both channels show the same floor and it appears stable/repeatable, subtract a measured
constant offset from each raw reading in `read_directional_couplers()`
(wt32_powermeter.ino:666) before the value goes into `millivolt_to_dbm()`.

Steps:
1. With no RF present, log several raw `analogReadMilliVolts()` samples per channel (or just
   read the existing 128mV `/readDATA` output) to confirm the floor value and that it's stable
   across power cycles and time (not drifting with temperature).
2. Subtract that measured floor from `voltage_fwd_raw`/`voltage_ref_raw` (or from
   `voltage_fwd_now`/`voltage_ref_now` inside the sampling loop), clamping at 0 so a
   below-floor sample can't go negative.
3. Consider whether the offset should be a compile-time constant, a config value editable from
   the web UI (in case it drifts board-to-board), or measured at boot (e.g. by sampling before
   the RF stage is expected to have any signal — riskier, since "no signal at boot" isn't
   guaranteed).

Caveat: this only removes a consistent *additive* bias. If the ADC's error is genuinely
nonlinear (not just a flat offset) across the low range, this won't fully fix accuracy near the
floor — only the multimeter test above can confirm whether the error is offset-only or truly
nonlinear.

## Approach 2: Move FWD/REF to ADC1 pins (better fix, needs a rewire)

Move the detector connections from GPIO2/GPIO4 (ADC2) to ADC1 pins — GPIO32-39 on the ESP32.
ADC1 is the better-characterized, more accurate ADC on this chip and isn't shared with other
peripherals the way ADC2 is.

Steps:
1. Check the WT32-ETH01 pinout/schematic for which of GPIO32-39 are actually broken out and
   free (some may be used by the LAN8720 Ethernet PHY or the board's other peripherals — this
   needs checking against the WT32-ETH01 pin reference before committing to specific pins).
2. Update `IO2_FWD` / `IO4_REF` (wt32_powermeter.ino:253-254) to the new GPIO numbers.
3. Physically rewire the detector outputs to the new pins.
4. Recalibrate — moving ADCs likely changes the mV:dBm mapping slightly even at the same RF
   power, since ADC1 and ADC2 have different characterization curves. Expect to need fresh
   calibration tables afterward, not just a pin number change.

This is the more thorough fix and directly addresses the "which ADC" half of the problem, but
doesn't remove any residual error from driving the ADC pin without a buffer.

## Not yet started: op-amp buffer

Mentioned in the same conversation as a "best fix" (removes source-impedance loading
entirely) but is a hardware change beyond firmware scope, and lower priority than the two
approaches above. Worth revisiting after the ADC1 migration if a low-mV accuracy problem
persists even off ADC2.
