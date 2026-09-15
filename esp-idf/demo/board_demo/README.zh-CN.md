[English](README.md) | [简体中文](README.zh-CN.md)

# ESP32-S31-PI 综合演示示例 (Board Demo)


本示例是 ESP32-S31-PI 开发板的综合性功能演示，旨在展示开发板的主要外设、接口和核心功能。

## 功能特性

本工程包含以下模块的演示：

*   **LED 闪烁**：支持普通 GPIO LED 或可寻址 RGB LED（WS2812）的周期性闪烁。
*   **按键输入**：支持短按和长按检测，包含中断消抖机制。
*   **以太网 (Ethernet)**：基于 LAN8720 PHY，通过 RMII 接口连接，支持 DHCP 获取 IP。
*   **LVGL 显示与触摸**：驱动 RGB LCD 屏幕，支持 GT911 电容触摸，包含单点、多点及手势识别（滑动、旋转、捏合）演示。
*   **SD 卡读写**：通过 SDMMC 接口挂载 FAT 文件系统，演示文件创建、读取、重命名和速度测试。
*   **USB 主机 (USB MSC)**：支持热插拔 U 盘，自动挂载文件系统，演示读写和速度测试。

## 硬件要求

*   ESP32-S31-PI 开发板
*   一条支持数据传输的 USB 线（用于供电和烧录）
*   如果测试以太网，需要一根网线
*   如果测试 SD 卡，需要一张 microSD 卡
*   如果测试 USB MSC，需要一个 U 盘

## 编译与烧录

请确保已经按照 `esp-idf` 文件夹中的指南配置好 ESP-IDF 环境。

1. **设置目标芯片**：
    在工程根目录下打开终端，执行：
        idf.py set-target esp32s31

2. **配置工程参数**：
    你需要通过 `menuconfig` 配置各模块对应的 GPIO 引脚（特别是板载的 LED、按键、LCD 和以太网引脚）：
        idf.py menuconfig
    在菜单中找到 `Example Configuration`，根据你的实际硬件连接配置各外设的引脚和选项。

3. **编译、烧录与监视**：
    连接开发板，在终端中运行以下命令（请将 `(你的串口号)` 替换为实际的端口号）：
        idf.py -p (你的串口号) flash monitor
    例如：
        idf.py -p COM7 flash monitor

### 配置工程参数

你需要通过 `menuconfig` 配置各模块对应的 GPIO 引脚（特别是板载的 LED、按键、LCD 和以太网引脚）：

请在终端中运行以下命令：

    idf.py menuconfig

在菜单中找到 `Example Configuration`，根据你的实际硬件连接配置各外设的引脚和选项。


### 编译、烧录与监视

连接开发板，在终端中运行以下命令（请将 `(你的串口号)` 替换为实际的端口号）：

    idf.py -p (你的串口号) flash monitor

例如：

    idf.py -p COM7 flash monitor

## 预期现象与操作说明

*   **LED**：系统启动后，板载 LED 将按照 `CONFIG_BLINK_PERIOD` 设置的周期闪烁（如果配置了 RGB LED，则会交替显示红、绿、蓝色）。
*   **按键**：按下 BOOT 键，串口终端会打印 `Button pressed!`。长按会打印 `Long press detected!`，松开后显示长按持续时间。
*   **以太网**：插入网线后，串口终端会打印 `ETH Link Up`，并显示获取到的 IP 地址。如果连接失败，程序会尝试自动重连。
*   **LVGL 屏幕**：屏幕会显示 UI 界面。你可以通过触摸屏进行单点触控、多点触控或手势操作（如两指滑动、旋转、缩放等），界面上的星星图标会跟随手势做出物理弹性和惯性滑动反馈。
*   **SD 卡**：插入 SD 卡后，程序会自动挂载，打印卡信息，并执行创建 `hello.txt`、重命名文件、读写测试等操作。
*   **USB MSC**：在开发板 USB Type-A 接口插入 U 盘后，串口终端会打印 U 盘容量和设备信息，并自动执行文件读写和 1MB 的读写速度测试。

## 注意事项

*   **USB MSC 示例**：该示例依赖 `button_input_example` 组件中安装的 GPIO ISR 服务，请勿在 USB 示例中重复安装 ISR 服务，以免报错。
*   **引脚冲突**：不同外设可能复用同一个 GPIO，请在 `menuconfig` 中仔细核对引脚配置，避免外设之间相互冲突。
*   **SD 卡和 USB 驱动能力**：使用 SD 卡时，确保数据线有外部上拉电阻（10k）；使用 USB MSC 时，建议 U 盘电流需求不超过 500mA。
*   **看门狗**：主循环中已加入 `vTaskDelay(10 / portTICK_PERIOD_MS)`，以确保空闲任务能正常运行，避免触发看门狗复位。

