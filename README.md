# ESP32S3 LCD OBD-II Display
This project is developed for ESP32S3 to display OBD-II data on a 2.8 inch RGB LCD panel.
It uses the LVGL library to create a user interface that shows the engine RPM.

Communication with the vehicle's ECU is done using a Bluetooth Low Energy (BLE) OBD-II adapter compatible with the ELM327 protocol.

> A datasheet for the ELM327 can be found here: https://cdn.sparkfun.com/assets/learn_tutorials/8/3/ELM327DS.pdf  
> KWP 2000 protocol documentation can be found here: https://www.internetsomething.com/kwp/KWP2000%20ISO%2014230-3.pdf  
> K-line communication: https://www.internetsomething.com/kwp/KWP2000%20ISO%2014230-2%20KLine%20.pdf


## Features
The only feature implemented in this project is the display of engine RPM.
The RPM is displayed on a gauge widget (LVGL Meter).

It is not compatible with standard OBD-II PIDs, as it targets specifically a **1999/2000 Fiat Punto 1.2 16v** with the **Bosch Motronic ME7.3H4** ECU.

As the vehicle predates OBD-II standardization in Europe, the ECU communicates using a proprietary protocol.
The ECU is accessed via the KWP 2000 protocol over a K-line interface.
The ECU address is `0x10`, and the header for requests is `81 10 F1`.
These value are, of course, very non-standard.

> [!WARNING]
> The project is NOT compatible with modern vehicles that use the OBD-II standard.


### Behavior
When the ESP32S3 is powered on, it initializes the LCD and connects to the ELM327 adapter via BLE.
Once connected, it sends the initialization commands to the ELM327 adapter to set up the connection with the ECU.
After the connection is established, the ESP32S3 periodically sends a request to read the engine RPM from the ECU.
The ECU responds with the engine RPM, which is then displayed on the LCD.

Until the BLE connection is established, the LCD shows nothing.
The application waits indefinitely for the connection to be established.
Once connected, the UI is initialized and periodically updated.
If the BLE connection is lost, the application waits for the connection to be re-established.
Likewise, when the ECU does not respond to the initialization, the UI shows a "CRITICAL ERROR" message and stops updating until a hardware reset is performed.
This usually happens when the engine is not running, as the ECU bus initialization then fails.

Logging is done via the ESP32S3's USB serial port, which can be viewed using the `idf.py monitor` command.


## ECU Communication
To communicate with the ECU, the ELM327 commands are used to set up the connection and request data.
The ELM327 adapter is connected to the ESP32S3 via Bluetooth Low Energy (BLE) using the NimBLE library.

It connects specifically to a BLE device advertising itself as ``IOS-Vlink`` with the service UUID `000018f0-0000-1000-8000-00805f9b34fb`.
After connecting, the ESP32S3 subscribes to the characteristic with UUID `00002af0-0000-1000-8000-00805f9b34fb` to receive data from the ELM327 adapter.
To send commands to the ELM327, the ESP32S3 writes to the characteristic with UUID `00002af1-0000-1000-8000-00805f9b34fb`.


### Initialization Sequence
Standard ELM327 commands are used to initialize the connection with the ECU.

```
ATZ            - Reset
ATE0           - Disable echo
ATSP4          - Set protocol to ISO 14230-4 KWP (KWP 2000)
ATIIA 10       - Set initialization address to 10
ATKW0          - Disable keyword checking
ATSH 81 10 F1  - Set header to 81 10 F1 (Fiat Proprietary)
ATSI           - ISO Slow Init
```

After the `ATSI` command, the ELM327 adapter is ready to send requests to the ECU and receive responses.


### Requesting ECU Data
To request data from the ECU, the ELM327 command `21` is used, which corresponds to the "Read Data by Local Identifier" service in the KWP 2000 protocol.
The specific local identifiers are manufacturer-specific and largely publicly unknown.
The only known local identifier for the Fiat Punto 1.2 16v is `0x30`, which corresponds to the engine RPM.
The request command to read the engine RPM is therefore `21301` - mode `21`, PID `30`, wait for exactly 1 response.

The ECU responds with, for example, `61 30 02 AE`, where `61` indicates a successful response to a mode `21` request, `30` is the requested PID, and `02 AE` is the engine RPM - 686 (engine idle).
Every returned value is in the big endian format.

Unfortunately, no other local identifiers are currently known for this vehicle, and reverse engineering the ECU's response data or existing closed-source software is required to discover additional identifiers.
This is however largely out of scope for this project.

> [!NOTE]
> The engine is however able to report some very interesting data, such as:
> - Engine RPM
> - Engine load
> - Coolant temperature
> - Intake air temperature
> - Throttle pedal position
> - Throttle valve angle
> - Spark advance
> - Fuel injection time
> - Vehicle speed
> - Intake manifold pressure
> - Mass Air Flow (MAF)
> 
> Unfortunately, none of the PIDs (and formulas to parse the ECU data) for these values are known.
> Applications such as AlfaOBD are able to read these values, but they are closed-source and the PIDs are not publicly documented.


## Implementation
The project is implemented using the ESP-IDF framework and the LVGL and NimBLE libraries.
It is written in C++ with an object-oriented approach and exceptions allowed for error handling.

The main components of the project are:
- `BleManager`: Manages the BLE connection to the ELM327 adapter and handles sending and receiving data.
- `Obd2Manager`: Manages the OBD2 communication with the ECU, including sending initialization commands and requesting data.
- `UIManager`: Manages the user interface and periodically requests data from the `Obd2Manager` to update the display.

A separate task is created for the OBD polling and the main task becomes the UI task, which is responsible for updating the display.

The RPM gauge is implemented as a separate class handling the LVGL Meter widget and updating it with the latest RPM value received from the ECU.
It also contains the specific ELM327 command to request the engine RPM from the ECU and a function to parse the ECU's response.

To enable testing and a decoupled architecture, the classes implement interfaces:
- `IBluetooth`: Defines the interface for the BLE communication and callback registration for response processing.
- `IObd2`: Defines the interface for the OBD2 communication to initialize the ELM327 adapter and request data from the ECU.
- `IObdPid`: Defines the interface for the OBD2 PIDs - the specific command to request data, the parsing of the response, and the update of the UI.

UI update messages are sent from the OBD polling task to the UI task using a FreeRTOS queue.
A pointer to the `IObdPid` interface instance is sent to the UI task, which then calls the `updateUI()` method.

The update task polls the `IObd2` interface for new data every 500ms, and the UI task ticks the LVGL library every 10ms to update the display.
After every command sent to the ELM327 adapter, a 100ms delay is added.

Every command has a timeout of 2 seconds, after which the read attempt is aborted and the associated UI widget is not updated until a new value can be successfully obtained.
The only exception is the `ATSI` command, which has a timeout of 10 seconds.


## Hardware
Developed specifically for the WaveShare 2.8" RGB LCD development board:
https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.8C

The project is based on the manufacturer's demo code, which can be found in the following link:
https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.8C/ESP32-S3-Touch-LCD-2.8C-Demo.zip

The specific ELM327 BLE adapter used for testing: "Vgate iCar Pro Bluetooth 4.0".

1999/2000 Fiat Punto 1.2 16v

Bosch Motronic ME7.3H4 ECU
