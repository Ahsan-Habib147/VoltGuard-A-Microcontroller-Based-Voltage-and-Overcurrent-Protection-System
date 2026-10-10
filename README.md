# VoltGuard: A Microcontroller-Based Voltage and Overcurrent Protection System

VoltGuard is an Arduino Uno-based protection system designed to detect abnormal voltage and overcurrent conditions and disconnect a simulated load. The project is implemented and tested using Proteus 8 Professional.

## Features

- Overvoltage protection
- Undervoltage protection
- Overcurrent protection
- Relay-based load disconnection
- 16×2 I2C LCD for voltage, current, and fault status
- Push-button control and reset
- Serial communication with a computer
- Configurable protection limits and recovery delay
- EEPROM-based configuration storage

## Software and Tools

- Proteus 8 Professional
- Arduino IDE
- Arduino C/C++

## Project Structure

```text
VoltGuard/
├── src/
│   └── voltage_protector.ino
├── design_document.pdf
├── schematics/
│   └── schematic.png
├── test_evidence/
│   ├── lcd_display.png
│   ├── normal_operation.png
│   ├── overcurrent_test.png
│   ├── overvoltage_test.png
│   ├── serial_monitor.png
│   └── undervoltage_test.png
├── project_files/
│   ├── required_libraries/
│   │   ├── ARDUINO2.IDX
│   │   └── ARDUINO2.LIB
│   ├── VoltGuard.pdsprj
└── README.md
```

## Installation

### 1. Install Required Proteus Libraries

Copy the following files from `project_files/required_libraries/`:

- `ARDUINO2.LIB`
- `ARDUINO2.IDX`

Paste both files into the following Proteus library directory:

```text
C:\Program Files (x86)\Labcenter Electronics\Proteus 8 Professional\DATA\LIBRARY
```

Restart Proteus after copying the files. Administrator permission may be required.

### 2. Open the Project

1. Open Proteus 8 Professional.
2. Open the Proteus project file from `project_files/`.
3. Check that all required components are available.
4. If the firmware is not already loaded, compile the source code in `src/` using Arduino IDE.
5. Select the generated HEX file in the Arduino Uno component's **Program File** field.
6. Start the simulation.

## Default Protection Settings

| Parameter | Default Value |
|---|---:|
| Overvoltage limit | 280 V |
| Undervoltage limit | 170 V |
| Overcurrent limit | 10 A |
| Trip delay | 120 ms |
| Recovery delay |  10s |
| Serial baud rate | 9600 |

These are the default firmware settings. The values may be changed using the supported controls or serial commands.

## Serial Commands

Use the Proteus Virtual Terminal at 9600 baud.

| Command | Description |
|---|---|
| `GET` | Display configuration |
| `STATUS` | Request system status |
| `STREAM ON` | Enable continuous serial output |
| `STREAM OFF` | Disable continuous serial output |
| `SET OV 280` | Set overvoltage limit |
| `SET UV 170` | Set undervoltage limit |
| `SET OC 10` | Set overcurrent limit |
| `SET DLY 120` | Set trip delay |
| `SET REC 10` | Set recovery delay |
| `RESET` | Request a protection reset |

Replace the example values with the desired settings.

## Simulation and Testing

The simulation is designed to demonstrate:

- **Normal voltage:** The relay remains ON.
- **Overvoltage:** The system displays `TRIP OV` and disconnects the load.
- **Undervoltage:** The system displays `TRIP UV` and disconnects the load.
- **Overcurrent:** The system displays `TRIP OC` and disconnects the load.
- **Voltage recovery:** The relay may reconnect after the configured recovery delay if the fault is cleared and no latched fault prevents reconnection.

Voltage conditions are selected using switches, while a potentiometer provides the simulated current-sensing input.

## Limitations

- The project is designed for Proteus simulation.
- The current-sensing input is simulated and has not been calibrated against a physical current sensor.
- EEPROM persistence across simulation restarts has not been independently verified.
- No physical mains-connected prototype or real-world surge test has been conducted.

## Safety Notice

**Do not connect this project directly to mains electricity.**

This project is intended for educational purposes and simulation. A practical implementation requires appropriate electrical isolation, correctly rated components, and hardware safety testing.

## Author

**Md. Ahsan Habib**

## Contact

For any inquiries or issues, please contact us at ahsan.2008032.ruet.mte@gmail.com.

Thank you for using VoltGuard: A Microcontroller-Based Voltage and Overcurrent Protection System simulation!
