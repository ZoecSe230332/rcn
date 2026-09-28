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
- **Daemon & IPC Architecture**:
  - Runs in the background as a detached daemon.
  - Uses native `AF_UNIX` domain sockets on both Linux (`/tmp/rcn/`) and Windows (`%TEMP%\rcn\`) for non-blocking local IPC.
- **Automatic IP & Port Discovery**:
  - Automatically queries and displays active network interface IPs and the listening port upon server start for easy connection setup.
- **Emergency Stop Keybind**:
  - Press **`Ctrl + Alt + Esc`** (or `Ctrl + Alt + Pause`) at any time to instantly unhook/ungrab all inputs, notify peers, and terminate `rcn`.
- **Reliable Disconnection & Session Management**:
  - Stopping the server immediately shuts down active peer connections, releases all virtual keys, and terminates cleanly on the first attempt.
- **Ready-to-Use Windows Launchers**:
  - Double-clickable batch files (`run.bat`, `start_server.bat`, `stop_server.bat`) for quick access without opening a terminal.

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
|                          |                       | Daemon & IPC:            |
| Daemon & IPC:            |                       | - AF_UNIX /tmp/rcn/      |
| - AF_UNIX %TEMP%\rcn\    |                       | - epoll event loop       |
| - WSAPoll + Message Pump |                       +--------------------------+
+--------------------------+
```

1. **Handshake & Device Registration**: When a client connects to the server, it transmits a `PEER_HEADER_DEV_CRT` descriptor for each captured device (e.g., keyboard, mouse) detailing supported event bits and device attributes.
2. **Input Streaming**: The client intercepts mouse motions and key events, encapsulates them into standard `input_event` packets, and streams them across the TCP connection.
3. **Replay**: The server receives each packet and emits corresponding native input events (`SendInput` on Windows, `/dev/uinput` on Linux).
4. **Relay Daemon**: A local UNIX domain socket accepts commands (`pause`, `resume`, `stop`, `log`) from other terminal processes and relays them across the connection.

---

## Building from Source

### On Windows

**Prerequisites**: Visual Studio (MSVC) with C/C++ tools and CMake.

```powershell
# Generate build files
cmake -B build

# Build Release binary (produces build\Release\rcn.exe)
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

### Option A: Windows 1-Click Launchers (No Terminal Needed)

- **`run.bat`** (Recommended): Double-click to open an interactive launcher menu:
  ```text
  ======================================================
               rcn - Remote Input Daemon
  ======================================================

    [1] Start Server (Listen for incoming inputs on this PC)
    [2] Connect Client (Send mouse/keyboard to remote PC)
    [3] Pause
    [4] Resume
    [5] View Server Logs
    [6] Stop Server / Daemon
    [7] Exit

  ======================================================
  Select an option [1-7] (default 1):
  ```
- **`start_server.bat`**: Double-click to immediately start the server in the background on default port `9999`.
- **`stop_server.bat`**: Double-click to immediately stop the running server daemon.

---

### Option B: Command Line Interface (CLI)

#### 1. Starting the Server (Host Machine)
Start the server in the background to receive inputs:

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

> **Emergency Stop Hotkey**: If you ever need to immediately unhook/ungrab devices and terminate `rcn`, press **`Ctrl + Alt + Esc`** (or `Ctrl + Alt + Pause`) on your keyboard. This restores local controls instantly!

#### 2. Connecting the Client (Sending Inputs)
Connect to the server IP and begin forwarding input:

- **Windows Client**:
  ```powershell
  # Forward both keyboard and mouse:
  .\rcn.exe connect -s <SERVER_IP> -p 9999 -d keyboard mouse

  # Or forward only mouse:
  .\rcn.exe connect -s <SERVER_IP> -p 9999 -d mouse
  ```
- **Linux Client**:
  ```bash
  # Forward specific evdev event devices:
  sudo ./rcn connect -s <SERVER_IP> -p 9999 -d /dev/input/event0 /dev/input/event1
  ```

---

## Managing Running Daemons (Subactions)

While a client or server is running in the background, you can interact with it at any time from any terminal:

| Action | Windows Command | Linux Command | Description |
| :--- | :--- | :--- | :--- |
| **Pause** | `.\rcn.exe server pause`<br>`.\rcn.exe client pause` | `./rcn server pause`<br>`./rcn client pause` | Ungrabs devices; input stays local. |
| **Resume** | `.\rcn.exe server resume`<br>`.\rcn.exe client resume` | `./rcn server resume`<br>`./rcn client resume` | Regrabs devices; resumes remote forwarding. |
| **View Logs** | `.\rcn.exe server log`<br>`.\rcn.exe client log` | `./rcn server log`<br>`./rcn client log` | Prints daemon log output to stdout. |
| **Stop** | `.\rcn.exe server stop`<br>`.\rcn.exe client stop` | `./rcn server stop`<br>`./rcn client stop` | Terminates the background daemon cleanly. |

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
