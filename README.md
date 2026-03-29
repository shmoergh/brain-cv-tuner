# Brain CV Tuner

Firmware for calibrating Brain CV outputs with a tuner-guided workflow. It lets you calibrate channel A and B across `0V..10V` in 1V steps for precise 1V/oct tracking. The calibration values are saved to Brain SDK calibration storage and persisted across firmwares which use the Brain SDK. This allows a single calibration to be automatically reused between different firmwares.

## Build

```bash
./build-firmware.sh
```

## Flash

Flash one of the generated UF2 files to your Brain module by holding the BOOTSEL button while connecting it to your computer, then copy the .uf2 file to the mounted drive:

- `brain-cv-tuner-pico.uf2` (RP2040)
- `brain-cv-tuner-pico-2.uf2` (RP2350)

## CV Tuner User Guide

### What You Need

- Brain module running this firmware
- A VCO with `1V/oct` pitch input
- A tuner (hardware tuner or tuner plugin/app)
- Patch cable from Brain CV out to the VCO pitch input

### Recommended Setup

1. Disconnect Brain from the VCO pitch input.
2. Tune the VCO to `C0` using your tuner.
3. Connect Brain output `A` (or `B`) to the VCO `1V/oct` input.
4. Leave the tuner connected to the VCO audio output.

### Controls

- `Button A (short)`: step down voltage (`10 -> 0`)
- `Button B (short)`: step up voltage (`0 -> 10`)
- `Button A (long, ~800ms)`: toggle active channel (`A <-> B`)
- `Button A + B (hold ~1500ms)`: save calibration to flash
- `Pot 1`: coarse tuning offset
- `Pot 2`: fine tuning offset

Notes:
- Step `0` (`0V`) is reference-only. Pot edits are ignored at this step.
- Only the active channel outputs the selected calibration voltage. The inactive channel is held at `0V`.
- After changing step or channel, pots use pickup behavior. You may need to turn a pot until it "catches" before it starts changing the offset.

### LED Feedback

- `LED1`: channel A is active
- `LED2`: channel B is active
- `LED3`: unsaved changes (`dirty`)
- `LED4`: save result
- Success: 2 long pulses
- Failure: 4 short pulses
- `LED5`: storage layout protection status
- Solid on: protected (save allowed)
- Blinking: unprotected (save blocked unless unsafe override is compiled)
- `LED6`: current step indicator
- Step `1..10`: that many short pulses
- Step `0`: one long pulse

### Calibration Workflow

1. Start at step `0` and confirm your VCO is tuned to `C0` (reference point).
2. Press `Button B` to go to step `1` (`1V`), then adjust with `Pot 1` and `Pot 2` until tuner reads `C1`.
3. Repeat for each step up to `10` (`C10`).
4. Long-press `Button A` to switch channel and repeat for the other output.
5. Hold `Button A + Button B` for ~1.5s to save.
6. Confirm save success on `LED4` and that `LED3` turns off.

### Save and Persistence

- Calibration is saved only when you perform the explicit `A+B` hold.
- Saved values persist across power cycles and firmware updates (as long as calibration storage is preserved).
- If save fails, `LED4` shows failure pulses and `LED3` remains on.
- Flashing firmware that does not reserve Brain SDK storage sectors can overwrite calibration data.

## Migrating Older Firmwares to Brain SDK Storage

Use this checklist when upgrading an older Brain firmware so calibration written by this tuner persists and is reused.

### 1) Reserve storage sectors in CMake

Add the storage reservation helper before `pico_sdk_init()`:

```cmake
include(brain-sdk/cmake/brain-storage-reserve-flash.cmake)
brain_storage_configure_flash_reservation()
```

This keeps the SDK calibration/app sectors outside the firmware image.

### 2) Load calibration at startup

In your firmware init path:

```cpp
#include "brain-io/audio-cv-out.h"

brain::io::AudioCvOut cv_out;
cv_out.init();
cv_out.set_coupling(brain::io::AudioCvOutChannel::kChannelA, brain::io::AudioCvOutCoupling::kDcCoupled);
cv_out.set_coupling(brain::io::AudioCvOutChannel::kChannelB, brain::io::AudioCvOutCoupling::kDcCoupled);

// Optional: continue with raw output if missing/corrupt.
cv_out.load_calibration_from_flash();
```

### 3) Switch output calls to calibrated writes

Replace raw calls:

```cpp
cv_out.set_voltage(channel, volts);
```

with:

```cpp
cv_out.set_voltage_calibrated(channel, volts);
```

### 4) If your firmware writes calibration, use SDK APIs only

- Use `brain::storage::write_cv_calibration(...)` and verify with `read_cv_calibration(...)`.
- Keep the SDK contract: `CvCalibrationV1` stores offsets for `1V..10V` only (`0V` is reference-only).
- Do not write raw flash directly for calibration.

### 5) Migration caveat for very old firmwares

If a legacy firmware stored calibration in a custom/non-SDK flash location, that data is not automatically imported. A one-time migration firmware is needed to read the old format and write `CvCalibrationV1` through Brain SDK storage APIs.

## Development

This project includes brain-sdk as a git submodule. To update the SDK:

```bash
cd brain-sdk
git pull origin main
cd ..
git add brain-sdk
git commit -m "Update brain-sdk"
```
