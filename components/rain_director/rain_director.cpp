#include "rain_director.h"
#include "esphome/core/log.h"

namespace esphome {
namespace rain_director {

static const char *const TAG = "rain_director";

// =======================================================
// MODE CODE MAPPINGS - Add new discovered codes here
// =======================================================
// This structure maps Rain Director hex mode codes to human-readable strings.
// Format: { hex_code, mode_string, status_string, source_string, is_refresh_indicator }
//
// Fields:
//   - code: The hex byte sent by the Rain Director display panel (device 10)
//   - mode: The operational mode (Normal, Holiday, Refresh)
//   - status: The controller status (Idle, Filling, Draining)
//   - source: The water source (Rainwater, Mains)
//   - is_refresh: True if this code indicates a refresh cycle (used for state tracking)
//
// Example: To add a new code 0x14 for "Service Mode, Idle, Mains", add:
//   { 0x14, "Service", "Idle", "Mains", false },
//
// See INITIAL-RELEASE-REQ-FN-04: Maintainable Code Mappings
static const struct {
  uint8_t code;
  const char* mode;
  const char* status;
  const char* source;
  bool is_refresh;
} MODE_MAPPINGS[] = {
  { 0x00, "Normal",  "Filling",  "Rainwater", false },  // Filling from rainwater (or refresh fill - see is_refresh tracking)
  { 0x01, "Normal",  "Idle",     "Rainwater", false },  // Normal mode, idle on rainwater
  { 0x04, "Normal",  "Idle",     "Mains",     false },  // Normal mode, idle on mains selected
  { 0x08, "Holiday", "Idle",     "Mains",     false },  // Holiday mode, idle
  { 0x0C, "Holiday", "Filling",  "Mains",     false },  // Holiday mode, filling from mains
  { 0x10, "Refresh", "Draining", "Rainwater", true  },  // Refresh cycle, draining tank
};
static const size_t MODE_MAPPINGS_COUNT = sizeof(MODE_MAPPINGS) / sizeof(MODE_MAPPINGS[0]);

void RainDirectorComponent::setup() {
  ESP_LOGI(TAG, "Rain Director Tank Sensor initialized");
}

void RainDirectorComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Rain Director:");
  LOG_SENSOR("  ", "Tank Level", this->tank_level_sensor_);
  LOG_SENSOR("  ", "Mode Code", this->mode_code_sensor_);
  LOG_SENSOR("  ", "State Code", this->state_code_sensor_);
  LOG_TEXT_SENSOR("  ", "Mode", this->mode_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Status", this->status_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Source", this->source_text_sensor_);
}

void RainDirectorComponent::loop() {
  while (this->available()) {
    char c = this->read();
    
    if (c == '\r' || c == '\n') {
      // End of line - process buffer
      if (!this->buffer_.empty()) {
        this->process_buffer_();
        this->buffer_.clear();
      }
    } else if (c == '<') {
      // Start of a new code - process any pending buffer first
      if (!this->buffer_.empty()) {
        this->process_buffer_();
        this->buffer_.clear();
      }
      this->buffer_ = "<";
    } else {
      // Add character to buffer
      this->buffer_ += c;
      
      // Check for JSON (doesn't have line terminator)
      size_t json_start = this->buffer_.find("{\"tanklevels\"");
      if (json_start != std::string::npos) {
        size_t json_end = this->buffer_.find("}}", json_start);
        if (json_end != std::string::npos) {
          this->process_json_(this->buffer_.substr(json_start, json_end - json_start + 2));
          this->buffer_.clear();
        }
      }
      
      // Prevent overflow
      if (this->buffer_.length() > 500) {
        this->buffer_.clear();
      }
    }
  }
}

void RainDirectorComponent::process_buffer_() {
  if (this->buffer_.empty()) return;

  // Handle hex codes starting with <
  if (this->buffer_[0] == '<') {
    this->process_hex_code_(this->buffer_);
    return;
  }

  // Handle JSON data
  size_t json_start = this->buffer_.find("{\"tanklevels\"");
  if (json_start != std::string::npos) {
    size_t json_end = this->buffer_.find("}}", json_start);
    if (json_end != std::string::npos) {
      this->process_json_(this->buffer_.substr(json_start, json_end - json_start + 2));
    }
  }
}

void RainDirectorComponent::process_hex_code_(const std::string &code) {
  // Remove < prefix for processing
  std::string hex = code.substr(1);
  
  // =======================================================
  // DEVICE 2: LEVEL SENSOR
  // =======================================================
  
  // Level data: <2053[LEVEL][80][CHECKSUM]
  if (hex.substr(0, 4) == "2053" && hex.length() >= 10) {
    std::string level_hex = hex.substr(4, 2);
    int level = this->hex_to_int_(level_hex);
    
    if (level >= 0 && level != this->last_top_) {
      this->last_top_ = level;
      if (this->tank_level_sensor_ != nullptr)
        this->tank_level_sensor_->publish_state(level);
      ESP_LOGI(TAG, "Level: %d%%", level);
    }
    return;
  }
  
  // Ignore heartbeats and version queries
  if (hex.substr(0, 5) == "20123" || hex.substr(0, 4) == "2071" || hex.substr(0, 4) == "2010") {
    return;
  }
  if (hex.substr(0, 5) == "30123") {
    return;
  }
  if (hex.substr(0, 5) == "40123" || hex.substr(0, 4) == "4071" || hex.substr(0, 4) == "4010" || hex.substr(0, 4) == "4033") {
    return;
  }
  
  // =======================================================
  // DEVICE 10: DISPLAY PANEL
  // =======================================================

  // Display commands: <1053[MODE_BYTE][SUB][CHECKSUM]
  if (hex.substr(0, 4) == "1053" && hex.length() >= 10) {
    std::string mode_hex = hex.substr(4, 2);
    int mode_byte = this->hex_to_int_(mode_hex);

    // Linear search through MODE_MAPPINGS array to find matching code
    decltype(&MODE_MAPPINGS[0]) mapping = nullptr;
    for (size_t i = 0; i < MODE_MAPPINGS_COUNT; i++) {
      if (MODE_MAPPINGS[i].code == mode_byte) {
        mapping = &MODE_MAPPINGS[i];
        break;
      }
    }

    // Determine mode, status, and source strings
    std::string mode_str;
    std::string status_str;
    std::string source_str;

    if (mapping == nullptr) {
      // Unknown code - log warning and publish "Unknown" for all text sensors
      ESP_LOGW(TAG, "Unknown mode code: 0x%02X", mode_byte);
      mode_str = "Unknown";
      status_str = "Unknown";
      source_str = "Unknown";
    } else {
      // Track refresh cycle using is_refresh field
      if (mapping->is_refresh) {
        this->in_refresh_ = true;
      } else if (mode_byte == 0x01 || mode_byte == 0x04 || mode_byte == 0x08) {
        // Back to an idle state = refresh complete
        this->in_refresh_ = false;
      }

      // Special case: mode 0x00 after 0x10 is a refresh fill (not normal fill)
      if (this->in_refresh_ && mode_byte == 0x00) {
        mode_str = "Refresh";
      } else {
        mode_str = mapping->mode;
      }

      status_str = mapping->status;
      source_str = mapping->source;
    }

    // Publish mode code (raw byte for diagnostics)
    if (mode_byte != this->last_mode_) {
      this->last_mode_ = mode_byte;
      if (this->mode_code_sensor_ != nullptr)
        this->mode_code_sensor_->publish_state(mode_byte);
    }

    // Publish mode
    if (mode_str != this->last_mode_str_) {
      this->last_mode_str_ = mode_str;
      if (this->mode_text_sensor_ != nullptr)
        this->mode_text_sensor_->publish_state(mode_str);
      ESP_LOGI(TAG, "Mode: %s (0x%02X)", mode_str.c_str(), mode_byte);
    }

    // Publish status
    if (status_str != this->last_status_) {
      this->last_status_ = status_str;
      if (this->status_text_sensor_ != nullptr)
        this->status_text_sensor_->publish_state(status_str);
      ESP_LOGI(TAG, "Status: %s", status_str.c_str());
    }

    // Publish source
    if (source_str != this->last_source_) {
      this->last_source_ = source_str;
      if (this->source_text_sensor_ != nullptr)
        this->source_text_sensor_->publish_state(source_str);
      ESP_LOGI(TAG, "Source: %s", source_str.c_str());
    }

    return;
  }
}

void RainDirectorComponent::process_json_(const std::string &json) {
  int top_val = this->extract_json_int_(json, "top");
  int state_val = this->extract_json_int_(json, "state");

  if (top_val >= 0 && top_val != this->last_top_) {
    this->last_top_ = top_val;
    if (this->tank_level_sensor_ != nullptr)
      this->tank_level_sensor_->publish_state(top_val);
    ESP_LOGI(TAG, "Level: %d%%", top_val);
  }
  if (state_val >= 0 && state_val != this->last_state_) {
    this->last_state_ = state_val;
    if (this->state_code_sensor_ != nullptr)
      this->state_code_sensor_->publish_state(state_val);
    ESP_LOGI(TAG, "State: %d", state_val);
  }
}

int RainDirectorComponent::extract_json_int_(const std::string &json, const std::string &key) {
  std::string search = "\"" + key + "\":\"";
  size_t start = json.find(search);
  if (start == std::string::npos) return -1;
  
  start += search.length();
  size_t end = json.find("\"", start);
  if (end == std::string::npos) return -1;
  
  return atoi(json.substr(start, end - start).c_str());
}

int RainDirectorComponent::hex_to_int_(const std::string &hex) {
  return (int)strtol(hex.c_str(), nullptr, 16);
}

}  // namespace rain_director
}  // namespace esphome
