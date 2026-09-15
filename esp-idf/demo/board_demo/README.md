[English](README.md) | [简体中文](README.zh-CN.md)

# ESP32-S31-PI Comprehensive Demo Example (Board Demo)

This example is a comprehensive functional demonstration for the ESP32-S31-PI development board, showcasing its main peripherals, interfaces, and core capabilities.

## Features

This project includes demonstrations for the following modules:

*   **LED Blinking**: Periodic blinking of a standard GPIO LED or an addressable RGB LED (WS2812).
*   **Button Input**: Short press and long press detection with interrupt-based debouncing.
*   **Ethernet**: LAN8720 PHY connected via RMII, supporting DHCP for automatic IP acquisition.
*   **LVGL Display and Touch**: Driving an RGB LCD screen with GT911 capacitive touch, featuring single-touch, multi-touch, and gesture recognition (swipe, rotate, pinch) demos.
*   **SD Card Read/Write**: Mounting a FAT file system via SDMMC, demonstrating file creation, reading, renaming, and speed tests.
*   **USB Host (USB MSC)**: Hot-plugging USB flash drives, auto-mounting the file system, and demonstrating read/write and speed tests.

## Hardware Requirements

*   ESP32-S31-PI development board
*   A USB data cable (for power supply and flashing)
*   An Ethernet cable (for Ethernet testing)
*   A microSD card (for SD card testing)
*   A USB flash drive (for USB MSC testing)


## Build and Flash

Please ensure that the ESP-IDF environment has been configured according to the guide in the `esp-idf` folder.

1. **Set the target chip**:
    Open a terminal in the project root directory and run:
        idf.py set-target esp32s31

2. **Configure project parameters**:
    You need to configure the GPIO pins for each module (especially the onboard LED, buttons, LCD, and Ethernet) via `menuconfig`:
        idf.py menuconfig
    In the menu, find `Example Configuration` and configure the pins and options for each peripheral according to your actual hardware connections.

3. **Build, flash, and monitor**:
    Connect the development board and run:
        idf.py -p (YOUR_PORT) flash monitor
    *(For example: `idf.py -p COM7 flash monitor`)*

## Expected Behavior and Operation Guide

*   **LED**: After the system boots, the onboard LED will blink according to the period set by `CONFIG_BLINK_PERIOD`. (If an RGB LED is configured, it will alternate between red, green, and blue.)
*   **Button**: Press the BOOT button, and the serial terminal will print `Button pressed!`. A long press will print `Long press detected!`, and the duration will be displayed upon release.
*   **Ethernet**: After plugging in the Ethernet cable, the serial terminal will print `ETH Link Up` and display the obtained IP address. If the connection fails, the program will attempt to reconnect automatically.
*   **LVGL Screen**: The screen will display the UI. You can perform single-touch, multi-touch, or gesture operations (such as two-finger swipe, rotate, pinch) on the touch screen. The star icon on the UI will respond with physical spring and inertia sliding feedback.
*   **SD Card**: After inserting the SD card, the program will automatically mount it, print card information, and perform operations such as creating `hello.txt`, renaming files, and read/write speed tests.
*   **USB MSC**: After inserting a USB flash drive into the USB Type-A port on the development board, the serial terminal will print the drive's capacity and device information, and automatically run file read/write and 1MB speed tests.


## Notes

*   **USB MSC Example**: This example relies on the GPIO ISR service installed by the `button_input_example` component. Do not install the ISR service again in the USB example to avoid errors.
*   **Pin Conflicts**: Different peripherals may reuse the same GPIO. Please carefully check the pin configurations in `menuconfig` to avoid conflicts between peripherals.
*   **SD Card and USB Drive Capability**: When using an SD card, ensure that the data lines have external pull-up resistors (10k). When using USB MSC, it is recommended that the USB flash drive current demand does not exceed 500mA.
*   **Watchdog**: `vTaskDelay(10 / portTICK_PERIOD_MS)` has been added to the main loop to ensure the idle task can run normally and avoid triggering a watchdog reset.


