#include "rain_director.h"
#include "esphome/core/log.h"

namespace esphome {
namespace rain_director {

static const char *const TAG = "rain_director";

// =======================================================
// MODE CODE MAPPINGS - Add new discovered codes here
// =======================================================
// This structure maps Rain Director mode/status byte combinations to human-readable strings
// using a composite key lookup system with two-tier priority matching.
//
// COMPOSITE KEY FORMAT:
// { mode_byte, status_byte, mode_string, status_string, source_string, is_refresh_indicator, match_any_status_flag }
//
// Fields:
//   - code: The mode byte sent by the Rain Director display panel (device 10) in hex codes
//   - status_byte: The status byte from JSON messages (only checked if match_any_status=false)
//   - mode: The operational mode (Normal, Holiday, Refresh, Init, Backup)
//   - status: The controller status (Idle, Filling, Draining)
//   - source: The water source (Rainwater, Mains)
//   - is_refresh: True if this code indicates a refresh cycle (used for state tracking)
//   - match_any_status: True = status-agnostic (matches mode byte only), False = status-specific (requires exact status byte match)
//
// MATCHING PRIORITY:
// The lookup algorithm searches this array in order from top to bottom.
// - Status-specific entries (match_any_status=false) should be placed FIRST
// - Status-agnostic entries (match_any_status=true) should be placed AFTER as fallback
// - The FIRST matching entry is used, implementing priority matching
//
// EXAMPLES:
// Status-specific entry (requires exact mode AND status byte match):
//   { 0x40, 0x0F, "Init", "Filling", "Rainwater", false, false }  // Mode 0x40 with Status 0x0F only
//
// Status-agnostic entry (matches mode byte regardless of status byte):
//   { 0x01, 0x00, "Normal", "Idle", "Rainwater", false, true }  // Mode 0x01 with any status (0x00 ignored)
//
// IMPORTANT: Mode 0x40 intentionally has NO status-agnostic fallback. Any status byte other than
// the documented values (0x0F, 0x09) will result in "Unknown" state, allowing detection of
// unexpected protocol variations.
//
// See INIT-BACKUP-MODES-REQ-FN-01: Composite Key Mode Mapping
// See INIT-BACKUP-MODES-REQ-FN-06: Existing Mode Compatibility
// See INITIAL-RELEASE-REQ-FN-04: Maintainable Code Mappings
static const struct {
  uint8_t code;
  uint8_t status_byte;
  const char* mode;
  const char* status;
  const char* source;
  bool is_refresh;
  bool match_any_status;
} MODE_MAPPINGS[] = {
  // STATUS-AGNOSTIC MAPPINGS (match mode byte only, status byte ignored)
  // These come first temporarily - will be reorganized in Phase 2 when status-specific entries are added
  { 0x00, 0x00, "Normal",  "Filling",  "Rainwater", false, true },  // Filling from rainwater (or refresh fill - see is_refresh tracking)
  { 0x01, 0x00, "Normal",  "Idle",     "Rainwater", false, true },  // Normal mode, idle on rainwater
  { 0x04, 0x00, "Normal",  "Idle",     "Mains",     false, true },  // Normal mode, idle on mains selected
  { 0x08, 0x00, "Holiday", "Idle",     "Mains",     false, true },  // Holiday mode, idle
  { 0x0C, 0x00, "Holiday", "Filling",  "Mains",     false, true },  // Holiday mode, filling from mains
  { 0x10, 0x00, "Refresh", "Draining", "Rainwater", true,  true },  // Refresh cycle, draining tank
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

    // Composite key lookup through MODE_MAPPINGS array with two-tier priority matching
    // INIT-BACKUP-MODES-REQ-FN-01: Use both mode byte and status byte for matching
    // Priority: status-specific entries (match_any_status=false) checked first, then status-agnostic entries (match_any_status=true)
    decltype(&MODE_MAPPINGS[0]) mapping = nullptr;
    for (size_t i = 0; i < MODE_MAPPINGS_COUNT; i++) {
      if (MODE_MAPPINGS[i].code == mode_byte) {
        // Check if this entry requires status byte matching
        if (!MODE_MAPPINGS[i].match_any_status) {
          // Status-specific: both mode and status must match
          if (MODE_MAPPINGS[i].status_byte == this->last_status_byte_) {
            mapping = &MODE_MAPPINGS[i];
            break;
          }
          // Continue searching - this mode byte might have a fallback mapping
        } else {
          // Status-agnostic: mode byte match is sufficient (fallback behavior)
          mapping = &MODE_MAPPINGS[i];
          break;
        }
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
    // Store status byte for composite key lookup
    this->last_status_byte_ = static_cast<uint8_t>(state_val);
    this->status_byte_received_ = true;
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
