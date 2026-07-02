# ESP32S3 LCD OBD-II Display
This project is developed for ESP32S3 to display OBD-II data on a 2.8 inch RGB LCD panel.
It uses the LVGL library to create a user interface that shows the engine RPM.

Communication with the vehicle's ECU is done using a Bluetooth Low Energy (BLE) OBD-II adapter compatible with the ELM327 protocol.

> A datasheet for the ELM327 can be found here: https://cdn.sparkfun.com/assets/learn_tutorials/8/3/ELM327DS.pdf  
> KWP 2000 protocol documentation can be found here: https://www.internetsomething.com/kwp/KWP2000%20ISO%2014230-3.pdf  
> K-line communication: https://www.internetsomething.com/kwp/KWP2000%20ISO%2014230-2%20KLine%20.pdf  
> List of standard OBD-II PIDs: https://en.wikipedia.org/wiki/OBD-II_PIDs


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


## Behavior
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


# ECU Communication
To communicate with the ECU, the ELM327 commands are used to set up the connection and request data.
The ELM327 adapter is connected to the ESP32S3 via Bluetooth Low Energy (BLE) using the NimBLE library.

It connects specifically to a BLE device advertising itself as ``IOS-Vlink`` with the service UUID `000018f0-0000-1000-8000-00805f9b34fb`.
After connecting, the ESP32S3 subscribes to the characteristic with UUID `00002af0-0000-1000-8000-00805f9b34fb` to receive data from the ELM327 adapter.
To send commands to the ELM327, the ESP32S3 writes to the characteristic with UUID `00002af1-0000-1000-8000-00805f9b34fb`.


## Initialization Sequence
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


## Requesting ECU Data
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


# Implementation and Architecture
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
LVGL is configured to update the display every 30ms.

Every command has a timeout of 2 seconds, after which the read attempt is aborted and the associated UI widget is not updated until a new value can be successfully obtained.
The only exception is the `ATSI` command, which has a timeout of 10 seconds.


## Details
The code is contained in the `main` folder.
The `main.cpp` file contains the `app_main()` function, which initializes the hardware, the LVGL library, and the BLE, OBD2, and UI managers.
It also contains the main loop that ticks the LVGL library and handles the UI updates.

The application code is stored in the `Application` subdirectory, which contains the `UIManager`, `BleManager`, and `Obd2Manager` classes.
The interfaces are defined in the `Interfaces.hpp` file.
In the `UIParsers` subdirectory, the UI widgets and the specific OBD2 PID response parsers are implemented.

The only actually used UI widget is the `UIParsers::RPM` class, which implements the `IObdPid` interface and handles the engine RPM gauge widget.
It contains the specific ELM327 command to request the engine RPM from the ECU and a function to parse the ECU's response.
It also contains the `updateUI()` method to update the gauge widget with the latest RPM value received from the ECU.
The pointer to the gauge widget is passed to the `UIParsers::RPM` class during initialization and is owned by the `UIManager`.

The hardware initialization code is also contained in the `main/` directory, structured into subdirectories for individual hardware components.
Most of the hardware is however unused, as the project only presents a single RPM gauge widget on the LCD.
Only the following components are initialized:
 - NVS Flash - supposedly used by the Bluetooth subsystem
 - I2C Bus
 - Touch panel interface (initialized but never actually used)
 - TCA9554PWR GPIO expander
 - LCD Interface
 - LVGL Library

The accelerometer, SD card interface, RTC, and the ADC are not initialized.

The project uses FreeRTOS tasks to achieve multitasking and parallelism.
Three tasks are utilized:
 - a OBD polling task, which periodically polls every registered PID for new data
 - a UI task, which receives the updated `IObdPid` instances and draws the UI using LVGL
 - an internal NimBLE task created to handle the BLE communication

----

The LVGL library is a part of the repository and is located in the `components/lvgl__lvgl` folder.
The specific version used is `v8.2.0`. Documentation for this version can be found at: https://lvgl.io/docs/open/8.2/

ESP-IDF version `5.5.2` is used for this project, and the documentation can be found at: https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32/  
A modified version of the FreeRTOS kernel is shipped with the framework. The NimBLE library is also shipped as part of the framework.

The used SDK configuration is derived from the board manufacturer's demo modified to use the NimBLE library for BLE communication and to enable C++ exceptions.

----

### `BleManager`
The `BleManager` class implements the `IBluetooth` interface and contains code to initialize the NimBLE library and to start the BLE processing task.
It starts the scan for the OBD-II adapter, registers for notification on its RX characteristic, and handles the main Bluetooth event loop.
When data is received on the registered characteristic, it is passed to the provided notification callback.

