#include "comms/backend_init.hpp"
#include "config_defaults.hpp"
#include "core/CommunicationBackend.hpp"
#include "core/KeyboardMode.hpp"
#include "core/Persistence.hpp"
#include "core/mode_selection.hpp"
#include "core/pinout.hpp"
#include "core/state.hpp"
#include "input/DebouncedSwitchMatrixInput.hpp"
#include "input/DebouncedGpioButtonInput.hpp"
#include "input/NunchukInput.hpp"
#include "reboot.hpp"
#include "stdlib.hpp"

#include <config.pb.h>

Config config = default_config;

const size_t num_rows = 3;
const size_t num_cols = 4;
const uint row_pins[num_rows] = { 0, 1, 2};
const uint col_pins[num_cols] = { 6, 5, 4, 3 };

// clang-format off

const Button matrix[num_rows][num_cols] = {
    { BTN_LF8, BTN_LF7, BTN_LF6, BTN_LF5},
    { BTN_LF4, BTN_LF3, BTN_LF2, BTN_LF1},
    { BTN_LT5, BTN_LT4, BTN_LT3, BTN_LT2}
};

// clang-format on
const DiodeDirection diode_direction = DiodeDirection::COL2ROW;

GpioButtonMapping button_mappings[] = {
    { BTN_LT1, 7  },

    { BTN_MB3, 8  },
    { BTN_MB1, 9  },
    { BTN_MB2, 10 },

    { BTN_RT1, 11 },
    { BTN_RT2, 12 },
    { BTN_RT3, 13 },
    { BTN_RT4, 14 },
    { BTN_RT5, 15 },

    { BTN_RF8, 16 },
    { BTN_RF4, 17 },
    { BTN_RF7, 18 },
    { BTN_RF3, 19 },
    { BTN_RF6, 20 },
    { BTN_RF2, 21 },
    { BTN_RF5, 22 },
    { BTN_RF1, 26 },
};
const size_t button_count = sizeof(button_mappings) / sizeof(GpioButtonMapping);

DebouncedGpioButtonInput<button_count> gpio_input(button_mappings);
DebouncedSwitchMatrixInput<num_rows, num_cols> matrix_input(row_pins, col_pins, matrix, diode_direction);

const Pinout pinout = {
    .joybus_data = 28,
    .nes_data = -1,
    .nes_clock = -1,
    .nes_latch = -1,
    .mux = -1,
    .nunchuk_detect = -1,
    .nunchuk_sda = -1,
    .nunchuk_scl = -1,
};

CommunicationBackend **backends = nullptr;
size_t backend_count;
KeyboardMode *current_kb_mode = nullptr;

void setup() {
    static InputState inputs;

    // Create GPIO input source and use it to read button states for checking button holds.
    matrix_input.UpdateInputs(inputs);
    gpio_input.UpdateInputs(inputs);

    // Check bootsel button hold as early as possible for safety.
    if (inputs.rt2) {
        reboot_bootloader();
    }

    // Turn on LED to indicate firmware booted.
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);

    // Attempt to load config, or write default config to flash if failed to load config.
    if (!persistence.LoadConfig(config)) {
        persistence.SaveConfig(config);
    }

    // Create array of input sources to be used.
    static InputSource *input_sources[] = {};
    size_t input_source_count = sizeof(input_sources) / sizeof(InputSource *);

    backend_count =
        initialize_backends(backends, inputs, input_sources, input_source_count, config, pinout);

    setup_mode_activation_bindings(config.game_mode_configs, config.game_mode_configs_count);
}

void loop() {
    select_mode(backends, backend_count, config);

    for (size_t i = 0; i < backend_count; i++) {
        backends[i]->SendReport();
    }

    if (current_kb_mode != nullptr) {
        current_kb_mode->SendReport(backends[0]->GetInputs());
    }
}

/* Button inputs are read from the second core */

void setup1() {
    while (backends == nullptr) {
        tight_loop_contents();
    }
}

void loop1() {
    if (backends != nullptr) {
        matrix_input.UpdateInputs(backends[0]->GetInputs());
        gpio_input.UpdateInputs(backends[0]->GetInputs());
    }
}
