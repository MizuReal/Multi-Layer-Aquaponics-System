# MQ135 Calibration Note

## Observed Behavior

| Phase | Reading | Cause |
|-------|---------|-------|
| System boot | 400 ppm | Software default (`aqPPM = 400.0f`), not a sensor reading |
| Buffer/gas exposure | Spikes high | Rs drops → ratio < 1 → `pow(<1, −2.86)` = large value |
| Recovery (minutes after exposure) | 0 ppm | Rs overshoots above Ro → ratio > 1 → `pow(>1, −2.86)` → 0 |
| Stable clean air | 500–800 ppm (indoor) | Sensor equilibrates — actual room CO₂ level |

## Why 0 ppm (Not 400)?

The formula is:

```
aqPPM = 7905.5 × (Rs / Ro)^(−2.862)
```

Where `Ro = 5417 Ω` was calibrated during a 9-hour burn-in. At that time, the sensor was either:
- Still stabilizing (heater not fully settled)
- In an enclosed space with elevated CO₂

Result: `Ro` is **too low**. In true clean air, `Rs > Ro` → `ratio > 1` → `pow(>1, −2.86)` approaches 0.

When the sensor recovers from gas exposure, it temporarily overshoots clean-air resistance before settling. During this overshoot window, the ratio exceeds 1 and the computed ppm hits the 0 cap.

## Fix (When Ready)

1. Place sensor in fresh **outdoor air** (~400 ppm CO₂)
2. Let it stabilize for 30+ minutes
3. Measure stable `Rs` via Serial debug (add `Serial.println(Rs)` to `computeMQ135()`)
4. Update `MQ135_RO` in `Compilation.ino` to the measured value

```
static const float MQ135_RO = <measured-clean-air-Rs>f;
```

After recalibration, the baseline in clean air will read ~400 ppm as expected.