To actually start the scanning and connection process, the `startScanAndConnect()` method must be called.


### `Obd2Manager`
The `Obd2Manager` class implements the `IObd2` interface and contains code to initialize the OBD adapter, connect to the ECU's bus and send commands and process responses.
It requires a `IBluetooth` instance to actually transfer data.
It registers for notifications of data reception, and contains code to buffer the OBD adapter's response data until a full response is received.
It is then cleaned - stripped of whitespace and the prompt character '>' - and returned as a response to a submitted command.
The command submission is strictly synchronous and managed by the `sendCommand()` method on the interface.


### `UIManager`
The `UIManager` class requires a `IObd2` interface.
It contains code to register PIDs for polling and to process UI updates.
It is also responsible for creating the UI layout and owns the LVGL objects.
It also owns the FreeRTOS polling task along with a FreeRTOS queue to pass UI update notifications to the UI thread.
As previously mentions, pointers to the registered `IObdPid` instances are sent over the queue.

The `processUIUpdates()` method must be periodically called from the UI thread to pull the pointers from the queue and call the update methods.
To start the polling, `startPollingTask()` must be called after the communication is set up and the ELM327 adapter initialized.
If the adapter is not connected yet, the `UIManager` waits for it to connect and handles the initialization automatically.
Likewise, reconnect attempts are handled automatically in the polling task.

The `pollRegisteredPids()` method, called automatically by the polling task, sends the PID's command, receives its response, and processes it.
It parses the hex string into bytes, checks for the success code, and passes the data to the PID's parser.
The response header with a success code and the repeated command are not provided to the parser.


### `UIParsers::RPM`
The `UIParsers::RPM` class implements the `IObdPid` interface.
It manages the LVGL meter widget with a needle indicator to display the engine RPM.
The parsing method simply converts from the big-endian 16-bit integer to a `int32_t`, which is then directly passed to LVGL.


### `MockObdDataProvider`
The `MockObdDataProvider` implements the `IObd2` interface.
It exists primarily for local testing of the UI without the need to connect it to the vehicle.
It serves static data in response to recognized commands.


### LVGL
LVGL (Light & Versatile Graphics Library) is a graphics library that provides a retained mode graphics API for embedded systems.
The library is used to create the user interface and manage the display on the LCD panel.
It provides a wide range of UI widgets and automatically handles the user's input, although this feature is not used in this project.


### FreeRTOS
The ESP-IDF FreeRTOS kernel is a lightweight real-time operating system that provides multitasking and inter-task communication.
As per Espressif's documentation, the FreeRTOS kernel is a modified version of the original FreeRTOS kernel, with additional support for multicore processors.
FreeRTOS is used to create parallel tasks for the application and BLE communication.


# Extending the functionality

## Registering new PIDs
To register new OBD PIDs and display them, several steps are necessary.
First, the `UIManager::createLayout()` method must be updated to create new widgets for the new parameters.
Additional fields must be added to the class to hold the windgets.

Then `UIManager::initPidTracking()` must be updated to register the new PID parser object into the polling loop.
The list is internally represented as a `std::vector<std::unique_ptr<IObdPid>>`.

A simple label object is provided, `UIParsers::SimpleLabel`, which displays the parsed value in an LVGL Label object (created with `lv_label_create()`).
It requires the full command string to request the parameter (e.g. `21301` for the RPM), the LVGL label instance to display to, and a function that processes the received response into a display string.
The string is then automatically set to the label in the UI update method.

If implementing custom widgets, a new derived class from the `IObdPid` interface should be created to handle the particular LVGL object.
Synchronization should be taken into account, as the `parse()` and `updateUI()` methods may be called in parallel from different FreeRTOS tasks.

Currently, the polling and processing code in the `UIManager` is hardcoded to check for `0x61` as the success code.
This corresponds to `0x21 | 0x40` - the actual ELM327 command with a success bit set.
If requesting different ELM327 services, this should be updated to check for the success status more rigorously.


## Different ECU protocols
To update the project to use a different ECU protocol, first the ELM327 initialization sequence must be changes.
The sequence is hardcoded in the `Obd2Manager::initAdapter()` as a sequence of commands sent to the adapter.

Then, the commands to poll the engine parameters must be changed.
The command string to read the RPM is hardcoded in the constructor for the `UIParsers::RPM` class.

As previously mentioned, the `UIManager::pollRegisteredPids()` method should be updated as well to handle the success response for the new commands.
For standard OBD-II, which polls for RPM with `01 0C`, the response success code would be `0x41` - again `0x01 | 0x40`.

