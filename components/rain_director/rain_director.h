#pragma once

#include <cmath>

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace rain_director {

class RainDirectorComponent : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_tank_level_sensor(sensor::Sensor *sensor) { tank_level_sensor_ = sensor; }
  void set_outside_tank_level_sensor(sensor::Sensor *sensor) { outside_tank_level_sensor_ = sensor; }
  void set_mode_code_sensor(sensor::Sensor *sensor) { mode_code_sensor_ = sensor; }
  void set_state_code_sensor(sensor::Sensor *sensor) { state_code_sensor_ = sensor; }
  void set_mode_text_sensor(text_sensor::TextSensor *sensor) { mode_text_sensor_ = sensor; }
  void set_status_text_sensor(text_sensor::TextSensor *sensor) { status_text_sensor_ = sensor; }
  void set_source_text_sensor(text_sensor::TextSensor *sensor) { source_text_sensor_ = sensor; }

  // Address 3 is polled every cycle and nothing answers it. These answer
  // that poll: with keypad bits (0x01 rainwater, 0x02 mains, 0x04 holiday,
  // 0x08 recycle) or with an outside tank level in percent.
  void press_keys(uint8_t keys);
  void set_outside_level(float level) { outside_level_ = level; }
  void set_outside_level_enabled(bool enabled) { outside_level_enabled_ = enabled; }

 protected:
  void process_buffer_();
  void process_hex_code_(const std::string &code);
  void process_json_(const std::string &json);
  int extract_json_int_(const std::string &json, const std::string &key);
  int hex_to_int_(const std::string &hex);
  void answer_address_3_poll_();
  void write_frame_(const std::string &body);

  sensor::Sensor *tank_level_sensor_{nullptr};
  sensor::Sensor *outside_tank_level_sensor_{nullptr};
  sensor::Sensor *mode_code_sensor_{nullptr};
  sensor::Sensor *state_code_sensor_{nullptr};
  text_sensor::TextSensor *mode_text_sensor_{nullptr};
  text_sensor::TextSensor *status_text_sensor_{nullptr};
  text_sensor::TextSensor *source_text_sensor_{nullptr};

  std::string buffer_;
  int last_top_{-1};
  int last_bottom_{-1};
  int last_mode_{-1};
  int last_state_{-1};
  std::string last_status_;
  std::string last_source_;
  std::string last_mode_str_;
  bool in_refresh_{false};

  uint8_t pending_keys_{0};
  // Polls left to answer for a press: the keys twice, then a release,
  // as the front panel keypad does.
  uint8_t key_polls_left_{0};
  float outside_level_{NAN};
  bool outside_level_enabled_{false};
  int last_outside_sent_{-1};
};

}  // namespace rain_director
}  // namespace esphome
