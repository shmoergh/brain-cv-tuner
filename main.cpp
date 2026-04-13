#include <pico/stdlib.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "brain/brain.h"

namespace {

class CalibrationApp {
	public:
		CalibrationApp() :
			button_a_(kButtonPin1, 35, kALongPressMs),
			button_b_(kButtonPin2, 35, kALongPressMs) {}

		void run() {
			init();
			while (true) {
				loop_once();
			}
		}

	private:
		static constexpr int kMinStep = 0;
		static constexpr int kMaxStep = 10;
		static constexpr int kOffsetMin = -1024;
		static constexpr int kOffsetMax = 1023;
		static constexpr int kCoarseMin = -410;	// ~ -1V in DAC LSB
		static constexpr int kCoarseMax = 410;		// ~ +1V in DAC LSB
		static constexpr int kFineMin = -32;
		static constexpr int kFineMax = 32;
		static constexpr int kCoarseQuantum = 16;

		static constexpr uint32_t kALongPressMs = 800;
		static constexpr uint32_t kSaveHoldMs = 1500;

		static constexpr uint8_t kLedChannelA = 0;
		static constexpr uint8_t kLedChannelB = 1;
		static constexpr uint8_t kLedDirty = 2;
		static constexpr uint8_t kLedSaveResult = 3;
		static constexpr uint8_t kLedLayoutProtected = 4;
		static constexpr uint8_t kLedStepPulse = 5;

		Brain brain_;
		Button button_a_;
		Button button_b_;

		CvCalibrationV1 working_cal_{};
		bool dirty_ = false;
		bool layout_protected_ = false;

		AudioCvOutChannel active_channel_ = kOutputsChannelA;
		int active_step_ = 0;

		int coarse_offset_lsb_ = 0;
		int fine_offset_lsb_ = 0;
		bool coarse_pickup_armed_ = true;
		bool fine_pickup_armed_ = true;
		int last_coarse_physical_ = 0;
		int last_fine_physical_ = 0;

		bool a_pressed_ = false;
		bool b_pressed_ = false;
		bool a_long_handled_ = false;
		bool a_suppressed_ = false;
		bool b_suppressed_ = false;
		bool combo_active_ = false;
		bool combo_consumed_ = false;
		absolute_time_t a_press_time_ = nil_time;
		absolute_time_t combo_start_time_ = nil_time;

		static int clamp_int(int value, int min_value, int max_value) {
			if (value < min_value) {
				return min_value;
			}
			if (value > max_value) {
				return max_value;
			}
			return value;
		}

		static int round_to_nearest_multiple(int value, int multiple) {
			const int rem = value % multiple;
			int rounded = value - rem;
			if (rem >= (multiple / 2)) {
				rounded += multiple;
			} else if (rem <= -(multiple / 2)) {
				rounded -= multiple;
			}
			return rounded;
		}

		static uint32_t millis_since(absolute_time_t t0) {
			const int64_t elapsed_us = absolute_time_diff_us(t0, get_absolute_time());
			if (elapsed_us <= 0) {
				return 0;
			}
			return static_cast<uint32_t>(elapsed_us / 1000);
		}

		static int coarse_from_adc(uint16_t adc) {
			return kCoarseMin
				+ static_cast<int>((static_cast<int32_t>(adc) * (kCoarseMax - kCoarseMin)) / 4095);
		}

		static int fine_from_adc(uint16_t adc) {
			return kFineMin
				+ static_cast<int>((static_cast<int32_t>(adc) * (kFineMax - kFineMin)) / 4095);
		}

		static const char* to_string(StorageStatus status) {
			switch (status) {
				case kStorageStatusOk:
					return "kOk";
				case kStorageStatusInvalidArgument:
					return "kInvalidArgument";
				case kStorageStatusNotFound:
					return "kNotFound";
				case kStorageStatusCorrupt:
					return "kCorrupt";
				case kStorageStatusOutOfBounds:
					return "kOutOfBounds";
				case kStorageStatusTooLarge:
					return "kTooLarge";
				case kStorageStatusUnprotectedLayout:
					return "kUnprotectedLayout";
				case kStorageStatusFlashError:
					return "kFlashError";
				case kStorageStatusTimeout:
					return "kTimeout";
				case kStorageStatusNotPermitted:
					return "kNotPermitted";
				default:
					return "kUnknown";
			}
		}

