# ESPHome Rain Director

Monitor and track water usage from a Rain Director rainwater tank system.

## Features

- Real-time tank level monitoring
- Track rainwater vs mains water consumption
- Tank volume calculation
- Water source tracking
- Consumption counters with reset capability
- WiFi connectivity with fallback AP
- Home Assistant integration

## Hardware Requirements

- ESP32 development board
- Rain Director controller with UART interface
- Connections:
  - RX: GPIO16
  - TX: GPIO17
  - Baud rate: 9600

## Installation

### Option 1: ESPHome Dashboard (Recommended)

1. Click the button below to install directly to your ESP32:

   [![Open your Home Assistant instance and show the add ESPHome device dialog.](https://my.home-assistant.io/badges/config_flow_start.svg)](https://my.home-assistant.io/redirect/config_flow_start/?domain=esphome)

2. Copy this import URL:
   ```
   github://pturner1989/esphome-rain-director/rain-director.yaml@main
   ```

3. Paste the URL in the ESPHome dashboard import dialog

### Option 2: Manual Installation

1. Clone this repository:
   ```bash
   git clone https://github.com/pturner1989/esphome-rain-director.git
   ```
2. Adjust the `tank_capacity` substitution to match your tank size (in liters)
3. Compile and upload using ESPHome

## Configuration

### Tank Capacity

Edit the substitution in the YAML file to match your tank size:

```yaml
substitutions:
  tank_capacity: "80.0"  # Change to your tank capacity in liters
```

### Custom Component

This project uses a custom `rain_director` component located in the `components/` directory. Make sure this directory is included when deploying.

## Sensors

- **Tank Level** - Tank fill percentage (0-100%)
- **Tank Volume** - Current water volume in liters
- **Rainwater Used** - Total rainwater consumption
- **Mains Used** - Total mains water consumption
- **Mode** - Current operating mode
- **Status** - Tank status
- **Source** - Current water source (Rainwater/Mains)

## Controls

- **Restart** - Restart the ESP32
- **Reset Consumption Counters** - Reset rainwater and mains usage totals

## License

This project is open source. Feel free to modify and share.

## Contributing

Contributions are welcome! Please open an issue or submit a pull request.
