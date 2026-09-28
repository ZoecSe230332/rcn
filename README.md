# rcn - Remote Input Daemon

`rcn` is a lightweight, high-performance, cross-platform input capture and remote replay daemon written in C. It allows you to forward keyboard and mouse input from a client machine to a remote server over TCP in real time, with full support for pausing, resuming, and background daemon IPC.

`rcn` runs natively on both **Windows** and **Linux**, and supports seamless cross-platform communication in any topology:
- **Windows Client $\leftrightarrow$ Linux Server**
- **Linux Client $\leftrightarrow$ Windows Server**
- **Windows Client $\leftrightarrow$ Windows Server**
- **Linux Client $\leftrightarrow$ Linux Server**

---

## Features

- **Cross-Platform Input Subsystem**:
  - **Linux**: Direct hardware capture via `evdev` (`/dev/input/event*`) with exclusive `EVIOCGRAB` device locking, and input replay via kernel `/dev/uinput` virtual devices.
  - **Windows**: Low-level hooks (`WH_KEYBOARD_LL`, `WH_MOUSE_LL`) with input suppression, and input replay via Win32 `SendInput` (`EV_KEY`, `EV_REL`, `EV_ABS`).
- **Binary Wire Protocol Compatibility**:
  - Byte-for-byte exact 64-bit struct layouts (`struct device_info`, `struct peer_msg_event`, `struct stream_header`) ensuring zero-overhead binary streaming without endianness or padding mismatches.
  - Bi-directional mapping between Windows Virtual-Key / Scan codes and Linux input event keycodes.
- **Foreground Console Execution & Clean Stopping**:
  - Runs directly in the terminal foreground with live status output and logs.
  - Simply press **`Ctrl + C`** at any time to cleanly stop the server or client and release all inputs.
- **Fail-Safe Disconnect & Automatic Input Recovery**:
  - Fast TCP keepalive probes (2-second timeout) and event loop close-detection ensure that if the connection drops (network off, Wi-Fi lost, server crash), the client **immediately releases all low-level keyboard and mouse hooks**, fully restores local machine control, and exits without freezing or locking input.
- **Emergency Stop Hotkey**:
  - Press **`Ctrl + Alt + Esc`** (or `Ctrl + Alt + Pause`) at any time for an instant hardware-level release of all hooks and immediate process exit.
- **Automatic IP & Port Discovery**:
  - Queries and displays local network interface IPs and the listening port upon server start for easy connection setup.
- **Ready-to-Use Windows Launchers**:
  - Double-clickable batch files (`run.bat`, `start_server.bat`) for quick access directly in your command prompt.

---

## How It Works

```
                      +-----------------------------+
                      |         TCP Network         |
                      |  Binary Wire Protocol (TCP) |
                      +--------------+--------------+
                                     ^
                                     |
           +-------------------------+-------------------------+
           |                                                   |
           v                                                   v
+--------------------------+                       +--------------------------+
|      Windows Host        |                       |       Linux Host         |
|--------------------------|                       |--------------------------|
| Client:                  |                       | Client:                  |
| - Low-level Hooks        |                       | - evdev input capture    |
|   (WH_KEYBOARD/MOUSE_LL) |                       | - EVIOCGRAB exclusive    |
| - Scan/VK -> Linux Code  |                       |                          |
|                          |                       | Server:                  |
| Server:                  |                       | - uinput virtual devices |
| - Linux Code -> Scan/VK  |                       | - Linux evdev events     |
| - Win32 SendInput        |                       |                          |
|                          |                       | Terminal & Event Loop:   |
| Terminal & Event Loop:   |                       | - epoll event loop       |
| - WSAPoll + Message Pump |                       | - Foreground / Ctrl+C    |
+--------------------------+                       +--------------------------+
```

1. **Handshake & Device Registration**: When a client connects to the server, it transmits a `PEER_HEADER_DEV_CRT` descriptor for each captured device (e.g., keyboard, mouse) detailing supported event bits and device attributes.
2. **Input Streaming**: The client intercepts mouse motions and key events, encapsulates them into standard `input_event` packets, and streams them across the TCP connection.
3. **Replay**: The server receives each packet and emits corresponding native input events (`SendInput` on Windows, `/dev/uinput` on Linux).
4. **Safety & Keepalive**: TCP keepalive monitors the connection every second. If disconnected, hooks and grabs are instantly released.

---

## Building from Source

### On Windows

**Prerequisites**: Visual Studio (MSVC) with C/C++ tools and CMake.

```powershell
# Generate build files
cmake -B build

# Build Release binary (produces build\Release\rcn.exe and ./rcn.exe)
cmake --build build --config Release
```

### On Linux

**Prerequisites**: GCC and Make (`sudo apt install build-essential`).

```bash
# Build binary (produces ./rcn)
make
```

---

## Quick Start & Usage

### Option A: Windows Interactive Launcher

- **`run.bat`**: Double-click to open the launcher menu:
  - Select `[1]` to start server (runs in foreground; press `Ctrl+C` to stop).
  - Select `[2]` to connect client to a server IP (runs in foreground; press `Ctrl+C` to disconnect).
- **`start_server.bat`**: Double-click to immediately start listening on port `9999` in your terminal.

---

### Option B: Command Line Interface (CLI)

#### 1. Starting the Server (Host Machine)
Start the server directly in your terminal to receive inputs:

- **Windows**:
  ```powershell
  .\rcn.exe start -p 9999
  ```
- **Linux**:
  ```bash
  ./rcn start -p 9999
  ```

Upon startup, `rcn` prints all available IP addresses on the host:
```text
rcn: Server listening on port 9999
rcn: IP address(es) for client to connect to:
       -> 192.168.1.150 (port 9999)
```
Press **`Ctrl + C`** in the terminal at any time to stop the server cleanly.

> **Emergency Stop Hotkey**: If you ever need to immediately unhook/ungrab devices and terminate `rcn`, press **`Ctrl + Alt + Esc`** (or `Ctrl + Alt + Pause`) on your keyboard. This restores local controls instantly!

#### 2. Connecting the Client (Sending Inputs)
Connect to the server IP and begin forwarding input:

- **Windows Client**:
  ```powershell
  .\rcn.exe connect -s <SERVER_IP> -p 9999
  ```
- **Linux Client**:
  ```bash
  sudo ./rcn connect -s <SERVER_IP> -p 9999 -d /dev/input/event0 /dev/input/event1
  ```

Press **`Ctrl + C`** in the client terminal at any time to disconnect and restore local keyboard and mouse control.

---

## Cross-Platform Verification Tests

The repository includes a verification suite in `tests/`:
- **`tests/test_linux_client.c`**: Verifies sending Linux device creation (`device_info`), keystroke events (`KEY_A`), and control signals to a Windows server.
- **`tests/test_linux_server.c`**: Verifies accepting Windows client device registrations, validating Linux 64-bit struct layouts, and issuing control commands.

Compile and run via WSL or native Linux:
```bash
gcc -Wall -Wextra -I. tests/test_linux_client.c -o tests/test_linux_client
gcc -Wall -Wextra -I. tests/test_linux_server.c -o tests/test_linux_server
```

---

## License

See repository details for license information.