		void init() {
			stdio_init_all();

			const BrainInitStatus leds_status = brain_.init_leds(LedMode::kSimple);
			if (!brain_init_succeeded(leds_status)) {
				std::printf("[ERROR] Leds init failed\n");
			}
			brain_.leds.off_all();

			button_a_.init(true);
			button_b_.init(true);
			button_a_.set_on_press([this]() { on_a_press(); });
			button_a_.set_on_release([this]() { on_a_release(); });
			button_b_.set_on_press([this]() { on_b_press(); });
			button_b_.set_on_release([this]() { on_b_release(); });

			PotsConfig pots_cfg = create_default_pots_config(3, 12);
			pots_cfg.change_threshold = 0;
			const BrainInitStatus pots_status = brain_.init_pots(pots_cfg);
			if (!brain_init_succeeded(pots_status)) {
				std::printf("[ERROR] Pots init failed\n");
			}
			brain_.pots.scan();

			const BrainInitStatus outputs_status = brain_.init_outputs();
			if (!brain_init_succeeded(outputs_status)) {
				std::printf("[ERROR] Outputs init failed\n");
			}
			brain_.outputs.set_output_range(
				kOutputsChannelA, kOutputsRange0To10V);
			brain_.outputs.set_output_range(
				kOutputsChannelB, kOutputsRange0To10V);
			brain_.outputs.set_voltage_millivolts(kOutputsChannelA, 0);
			brain_.outputs.set_voltage_millivolts(kOutputsChannelB, 0);

			const StorageStatus load_status = brain_.storage.read_cv_calibration(&working_cal_);
			if (load_status == kStorageStatusOk) {
				std::printf("Loaded calibration from flash\n");
			} else if (
				load_status == kStorageStatusNotFound || load_status == kStorageStatusCorrupt) {
				std::memset(&working_cal_, 0, sizeof(working_cal_));
				std::printf(
					"Calibration missing/corrupt (%s), using zero table\n",
					to_string(load_status));
			} else {
				std::memset(&working_cal_, 0, sizeof(working_cal_));
				std::printf("Calibration read failed (%s), using zero table\n", to_string(load_status));
			}

			layout_protected_ = brain_.storage.is_layout_protected();
			if (layout_protected_) {
				brain_.leds.stop_blink(kLedLayoutProtected);
				brain_.leds.on(kLedLayoutProtected);
			} else {
				brain_.leds.start_blink(kLedLayoutProtected, 250);
			}

			enter_context();
			update_status_leds();
			pulse_step_led();
			std::printf("brain-cv-tuner calibration firmware started\n");
		}

		void loop_once() {
			button_a_.update();
			button_b_.update();
			maybe_handle_holds();

			update_pots();
			drive_cv_outputs();
			update_status_leds();

			brain_.update_leds();

			sleep_ms(2);
		}

		void on_a_press() {
			a_pressed_ = true;
			a_long_handled_ = false;
			a_suppressed_ = false;
			a_press_time_ = get_absolute_time();

			if (b_pressed_) {
				start_combo();
			}
		}

		void on_a_release() {
			a_pressed_ = false;
			if (!a_suppressed_ && !a_long_handled_) {
				step_down();
			}
			reset_combo_if_released();
		}

		void on_b_press() {
			b_pressed_ = true;
			b_suppressed_ = false;

			if (a_pressed_) {
				start_combo();
			}
		}

		void on_b_release() {
			b_pressed_ = false;
			if (!b_suppressed_) {
				step_up();
			}
			reset_combo_if_released();
		}

		void start_combo() {
			combo_active_ = true;
			combo_consumed_ = false;
			combo_start_time_ = get_absolute_time();
			a_suppressed_ = true;
			b_suppressed_ = true;
		}

		void reset_combo_if_released() {
			if (!a_pressed_ && !b_pressed_) {
				combo_active_ = false;
				combo_consumed_ = false;
			}
		}

