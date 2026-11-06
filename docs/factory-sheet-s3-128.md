# Blipscope 1.28" board: factory flashing sheet / 工厂烧录说明

**Final.** The factory uses Espressif's Flash Download Tool (its answer, 2026-11-06). Every step below was run on
our bench ([factory-image-plan.md](factory-image-plan.md)): the erase and write on 2026-11-05, and the write
without the erase on 2026-11-06.

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

### 2. Flash each board: write the one file at 0x0 (only the 3 samples are erased first)

Use **one** of the two tools.

**Which boards are erased first:**
- **The 3 pre-production samples:** a full chip erase first, then the write.
- **Production (every other board, test program or not):** no erase. Write the one file at `0x0`.

**A. esptool** (one command per board):

The 3 samples (the whole chip is erased first, then written):

```
esptool --chip esp32s3 --port <PORT> --before default-reset --after hard-reset write-flash --erase-all --flash-mode keep --flash-freq keep --flash-size keep 0x0 firmware-s3-128.factory.bin
```

Production, every other board (no erase):

```
esptool --chip esp32s3 --port <PORT> --before default-reset --after hard-reset write-flash --flash-mode keep --flash-freq keep --flash-size keep 0x0 firmware-s3-128.factory.bin
```

**B. Espressif Flash Download Tool** (version 3.9.11, from Espressif's own download server,
https://dl.espressif.com/public/flash_download_tool.zip):
1. Start the tool. In its first window, choose the chip type **ESP32-S3**: set **ChipType** to
   `ESP32-S3`, **WorkMode** to `Develop`, and **LoadMode** to `USB`. Click **OK**. Any other chip
   type is wrong for this board.
2. In the first row, select `firmware-s3-128.factory.bin`, enter the address `0x0`, and tick the
   row. Leave every other row empty and unticked.
3. Set **BAUD** to `1152000` (over the board's USB it does not matter). Set **SPI SPEED** to `80MHz`, **SPI MODE**
   to `QIO`, and **FLASH SIZE** to `16MB`.
4. **DoNotChgBin** must be ticked. In this version it is already ticked and locked: leave it as it is, so the
   file is written exactly as it is. In any tool version that lets it be unticked, it must stay ticked:
   unticked, the tool rewrites the image header's flash settings, which can stop boards from booting.
5. **Do not burn any eFuses.**
6. Select the board's COM port.
7. For each board:
   - **production (every other board):** click **START** only, and wait for **FINISH**;
   - **the 3 samples:** click **ERASE** and wait for **FINISH**. Then click **START** and wait for **FINISH**.

The tool is **not code-signed**, so Windows may warn when it is opened, and the factory's IT may
need to allow it. Its user guide for the ESP32-S3:
https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32s3/production_stage/tools/flash_download_tool.html

**Time per board** (esptool, measured on our bench): production, without the erase, **about 21 seconds**
(2026-11-06); the samples, with the erase, about 1 minute, of which about 40 seconds is the erase (2026-11-05).

**If the computer does not see the board:** press and hold the board's left-side button for 2
seconds to power it on. On our bench the board came on by itself from USB (2026-11-05), so this is only a
fallback.

### 3. Pass signs: a board passes only if both appear

1. **The tool reports success:**
   - **esptool:** its output shows `Hash of data verified.` **exactly once**. The line missing, or
     appearing twice, is a **FAIL**, even if esptool exits normally.
   - **Flash Download Tool:** START ends in **FINISH**. This tool does not print the hash line.
2. **The screen shows this sequence** within about 15 seconds after the board resets:
   1. a splash screen reading **"Blipscope V18"**;
   2. a 3-second countdown;
   3. a green **`SETUP`** with a QR code and the unit's name, on a black background.
   - esptool resets the board itself. After the Flash Download Tool, unplug and replug the board.
   - A splash with any other version, a black screen, an orange screen, or a board that keeps
     restarting is a **FAIL**.

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
- **The Flash Download Tool** shows it in its DetectedInfo box. Record the value after **STA:**.
  It appears as 12 characters without colons, such as `AABBCCDDEEFF` (example only), and it is the
  same value esptool prints as `MAC:`. Do not record the AP or BT values.

Put the MACs in one plain text file, one MAC per line, and send it to Valar Systems
(daniel@valarsystems.com) together with the shipment. With colons or without them, both are fine.
Saving all the esptool logs and sending them instead is also fine. **A board whose MAC is not on
the list cannot be activated by its customer.**

---

## 中文

### 1. 烧录开始前先核对一次文件

在 Windows 上运行 `certutil -hashfile firmware-s3-128.factory.bin SHA256`。结果必须与上表中的 SHA-256
完全一致。如果不一致，请停止，并重新索取文件。

### 2. 每块板：把这一个文件写入地址 0x0（只有 3 块样品先擦除）

以下两种工具**任选其一**。

**哪些板子需要先擦除：**
- **3 块预生产样品：**先全片擦除，再写入。
- **量产板（其他所有板子，无论是否写入过测试程序）：**不需要擦除，直接把文件写到 0x0。

**A. esptool**（每块板一条命令）：

3 块样品（先擦除整片，再写入）：

```
esptool --chip esp32s3 --port <端口> --before default-reset --after hard-reset write-flash --erase-all --flash-mode keep --flash-freq keep --flash-size keep 0x0 firmware-s3-128.factory.bin
```

量产板，即其他所有板子（不擦除）：

```
esptool --chip esp32s3 --port <端口> --before default-reset --after hard-reset write-flash --flash-mode keep --flash-freq keep --flash-size keep 0x0 firmware-s3-128.factory.bin
```

**B. 乐鑫 Flash 下载工具（Flash Download Tool）**（3.9.11 版，来自乐鑫官方下载服务器
https://dl.espressif.com/public/flash_download_tool.zip）：
1. 打开工具，在第一个窗口中选择芯片类型 **ESP32-S3**：**ChipType** 选 `ESP32-S3`，**WorkMode** 选
   `Develop`，**LoadMode** 选 `USB`，点击 **OK**。选择其他芯片类型对这块板都是错误的。
2. 在第一行选择 `firmware-s3-128.factory.bin`，地址填 `0x0`，并勾选该行。其他行全部留空、不勾选。
3. **BAUD** 选 `1152000`（通过板子自带的 USB 烧录时，该值不影响结果）；**SPI SPEED** 选 `80MHz`、**SPI MODE** 选
   `QIO`、**FLASH SIZE** 选 `16MB`。
4. **必须勾选 DoNotChgBin。**此版本中它已默认勾选并锁定，保持不变即可，文件会按原样写入。如果所用版本允许取消勾选，
   也必须保持勾选：取消勾选后，工具会改写镜像头中的 Flash 设置，可能导致板子无法启动。
5. **不要烧写任何 eFuse。**
6. 选择该板子的 COM 端口。
7. 每块板：
   - **量产板（其他所有板子）：**只点 **START**，等待显示 **FINISH**，把文件写到 0x0；
   - **3 块预生产样品：**先点 **ERASE**（全片擦除），等待显示 **FINISH**；
     完成后再点 **START**，等待显示 **FINISH**。

该工具**没有代码签名**，打开时 Windows 可能会弹出警告，工厂 IT 可能需要放行。ESP32-S3 版用户指南：
https://docs.espressif.com/projects/esp-test-tools/zh_CN/latest/esp32s3/production_stage/tools/flash_download_tool.html

**每块板用时**（esptool，在我们测试台上实测）：量产板不擦除，**约 21 秒**（2026-11-06）；3 块样品含擦除约 1 分钟，
其中擦除约 40 秒（2026-11-05）。

**如果电脑识别不到板子：**长按板子左侧按键 2 秒开机。在我们的测试台上，板子接上 USB 后会自行开机（2026-11-05），因此这只是备用方法。

### 3. 合格标志：两项都出现才算合格

1. **工具报告成功：**
   - **esptool：**输出中出现 `Hash of data verified.`，且**只出现一次**。没有这一行或出现两次，
     即为**不合格**，即使 esptool 正常退出。
   - **Flash 下载工具：**点击 START 后显示 **FINISH**。该工具不会打印上述校验行。
2. **板子复位后约 15 秒内，屏幕依次显示：**
   1. 启动画面显示 **"Blipscope V18"**；
   2. 3 秒倒计时；
   3. 黑色背景上的绿色 **`SETUP`**、二维码和设备名称。
   - esptool 会自动复位板子；使用 Flash 下载工具后，请拔下再插上板子。
   - 启动画面显示其他版本、黑屏、橙色屏幕或反复重启，即为**不合格**。

**不合格时：**再烧录一次。如果仍不合格，请将板子单独放置、贴上标记并报告。不要出货。

### 4. ⚠ 不要打开串口

**烧录后，绝对不要在板子上打开任何串口监视器或串口终端**（包括 Arduino 串口监视器、PuTTY、PlatformIO
monitor 等）。关闭串口可能使这块板停留在下载模式并黑屏。如果出现这种情况，拔下再插上板子即可正常启动。

### 5. 每块板的 MAC 地址清单：随货发送

请记录**每一块**合格板子的 MAC 地址：
- **esptool** 会在输出开头附近打印，格式如 `MAC: AA:BB:CC:DD:EE:FF`（仅为示例）。
- **Flash 下载工具**会在 DetectedInfo 框中显示 MAC。请记录 **STA:** 后面的值，它显示为 12 个字符、
  不带冒号，例如 `AABBCCDDEEFF`（仅为示例），与 esptool 打印的 `MAC:` 是同一个值。不要记录 AP 或 BT 的值。

把所有 MAC 写入一个纯文本文件，每行一个 MAC，随货一起发送给 Valar Systems（daniel@valarsystems.com）。
带冒号或不带冒号均可。也可以保存全部 esptool 日志并直接发送日志。**不在清单上的板子，客户将无法激活。**
