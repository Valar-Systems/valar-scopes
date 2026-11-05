# Blipscope 1.28" board: factory flashing sheet / 工厂烧录说明

**DRAFT.** Due to the factory 2026-11-11. Items marked *(bench)* are confirmed or filled in by the
bench proof in [factory-image-plan.md](factory-image-plan.md) before the sheet is sent.

| | |
|---|---|
| Board / 板子 | ESP32-S3, 16 MB flash, 8 MB PSRAM, 1.28" round screen / 1.28 寸圆屏 |
| File / 文件 | `firmware-s3-128.factory.bin` (firmware v18) |
| Size / 大小 | 2,059,264 bytes |
| SHA-256 | `1e1670abc917737733b15ef8875286f2945e1d9ac16148dbf877c1c6814b20bd` |
| Address / 地址 | `0x0` |
| Source / 来源 | https://github.com/Valar-Systems/valar-scopes/releases/download/v18/firmware-s3-128.factory.bin |

---

## English

### 1. Check the file once, before the run

On Windows, run `certutil -hashfile firmware-s3-128.factory.bin SHA256`. The result must match the
SHA-256 above exactly. If it does not, stop and ask for the file again.

### 2. Flash each board: erase the whole chip, then write the one file at 0x0

Use **one** of the two tools.

**A. esptool** (one command per board; it erases first, then writes):

```
esptool --chip esp32s3 --port <PORT> --before default-reset --after hard-reset write-flash --erase-all --flash-mode keep --flash-freq keep --flash-size keep 0x0 firmware-s3-128.factory.bin
```

**B. Espressif Flash Download Tool:**
1. Start the tool. Set **ChipType** to `ESP32-S3`, **WorkMode** to `Develop`, and **LoadMode** to
   `USB`. Click **OK**.
2. In the first row, select `firmware-s3-128.factory.bin`, enter the address `0x0`, and tick the
   row. Leave every other row empty and unticked.
3. Tick **DoNotChgBin**, so the file is written exactly as it is.
4. Select the board's COM port.
5. For each board: click **ERASE** and wait until it finishes. Then click **START** and wait until
   it shows **FINISH**.

*(bench)* These Download Tool steps follow Espressif's user guide. They have not yet been run on
our bench.

**If the computer does not see the board:** press and hold the board's left-side button for 2
seconds to power it on. On this board, USB power alone may only light the charger LED. *(bench)*

### 3. Pass signs: a board passes only if both appear

1. **The flash log shows `Hash of data verified.` exactly once.**
   - For esptool, look in its printed output.
   - For the Flash Download Tool, look in its console window. *(bench)*
   - The line missing, or appearing twice, is a **FAIL**, even if the tool says it finished.
2. **The screen shows a green `SETUP` with a QR code and the unit's name, on a black background,**
   within 15 seconds after the board resets.
   - esptool resets the board itself.
   - After the Flash Download Tool, unplug and replug the board.
   - A black screen, an orange screen, or a board that keeps restarting is a **FAIL**.

**On a FAIL:** flash the board once more. If it fails again, set it aside, label it, and report
it. Do not ship it.

### 4. ⚠ Do not open a serial port

**Never open a serial monitor or serial terminal on the board after flashing.** That includes the
Arduino monitor, PuTTY and the PlatformIO monitor. Closing a serial port can leave this board stuck
in download mode with a black screen. If that happens, unplug and replug the board, and it starts
normally.

### 5. The per-unit MAC list: send it with the shipment

Record the MAC address of **every** board that passes:
- **esptool** prints it near the top of its output, on a line like
  `MAC: AA:BB:CC:DD:EE:FF` (example only).
- **The Flash Download Tool** shows it in the detected-chip information. Record the **STA** MAC
  (the Wi-Fi station MAC), which is the same value esptool prints as `MAC:`. *(bench)*

Put the MACs in one plain text file, one MAC per line, and send it to Valar Systems
(daniel@valarsystems.com) together with the shipment. Saving all the esptool logs and sending them
instead is also fine. **A board whose MAC is not on the list cannot be activated by its customer.**

---

## 中文

### 1. 烧录开始前先核对一次文件

在 Windows 上运行 `certutil -hashfile firmware-s3-128.factory.bin SHA256`。结果必须与上表中的 SHA-256
完全一致。如果不一致，请停止，并重新索取文件。

### 2. 每块板：先擦除整片 Flash，再把这一个文件写入地址 0x0

以下两种工具**任选其一**。

**A. esptool**（每块板一条命令；先擦除，再写入）：

```
esptool --chip esp32s3 --port <端口> --before default-reset --after hard-reset write-flash --erase-all --flash-mode keep --flash-freq keep --flash-size keep 0x0 firmware-s3-128.factory.bin
```

**B. 乐鑫 Flash 下载工具（Flash Download Tool）：**
1. 打开工具，**ChipType** 选 `ESP32-S3`，**WorkMode** 选 `Develop`，**LoadMode** 选 `USB`，点击
   **OK**。
2. 在第一行选择 `firmware-s3-128.factory.bin`，地址填 `0x0`，并勾选该行。其他行全部留空、不勾选。
3. 勾选 **DoNotChgBin**，使文件按原样写入。
4. 选择该板子的 COM 端口。
5. 每块板：先点击 **ERASE**，等待擦除完成；再点击 **START**，等待显示 **FINISH**（完成）。

*(待验证)* 以上下载工具步骤依据乐鑫用户指南编写，尚未在我们的测试台上实际运行。

**如果电脑识别不到板子：**长按板子左侧按键 2 秒开机。这块板仅接 USB 时，可能只有充电指示灯亮。*(待验证)*

### 3. 合格标志：两项都出现才算合格

1. **烧录日志中出现 `Hash of data verified.`，且只出现一次。**
   - esptool：看它输出的内容。
   - Flash 下载工具：看它的控制台窗口。*(待验证)*
   - 没有这一行，或出现两次，即为**不合格**，即使工具显示已完成。
2. **板子复位后 15 秒内，屏幕在黑色背景上显示绿色的 `SETUP`、二维码和设备名称。**
   - esptool 会自动复位板子。
   - 使用 Flash 下载工具后，请拔下再插上板子。
   - 黑屏、橙色屏幕或反复重启，即为**不合格**。

**不合格时：**再烧录一次。如果仍不合格，请将板子单独放置、贴上标记并报告。不要出货。

### 4. ⚠ 不要打开串口

**烧录后，绝对不要在板子上打开任何串口监视器或串口终端**（包括 Arduino 串口监视器、PuTTY、PlatformIO
monitor 等）。关闭串口可能使这块板停留在下载模式并黑屏。如果出现这种情况，拔下再插上板子即可正常启动。

### 5. 每块板的 MAC 地址清单：随货发送

请记录**每一块**合格板子的 MAC 地址：
- **esptool** 会在输出开头附近打印，格式如 `MAC: AA:BB:CC:DD:EE:FF`（仅为示例）。
- **Flash 下载工具**会在检测到的芯片信息中显示 MAC。请记录 **STA** MAC（Wi-Fi Station MAC），它与
  esptool 打印的 `MAC:` 是同一个值。*(待验证)*

把所有 MAC 写入一个纯文本文件，每行一个 MAC，随货一起发送给 Valar Systems（daniel@valarsystems.com）。
也可以保存全部 esptool 日志并直接发送日志。**不在清单上的板子，客户将无法激活。**