The parser code would also likely need changing, as standard OBD-II uses a 14.2 fixed-point representation for the RPM, whereas the Fiat Punto uses a 16-bit integer.


## Different communication interfaces
To change the communication interface with the ELM327 adapter, creating a new derived class of `IBluetooth` is necessary.
The actual interface only dictates methods to send data to the adapter, register for a callback for received data and a disconnect event, and connection status getter.

Although the project was primarily intended for wireless BLE communication, the interface is versatile enough to support a wide range of underlying communication channels.
The constructor for such an object should then set up the hardware interface, and the interface methods should then simply manage data transfer.
The communication is inherently asynchronous and response data should be provided by calling the registered callback.

The development board contains various hardware interfaces, including UART, USB (providing a virtual UART) or Wi-Fi.


# Hardware
Developed specifically for the WaveShare 2.8" RGB LCD development board:
https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.8C

The project is based on the manufacturer's demo code, which can be found in the following link:
https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.8C/ESP32-S3-Touch-LCD-2.8C-Demo.zip

The specific ELM327 BLE adapter used for testing: "Vgate iCar Pro Bluetooth 4.0".

1999/2000 Fiat Punto 1.2 16v

Bosch Motronic ME7.3H4 ECU

----

The development board contains a lot of interesting peripherals:
 - QMI8658 - 6-axis IMU with a gyroscope and an accelerometer
 - TCA9554PWR - GPIO expander to interface with other peripherals
 - SD card slot
 - USB to UART converter
 - Battery management
 - PCF85063 - RTC clock
 - 8 MB PSRAM
 - LCD capacitive touch interface
 - 480x480 2.8" RGB LCD panel
 - ESP32-S3R8 - Dual-core Xtensa LX7 microcontroller clocked at 240 MHz, integrated 2.4 GHz Wi-Fi and Bluetooth 5 (BLE)

Most of these are left unused by this project.

The board supports hardware-level debugging via JTAG (`openocd`).
It supports both the ESP-IDF and Arduino frameworks.
As the ESP-IDF allows for finer control over the hardware, it was chosen for this project.
<!-- also it's just better... -->


# Further development

## Decouple OBD PIDs and UI
The current implementation has the OBD PIDs and the UI tightly coupled.
The `UIManager` class owns the LVGL objects and the `IObdPid` instances, and the `IObdPid` instances are responsible for both updating the UI and processing the OBD data.
This makes it rather difficult to change the UI or add new PIDs without rather extensive modifications to the UI/OBD code (contained in the `UIParsers` namespace).

Utilizing C++ features such as templates and polymorphism, the OBD PIDs and the UI could be decoupled, perhaps into a templated parsing interface and a UI interface.


## Decouple PID storage and polling order
The current implementation stores the `IObdPid` instances in a `std::vector<std::unique_ptr<IObdPid>>` and polls them in the order they are stored.
This could be decoupled, allowing for different polling orders or different polling frequencies for different PIDs.

Ideally the order of the PID polling should be configurable, perhaps even by a configuration file stored on the SD card.


## More PIDs and standard OBD-II support
The current implementation only supports the engine RPM PID for the Fiat Punto 1.2 16v.
Adding support for more PIDs would require reverse engineering the ECU's response data or existing closed-source software to discover additional identifiers.
Once the PIDs are known, they can be added to the project by creating new `IObdPid` instances and registering them with the `UIManager`.

Another feature that could be added is support for standard OBD-II PIDs, which would allow the project to work with a wider range of vehicles.
This would likely require extensive modifications to the overall architecture, as the current implementation is tightly coupled to a single communication protocol and dynamically selecting different protocols is not easily achievable.
A configuration file could then be used to select the protocol and the PIDs to poll.

## More advanced UI
The current implementation only displays the engine RPM on a gauge widget.
The UI could be enhanced to display more information or take into account the user's touch input.

The current UI simply displays the latest measured values on the screen, but it could be enhanced to include smoothing of the values with animations to move the gauge needle more smoothly.


## Data logging
As the development board contains an SD card slot, data logging could be implemented to store the measured values on the SD card for later analysis.
Debug logs could also be stored in the case of an error with the communication.


## IMU widget
The development board contains a QMI8658 6-axis IMU.
As it is intended to be used in a vehicle, it could be used to measure the vehicle's acceleration and display it on the LCD.

Both lateral and forward acceleration could be measured, accurately conveying the vehicle's dynamics to the driver.
