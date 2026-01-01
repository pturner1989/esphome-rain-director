# ESPHome Rain Director

Monitor and track water usage from a Rain Director rainwater tank system. The Rain Director is provided by a company called Rainwater Harvesting in the UK.

This project interfaces with the Rain Director controller via UART communication to decode operational modes and status codes. The protocol was reverse-engineered by monitoring the serial output, and not all codes have been identified yet. Contributions via PRs or issues are welcome to expand the code mappings.

This project is not affiliated with the manufacturer in any way and takes no responsibility for any issues caused by connecting an ESP32 to the Rain Director.

## Features

- Real-time header tank level monitoring
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

## Communication Protocol

The Rain Director communicates via UART (9600 baud) sending periodic status updates. The custom component parses these messages to extract:

- **Mode codes** - Operational modes like Normal, Holiday, and Refresh
- **State codes** - Controller states like Idle, Filling, and Draining
- **Tank level** - Water level as a percentage
- **Water source** - Whether the system is using rainwater or mains

The mode and state code mappings were determined by monitoring the serial output and correlating with observed behavior. Not all possible codes have been identified. If you discover additional codes, please contribute via:

- Opening an issue with the code number and observed behavior
- Submitting a PR to add new code mappings to the component

## Sensors

- **Tank Level** - Tank fill percentage (0-100%)
- **Tank Volume** - Current water volume in liters
- **Rainwater Used** - Total rainwater consumption
- **Mains Used** - Total mains water consumption
- **Mode** - Current operating mode (Normal, Holiday, Refresh)
- **Status** - Controller status (Idle, Filling, Draining)
- **Source** - Current water source (Rainwater/Mains)

## Controls

- **Restart** - Restart the ESP32
- **Reset Consumption Counters** - Reset rainwater and mains usage totals

## License

This project is MIT-license open source.

## Contributing

Contributions are welcome! Please open an issue or submit a pull request.
