[English](README.md) | [简体中文](README.zh-CN.md)

# ESP-IDF User Guide

This document describes how to use ESP-IDF for development on the ESP32-S31-PI.


> **Fast Track**:
> If you have already mastered the basic ESP-IDF configuration (environment setup, creating a new project, compiling and flashing, project configuration, etc.), you can quickly skip the preceding basic chapters and jump directly to **[How to Use the Examples in This Repository](#how-to-use-the-examples-in-this-repository)**.
> If you are a beginner, it is recommended to read them in order. The preceding basic chapters will guide you through the complete workflow from environment setup, creating a new project, compiling and flashing, to partition table configuration and enabling PSRAM.

## Official Reference Installation Steps (Optional)

For the latest and most detailed official installation instructions, please visit: [ESP-IDF Get Started](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s31/get-started/index.html)

> **Note**: Espressif provides complete installation documentation, which can serve as an authoritative reference. Later in this document, we will provide a streamlined set of configuration steps, starting with running EIM.

## Preparation

Before starting the formal configuration, please complete the following three preparation tasks:

### 1. Install VS Code
Please follow the official guide to download and install VS Code: [VS Code Official Guide](https://code.visualstudio.com/docs/getstarted/overview)

### 2. Install the ESP-IDF Extension
After installing VS Code, open the Extensions Marketplace on the left, search for `idf`, and install the ESP-IDF extension provided by Espressif Systems.

![VS Code ESP-IDF Extension](images/vscode-esp-idf-extension.png "Search for idf in VS Code Extensions Marketplace")

### 3. Download the EIM Tool
Please go to Espressif's official release page and download the ESP-IDF Installation Manager (EIM) suitable for your operating system:
[ESP-IDF Installation Manager](https://dl.espressif.cn/dl/eim/)

> After completing the above preparations, please continue reading the following chapters. Starting with running EIM, we will complete the environment configuration step by step.

## Installation Steps

1. Double-click the EIM installer you downloaded (for example, `eim-gui-windows-x64.exe`) to open the ESP-IDF Installation Manager welcome screen:

![EIM Welcome Screen](images/eim-welcome.png "ESP-IDF Installation Manager Welcome Screen")

2. Click the "Start Installation" button under "New Installation" to enter the installation options screen:

![EIM Installation Options](images/eim-install-option.png "Select installation type")

3. Select "Easy Installation" and click the red "Start Easy Installation" button.

![EIM Ready to Install](images/eim-ready-to-install.png "Ready to install ESP-IDF")

On the next screen, select `v6.1` (or later) from the "ESP-IDF version" drop-down menu.

> **Note**: ESP32-S31 support requires v6.1 or later. Do not select a version lower than v6.1.

Then click the red "Start Installation" button.

4. Follow the on-screen prompts. During this process, the system may prompt you to install or update necessary toolchains such as Python.

Once the installation is complete, the environment is ready.

## Creating a New Example Project

1. Double-click to open VS Code. In the Activity Bar on the left (the leftmost vertical bar), find and click the **ESP-IDF** icon at the bottom (similar to a chip or spider web shape):

![Click the ESP-IDF icon](images/vscode-esp-idf-sidebar.png "Open the ESP-IDF Explorer")

2. In the left sidebar, click to expand `Advanced`, and then click `New Project Wizard`.

![Select IDF version](images/vscode-select-idf-version.png "Select ESP-IDF version")

A drop-down menu will then appear at the top center of VS Code. Select the ESP-IDF version `v6.1` or later that we installed earlier (for example, `Use ESP-IDF C:\esp\v6.1\...`).

3. On the new project page, expand the following path on the left: `ESP-IDF Examples` -> `get-started` -> `hello_world`. After selecting `hello_world`, click the blue button `Create project using template hello_world` near the top of the page:

![Select hello_world template](images/vscode-new-project-template.png "Select hello_world example template")

4. Configure the project details. Keep `Project Name` as default. Click the folder icon on the right to select the path where you want to store the project. In the `Choose ESP-IDF Target` drop-down, select `esp32s31`; in the `Choose ESP-IDF Board` drop-down, select `ESP32-S31 chip (via builtin USB-JTAG)`:

![Configure project parameters](images/vscode-new-project-config.png "Configure project path and target chip")

After confirming that everything is correct, click the blue `Create Project` button in the bottom right corner of the page.

5. When the page displays `Project has been created!`, the project has been created successfully. Click the blue `Open Project` button in the middle of the page to open the project directly in the current window:

![Open the created project](images/vscode-project-created.png "Click Open Project to open the project")

6. In daily use, there are usually two ways to open a project. The first is to click `File` -> `Open Folder...` in the VS Code top menu bar; the second is to directly drag and drop the project folder onto the VS Code window or icon.

![Drag a folder to open the project](images/vscode-drag-folder.png "Drag the folder directly onto VS Code to open it")

Note that regardless of which method you use, the folder you open must **directly contain the `main` folder** (i.e., the project root directory) so that VS Code can correctly recognize it as an ESP-IDF project.


## Project Setup and Preparation

1. After opening the project, the page layout is shown in the following figure. In the Explorer on the left, click to expand the `main` folder:

![Project directory structure](images/vscode-project-main.png "View the project directory structure")

2. Click `hello_world_main.c` under the `main` folder to view or modify the example program on the right:

![View example code](images/vscode-hello-world-code.png "View or modify example code")

3. Next, configure the flashing method. In the blue status bar at the bottom left of the VS Code interface, find and click a star-shaped or flame-shaped icon (depending on your theme; here it is the flash method selection button):

![Select flash method](images/vscode-flash-method-uart.png "Select flash method")

In the drop-down menu that appears at the top, select `UART`.

4. Then select the serial port. In the blue status bar at the bottom left of VS Code, click the `detect` button; it will automatically scan for serial devices currently connected to your computer:

![Select serial port](images/vscode-select-serial-port.png "Select serial port")

> **Note**: If no device can be found at this step, please install the CH340 driver first, or check whether the development board connection is loose and whether the data cable supports data transfer.

After scanning is complete, in the drop-down menu that appears at the top, select the serial port corresponding to your development board (for example, `COM7`). At this point, the Project Setup and Preparation chapter is complete. In the next chapter, we will describe how to compile, flash, and debug.



## Compile, Flash, and Debug

1. In the blue status bar at the bottom of VS Code, click the first wrench-shaped icon on the left (the build button) to start building the project:

![Click the build button](images/vscode-build-icon.png "Click the wrench icon to start building the project")

2. During compilation, the terminal window at the bottom will output progress in real time. After compilation is complete, a detailed memory usage information table (Memory Type Usage Summary) will be output:

![Compilation output](images/vscode-build-output.png "View the memory usage table after compilation")

3. After a successful build, a new `build` folder will be generated in the Explorer on the left. It contains the compiled binary files, partition table, `sdkconfig.h`, and other project information. You can send the firmware `.bin` files in the build folder to others, and they can use a dedicated flashing tool (such as Flash Download Tool) to download them directly to the board. At the same time, the `sdkconfig` file stores the basic configuration generated for the chip (such as Flash size, whether PSRAM is enabled, etc.). In the next chapter, we will describe how to use the `SDK Configuration Editor` to configure these parameters:

![View build directory](images/vscode-build-folder.png "Find the build folder and sdkconfig file")

4. After confirming that the build is correct, click the lightning bolt-shaped icon in the bottom status bar (the flash button) to download the program to the development board via the serial port:

![Click the flash button](images/vscode-flash-icon.png "Click the lightning bolt icon to start flashing")

5. The terminal will display flashing logs such as connecting to the chip, erasing, writing, and verifying. When you see `Flash has finished.`, the flashing is successful:

![Flashing output](images/vscode-flash-output.png "View the flashing process and completion prompt")

6. After downloading is complete, click the screen/monitor icon on the right side of the bottom status bar to open the serial terminal and establish communication with the development board. If you need to exit the monitor, select the terminal window and press `Ctrl + C`:

![Open serial monitor](images/vscode-monitor-icon.png "Open the serial monitor to view Hello World")


## Project Configuration and Partition Table Settings

1. In the blue status bar at the bottom left of VS Code, click the gear-shaped icon (`SDK Configuration Editor`) to open the project configuration menu:

![Click the gear icon](images/vscode-sdkconfig-gear-icon.png "Click the gear icon in the bottom left to open SDK configuration")

2. After a short while, the SDK Configuration Editor interface will open. At the top of this page there is a search box that allows you to quickly search for and locate options; on the right is a directory tree of configuration items, which you can click to jump directly to the desired configuration item:

![SDK configuration interface](images/vscode-sdkconfig-main-interface.png "SDK Configuration Editor main interface")

3. In the left directory, click to expand `Boot ROM Behavior`, then find `Flash size` on the right and select `16 MB` from the drop-down menu:

![Configure Flash size](images/vscode-sdkconfig-flash-size.png "Configure Flash Size as 16MB")

4. Click `Partition Table` in the left directory. In the `Partition Table` drop-down menu on the right, select `Custom partition table CSV` (i.e., custom partition table):

![Configure custom partition table](images/vscode-sdkconfig-partition-table.png "Select custom partition table")

5. Select the default name `partitions.csv` in the `Custom partition CSV file` input box and press `Ctrl + C` to copy the file name (you can also change it to another name at this point; here we use the default name):

![Copy partition table file name](images/vscode-sdkconfig-partition-filename.png "Copy partition table file name")

6. Click the plus sign file icon at the top of the Explorer to create a new file in the project root directory. Paste the `partitions.csv` name you just copied and press Enter to create it. Then copy and paste the following example partition table code into it:

![Create partition table file](images/vscode-project-partitions-file.png "Create and edit the partitions.csv file")

```csv
# Name,   Type, SubType,  Offset,   Size,     Flags
nvs,      data, nvs,        0x9000,   0x5000,
otadata,  data, ota,        0xe000,   0x2000,
app0,     app,  ota_0,     0x10000, 0x600000,
app1,     app,  ota_1,    0x610000, 0x600000,
ffat,     data, fat,      0xC10000, 0x3F0000,
#16MB 6MB + 6MB app ffat 3.9375MB
#16M_app6M_fat4M.csv
```

> **Note**: This custom partition table allocates a relatively large amount of space for applications (6 MB each for the `app0` and `app1` partitions, and a 4 MB `ffat` file system partition), making it very suitable for scenarios that require storing large amounts of data or performing OTA upgrades. For the specific calculation and writing rules of partition tables, please refer to the official documentation: [API Guide - Partition Tables](https://docs.espressif.com/projects/esp-idf/zh_CN/stable/esp32/api-guides/partition-tables.html).

After editing is complete, press `Ctrl + S` to save the file. Then click the `Save` button in the upper right corner of the SDK Configuration Editor to close the configuration page, and the project configuration is complete.

![Save configuration](images/vscode-sdkconfig-save.png "Save configuration and exit")

Configuration complete. In the next chapter, we will describe how to configure PSRAM (external RAM).


## Configuring PSRAM (External RAM)

1. In the Explorer on the left, click to open the `CMakeLists.txt` file in the project root directory:

![Open CMakeLists file](images/vscode-cmakelists-minimal-build.png "Open the CMakeLists.txt file in the root directory")

Find line 7, `idf_build_set_property(MINIMAL_BUILD ON)`, and change its parameter from `ON` to `OFF`.

> **Brief note**: When `MINIMAL_BUILD` is set to `ON`, the build system only compiles the minimal dependency set, which may prevent some components (such as PSRAM) from linking properly. Changing it to `OFF` ensures that the build system includes complete dependencies, which is a prerequisite for enabling features such as PSRAM.

2. After making the change, press `Ctrl + S` to save. Click the wrench-shaped icon (build button) in the bottom left status bar again to rebuild the project. Wait for the build to complete.

3. After the build is complete, click the gear-shaped icon in the bottom left status bar to open the SDK Configuration Editor again. In the `Search parameter` search box at the top, enter `ESP PSRAM`:

![Enable PSRAM configuration](images/vscode-sdkconfig-enable-psram.png "Search for and check Support for external PSRAM")

4. In the `ESP PSRAM` configuration item on the right, check `Support for external PSRAM`, and then click the `Save` button in the upper right corner.


## Enabling Debug Dependencies (Error Information Tracking)

To allow the program to output clearer debugging information when it crashes, it is recommended to enable stack trace printing for errors.

1. Click the gear-shaped icon in the bottom left status bar to open the SDK Configuration Editor, and in the `Search parameter` search box at the top, enter `Panic handler behaviour`:

![Configure Panic handler behaviour](images/vscode-sdkconfig-panic-handler.png "Search for and configure Panic handler behaviour")

2. In the right-side `ESP System Settings` -> `Panic handler behaviour` drop-down menu, select `Print registers and reboot`.

> **Note**: This option causes the system to automatically print the CPU register state and the call stack at the time of the error when a serious error (Panic) causes a crash, helping developers quickly locate the problem. If you want the system not to reboot after an error for debugging purposes, you can also select the `Print registers and halt` option.

3. After selecting, click the `Save` button in the upper right corner to enable detailed error tracking. When the program crashes due to an exception during operation, the serial terminal will output detailed error location information.

At this point, the basic development environment configuration is complete. Next, we will introduce the specific examples provided in this repository.

## How to Use the Examples in This Repository

This repository provides two parts of examples: a complete comprehensive demonstration Demo (located in the `demo` folder), and runnable examples based on official examples with minor modifications to configuration files (default pins and parameters) so they can be used directly with this development board (located in the `examples` folder).

### Comprehensive Demo Example

This example comprehensively demonstrates most of the development board's peripheral functions, including an LCD RGB display + LVGL 9.5 + GT911 touch, RGB WS2812 LEDs, Ethernet, SD card, USB flash drive mounting, and a comprehensive demonstration of a pluggable socket for USB 2.0 peripherals. You can click **[Demo Documentation](../demo/board_demo/README.md)** to view detailed project introduction and usage instructions.

### Other Basic Examples

The `examples` folder contains basic examples that have been modified and adapted. They can be opened, compiled, and debugged directly as standard projects, and include some of the most basic commonly used functions (such as `get-started`, `peripherals`, etc.).

If you need more official examples, you can use the New Project Wizard. Click the ESP-IDF icon in the left sidebar, expand `Advanced`, and click `New Project Wizard`; you will then see the vast majority of official example templates provided by Espressif for ESP32:

![New Project Wizard](images/vscode-new-project-templates.png "Select an official example template in the New Project Wizard")