		void maybe_handle_holds() {
			if (a_pressed_ && b_pressed_) {
				if (!combo_active_) {
					start_combo();
				}

				if (!combo_consumed_ && millis_since(combo_start_time_) >= kSaveHoldMs) {
					save_calibration();
					combo_consumed_ = true;
					a_long_handled_ = true;
				}
				return;
			}

			if (a_pressed_ && !a_suppressed_ && !a_long_handled_ &&
				millis_since(a_press_time_) >= kALongPressMs) {
				toggle_channel();
				a_long_handled_ = true;
			}
		}

		void step_down() {
			if (active_step_ <= kMinStep) {
				return;
			}
			active_step_--;
			enter_context();
			pulse_step_led();
			std::printf("Step -> %dV\n", active_step_);
		}

		void step_up() {
			if (active_step_ >= kMaxStep) {
				return;
			}
			active_step_++;
			enter_context();
			pulse_step_led();
			std::printf("Step -> %dV\n", active_step_);
		}

		void toggle_channel() {
			if (active_channel_ == kOutputsChannelA) {
				active_channel_ = kOutputsChannelB;
			} else {
				active_channel_ = kOutputsChannelA;
			}
			enter_context();
			std::printf(
				"Active channel -> %s\n",
				active_channel_ == kOutputsChannelA ? "A" : "B");
		}

		void pulse_step_led() {
			brain_.leds.off(kLedStepPulse);
			if (active_step_ == 0) {
				brain_.leds.blink(kLedStepPulse, 1, 350);
				return;
			}
			brain_.leds.blink(kLedStepPulse, static_cast<uint>(active_step_), 120);
		}

		void pulse_save_success() {
			brain_.leds.off(kLedSaveResult);
			brain_.leds.blink(kLedSaveResult, 2, 320);
		}

		void pulse_save_failure() {
			brain_.leds.off(kLedSaveResult);
			brain_.leds.blink(kLedSaveResult, 4, 100);
		}

		void save_calibration() {
			layout_protected_ = brain_.storage.is_layout_protected();
			if (!layout_protected_ && !kAllowUnprotectedLayout) {
				std::printf("Save blocked: layout is not protected\n");
				pulse_save_failure();
				return;
			}

			const StorageStatus write_status = brain_.storage.write_cv_calibration(&working_cal_);
			if (write_status != kStorageStatusOk) {
				std::printf("Save failed on write: %s\n", to_string(write_status));
				pulse_save_failure();
				return;
			}

			CvCalibrationV1 verify{};
			const StorageStatus verify_status = brain_.storage.read_cv_calibration(&verify);
			if (verify_status != kStorageStatusOk) {
				std::printf("Save verify read failed: %s\n", to_string(verify_status));
				pulse_save_failure();
				return;
			}

			if (std::memcmp(&verify, &working_cal_, sizeof(CvCalibrationV1)) != 0) {
				std::printf("Save verify mismatch\n");
				pulse_save_failure();
				return;
			}

			dirty_ = false;
			pulse_save_success();
			std::printf("Calibration save successful\n");
		}

		int16_t* current_offset_slot() {
			if (active_step_ < 1 || active_step_ > 10) {
				return nullptr;
			}
			const int idx = active_step_ - 1;
			if (active_channel_ == kOutputsChannelA) {
				return &working_cal_.a_offset_lsb[idx];
			}
			return &working_cal_.b_offset_lsb[idx];
		}

		int current_offset() const {
			if (active_step_ < 1 || active_step_ > 10) {
				return 0;
			}
			const int idx = active_step_ - 1;
			if (active_channel_ == kOutputsChannelA) {
				return working_cal_.a_offset_lsb[idx];
			}
			return working_cal_.b_offset_lsb[idx];
		}

