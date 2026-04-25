# Brain CV Tuner

This is a small firmware that helps you dial in the CV outputs on your Brain module so they're actually accurate. You walk through `0V` to `10V` in one-volt steps on both channel A and B, nudging each step until the output lands exactly where it should. The values you save get tucked away in the Brain SDK's calibration storage, which means any other Brain SDK firmware you flash later will pick them up automatically.

## Installation

You'll get two UF2 files out of the build — pick the one that matches your hardware:

- `brain-cv-tuner-pico.uf2` for RP2040
- `brain-cv-tuner-pico-2.uf2` for RP2350

To flash it, hold the BOOTSEL button while plugging the Brain into your computer. Once it shows up as a USB drive just drop the UF2 file onto it and it'll reboot into the firmware.

## CV Tuner User Guide

### What you need

The most reliable way to calibrate is with a multimeter, so that's what this guide assumes. You'll need:

- Your Brain module running this firmware
- A reasonably accurate multimeter (a decent handheld DMM is plenty, and the more precise it is the better the calibration)
- A patch cable from the output to your meter's input

If you'd rather calibrate by ear using a VCO and a tuner, that's covered further down in [Alternative: calibrating with a VCO and tuner](#alternative-calibrating-with-a-vco-and-tuner).

### Setting up the multimeter

Set your multimeter to DC voltage, on a range that covers `10V` (usually the `20V` DC range on a manual meter, or just leave it on auto). Connect the black probe to the Brain's ground (any sleeve/ring of an unused jack works), then connect the red probe to the tip of the CV output you're about to calibrate.

That's the whole setup. You're going to step through each voltage and turn the pots until the meter shows the right number.

### Controls

The two buttons walk you through the calibration steps and the pots do the actual tuning:

- **Button A (short press)**: step the voltage down (`10 → 0`)
- **Button B (short press)**: step the voltage up (`0 → 10`)
- **Button A (long press, ~800ms)**: switch between channel A and B
- **Buttons A + B (hold ~1.5s)**: save your calibration to flash
- **Pot 1**: coarse offset
- **Pot 2**: fine offset

A couple of things worth knowing. Step `0` is just a reference point at `0V`, so the pots don't do anything there, which is expected. Only the channel you're working on actually outputs voltage; the other one is held at `0V` so you don't accidentally probe the wrong jack. And after you change steps or switch channels, the pots use pickup behavior, so you might need to turn one a bit before it "catches" and starts moving the offset.

### What the LEDs are telling you

- **LED1**: channel A is the one you're editing
- **LED2**: channel B is the one you're editing
- **LED3**: you have unsaved changes
- **LED4**: save result. Two long pulses means it worked, four short pulses means it didn't
- **LED5**: storage layout status. Solid means everything's fine and saving is allowed. Blinking means the storage layout isn't protected, and saving is blocked (unless the firmware was built with the unsafe override)
- **LED6**: which step you're on. Steps `1` through `10` show that many short pulses; step `0` is a single long pulse

### Calibrating, step by step

Start at step `0`. Your meter should read very close to `0V`, which is just the reference, with nothing to adjust. Press **Button B** to bump up to step `1`, which should output `1V`. Watch the meter and turn **Pot 1** for coarse changes, then **Pot 2** for fine adjustments, until the meter reads exactly `1.000V` (or as close as your meter resolves).

Press **Button B** again to move to step `2` and repeat, adjusting until you see `2.000V`. Keep going all the way up to step `10` (`10.000V`). Each step is independent, so take your time on each one.

Once channel A is done, long-press **Button A** to switch over to channel B (LED2 lights up). Move your probe over to the channel B output and run through the same `1V → 10V` walk.

When both channels feel good, hold **Button A and B together** for about a second and a half. LED4 will flash two long pulses to confirm the save, and LED3 (the "unsaved changes" indicator) will turn off. That's it, you're calibrated.

### Saving and what sticks around

Nothing gets written to flash until you do the deliberate A+B hold, so you can experiment freely without worrying about clobbering anything. Once saved, the calibration survives power cycles and firmware updates, as long as whatever you flash next reserves the Brain SDK storage sectors (most SDK-based firmwares do).

If the save fails for some reason, LED4 will show the failure pattern and LED3 will stay on so you know your changes didn't make it. Flashing a firmware that doesn't respect the SDK storage layout is the main way you'd lose calibration data, so be a little careful with non-SDK firmwares.

### Alternative: calibrating with a VCO and tuner

If you don't have a multimeter handy, you can calibrate by ear-and-eye using a VCO and a chromatic tuner. It's less precise than a meter but works in a pinch.

You'll need a VCO with a `1V/oct` pitch input, a tuner (hardware unit or a plugin/app), and a patch cable. Start by disconnecting Brain from the VCO entirely and tuning the VCO to `C0` using just the tuner. Once that's set, patch Brain's output A (or B) into the VCO's `1V/oct` input and leave the tuner watching the VCO's audio output.

From there the workflow is the same as above. Step up through `1V`, `2V`, and so on, but instead of watching a meter you're adjusting until the tuner reads `C1`, `C2`, etc. Keep in mind that VCO tuning drifts with temperature and the tuner itself has its own accuracy limits, so a meter will generally give you a cleaner result.

## Migrating Older Firmwares to Brain SDK Storage

Use this checklist when upgrading an older Brain firmware so calibration written by this tuner persists and is reused.

### 1) Reserve storage sectors in CMake

Add the storage reservation helper before `pico_sdk_init()` in your main CMakeList.txt:

```cmake
set(BRAIN_STORAGE_ENABLE_FLASH_RESERVATION ON CACHE BOOL "" FORCE)
set(BRAIN_STORAGE_ALLOW_UNPROTECTED_LAYOUT OFF CACHE BOOL "" FORCE)
include(brain-sdk/cmake/brain-storage-reserve-flash.cmake)
brain_storage_configure_flash_reservation()
```

This keeps the SDK calibration/app sectors outside the firmware image.

### 2) Load calibration at startup

If you want to use calibrated values in your firmware, add this in your init section:

```cpp
#define BRAIN_USE_STORAGE 1
#define BRAIN_USE_OUTPUTS 1
#include "brain/brain.h"

Brain brain;
brain.init_outputs();
brain.outputs.set_output_range(AudioCvOutChannel::kChannelA, AudioCvOutRange::kRange0To10V);
brain.outputs.set_output_range(AudioCvOutChannel::kChannelB, AudioCvOutRange::kRange0To10V);

// Optional: continue with raw output if missing/corrupt.
brain.outputs.load_calibration_from_flash();
```

### 3) Switch output calls to calibrated writes

Replace raw calls:

```cpp
brain.outputs.set_voltage_millivolts(channel, millivolts);
```

with:

```cpp
brain.outputs.set_voltage_calibrated_millivolts(channel, millivolts);
```

Example conversion: `1.25V -> 1250mV`.

### 4) If your firmware writes calibration, use SDK APIs only

- Use `brain.storage.write_cv_calibration(...)` and verify with `brain.storage.read_cv_calibration(...)`.
- Keep the SDK contract: `CvCalibrationV1` stores offsets for `1V..10V` only (`0V` is reference-only).
- Do not write raw flash directly for calibration.

### 5) Migration caveat for very old firmwares

If a legacy firmware stored calibration in a custom/non-SDK flash location, that data is not automatically imported. A one-time migration firmware is needed to read the old format and write `CvCalibrationV1` through Brain SDK storage APIs.

### 6) Add app-level persistent data (app blob)

Use Brain SDK `app_blob` APIs for firmware-specific settings/state that should persist.

- APIs: `read_app_blob(...)`, `write_app_blob(...)`, `clear_app_blob()`
- Storage model: one SDK-managed app-data sector (single blob record)
- Max payload: `4096 - 12 - 4 = 4080` bytes

Recommended pattern:

1. Define a versioned app state struct.
2. On boot, call `read_app_blob(...)`.
3. If status is `kNotFound` or `kCorrupt`, initialize defaults.
4. If status is `kOk`, validate `actual_size` and your own app schema version.
5. After settings changes, write back via `write_app_blob(...)`.

Example:

```cpp
#define BRAIN_USE_STORAGE 1
#include "brain/brain.h"

struct AppStateV1 {
	uint32_t schema_version;
	float last_tempo_bpm;
	uint8_t mode;
};

constexpr uint32_t kAppStateSchemaV1 = 1;

Brain brain;
brain.init_storage();

AppStateV1 state{};
size_t actual_size = 0;

StorageStatus load_status =
	brain.storage.read_app_blob(&state, sizeof(state), &actual_size);

if (load_status == StorageStatus::kOk &&
	actual_size == sizeof(state) &&
	state.schema_version == kAppStateSchemaV1) {
	// Loaded successfully.
} else {
	// Missing/corrupt/mismatched version: use defaults.
	state.schema_version = kAppStateSchemaV1;
	state.last_tempo_bpm = 120.0f;
	state.mode = 0;
}

// Later, when persisting:
StorageStatus save_status =
	brain.storage.write_app_blob(&state, sizeof(state));
```

Status handling tips:

- `kTooLarge` on read: stored blob is larger than your read buffer.
- `kTooLarge` on write: payload exceeds app blob capacity.
- `kUnprotectedLayout`: firmware image was built without storage reservation.

Important:

- Calibration (`CvCalibrationV1`) is device-level and shared across SDK-based firmwares.
- App blob data is firmware-level by design. Use your own schema versioning and migration logic as the app evolves.

## Development

This project includes brain-sdk as a git submodule. To update the SDK:

```bash
cd brain-sdk
git pull origin main
cd ..
git add brain-sdk
git commit -m "Update brain-sdk"
```

## Build

To build the firmware, run:

```bash
./build-firmware.sh
```