		void enter_context() {
			int offset = current_offset();
			if (active_step_ == 0) {
				offset = 0;
			}

			int coarse = round_to_nearest_multiple(offset, kCoarseQuantum);
			coarse = clamp_int(coarse, kCoarseMin, kCoarseMax);
			int fine = offset - coarse;

			while (fine > kFineMax && coarse < kCoarseMax) {
				coarse = clamp_int(coarse + kCoarseQuantum, kCoarseMin, kCoarseMax);
				fine = offset - coarse;
			}
			while (fine < kFineMin && coarse > kCoarseMin) {
				coarse = clamp_int(coarse - kCoarseQuantum, kCoarseMin, kCoarseMax);
				fine = offset - coarse;
			}

			fine = clamp_int(fine, kFineMin, kFineMax);
			coarse = clamp_int(coarse, kCoarseMin, kCoarseMax);

			coarse_offset_lsb_ = coarse;
			fine_offset_lsb_ = fine;

			brain_.pots.scan();
			last_coarse_physical_ = coarse_from_adc(brain_.pots.get_buffered(0));
			last_fine_physical_ = fine_from_adc(brain_.pots.get_buffered(1));
			coarse_pickup_armed_ = true;
			fine_pickup_armed_ = true;
		}

		bool pickup_reached(int previous, int current, int target, int hysteresis) const {
			if (current == target) {
				return true;
			}
			if (current >= (target - hysteresis) && current <= (target + hysteresis)) {
				return true;
			}
			if (previous < target && current > target) {
				return true;
			}
			if (previous > target && current < target) {
				return true;
			}
			return false;
		}

		void apply_current_offset() {
			if (active_step_ == 0) {
				return;
			}

			int16_t* slot = current_offset_slot();
			if (!slot) {
				return;
			}

			const int next = clamp_int(coarse_offset_lsb_ + fine_offset_lsb_, kOffsetMin, kOffsetMax);
			if (*slot != next) {
				*slot = static_cast<int16_t>(next);
				dirty_ = true;
			}
		}

		void update_pots() {
			brain_.pots.scan();

			const int coarse_physical = coarse_from_adc(brain_.pots.get_buffered(0));
			const int fine_physical = fine_from_adc(brain_.pots.get_buffered(1));

			if (active_step_ == 0) {
				last_coarse_physical_ = coarse_physical;
				last_fine_physical_ = fine_physical;
				return;
			}

			if (coarse_pickup_armed_) {
				if (pickup_reached(last_coarse_physical_, coarse_physical, coarse_offset_lsb_, 2)) {
					coarse_pickup_armed_ = false;
				}
			} else if (coarse_physical != coarse_offset_lsb_) {
				coarse_offset_lsb_ = coarse_physical;
				apply_current_offset();
			}
			last_coarse_physical_ = coarse_physical;

			if (fine_pickup_armed_) {
				if (pickup_reached(last_fine_physical_, fine_physical, fine_offset_lsb_, 1)) {
					fine_pickup_armed_ = false;
				}
			} else if (fine_physical != fine_offset_lsb_) {
				fine_offset_lsb_ = fine_physical;
				apply_current_offset();
			}
			last_fine_physical_ = fine_physical;
		}

		void drive_cv_outputs() {
			brain_.outputs.set_calibration(working_cal_);

			const int32_t step_millivolts = static_cast<int32_t>(active_step_) * 1000;
			if (active_channel_ == kOutputsChannelA) {
				brain_.outputs.set_voltage_calibrated_millivolts(
					kOutputsChannelA, step_millivolts);
				brain_.outputs.set_voltage_millivolts(kOutputsChannelB, 0);
			} else {
				brain_.outputs.set_voltage_calibrated_millivolts(
					kOutputsChannelB, step_millivolts);
				brain_.outputs.set_voltage_millivolts(kOutputsChannelA, 0);
			}
		}

		void update_status_leds() {
			if (active_channel_ == kOutputsChannelA) {
				brain_.leds.on(kLedChannelA);
				brain_.leds.off(kLedChannelB);
			} else {
				brain_.leds.off(kLedChannelA);
				brain_.leds.on(kLedChannelB);
			}

			if (dirty_) {
				brain_.leds.on(kLedDirty);
			} else {
				brain_.leds.off(kLedDirty);
			}

			if (layout_protected_) {
				brain_.leds.on(kLedLayoutProtected);
			}
		}
};

}  // namespace

int main() {
	CalibrationApp app;
	app.run();
	return 0;
}
