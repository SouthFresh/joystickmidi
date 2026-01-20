# JoystickMIDI 🎮➡️🎹

A simple cross-platform utility to map HID joystick/gamepad inputs (axes/buttons) to MIDI messages (Note On/Off or CC).

[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-0078D6?style=flat-square&logo=windows&logoColor=white&logo=linux&logoColor=white)](https://www.microsoft.com/windows)

## Features

*   **Multi-control mapping** - Map multiple joystick/gamepad buttons and axes simultaneously.
*   Map to MIDI Note On/Off or Control Change (CC) messages.
*   **Default MIDI channel** with per-mapping channel override.
*   Configure note/CC number, velocity, and output values per control.
*   Interactive axis calibration (min/max detection) and reversal.
*   Save and load configurations (`.hidmidi.json`).
*   **Edit existing configurations** - Add, remove, or modify control mappings without starting from scratch.
*   **Descriptive control names** - Displays human-readable names like "X Axis", "Throttle", "Hat Switch" instead of raw HID codes.
*   **Graceful device handling** - Prompts to connect the device if not found, with retry option.
*   Simple console interface.
*   Cross-platform support for Windows and Linux.

## Program Flow

```mermaid
flowchart TD
    subgraph Startup
        A[Start Application] --> B{Config files exist?}
        B -->|Yes| C[Display config list]
        C --> D{Load existing or create new?}
        D -->|Load existing| E[Load JSON config]
        D -->|Create new| F[New Configuration]
        B -->|No| F
    end

    subgraph NewConfig ["New Configuration Setup"]
        F --> G[Step 1: Enumerate HID devices]
        G --> H[Select joystick/gamepad]
        H --> I[Step 2: Enumerate MIDI ports]
        I --> J[Select MIDI output port]
        J --> K[Step 3: Set default MIDI channel]
        K --> L[Step 4: Add Control Mapping]
    end

    subgraph AddMapping ["Add Control Mapping Loop"]
        L --> M[Display available controls]
        M --> N{Select control or finish?}
        N -->|Select control| O[Configure MIDI settings]
        O --> P{Button or Axis?}
        P -->|Button| Q[Set Note/CC type & values]
        P -->|Axis| R[Set CC & reverse option]
        R --> S[Calibrate min/max range]
        Q --> T{Add another control?}
        S --> T
        T -->|Yes| M
        T -->|No| U[Step 5: Save configuration]
        N -->|Finish| U
    end

    subgraph LoadConfig ["Load Existing Config"]
        E --> V[Find HID device by path]
        V --> W{Device found?}
        W -->|No| X[Device Not Connected prompt]
        X --> X1{Retry or Exit?}
        X1 -->|Retry| V
        X1 -->|Exit| END1[Exit]
        W -->|Yes| Y{Run or Edit config?}
        Y -->|Edit| EDIT[Edit Configuration Menu]
        EDIT --> Y
        Y -->|Run| Z[Find MIDI port by name]
        Z --> ZZ{Port found?}
        ZZ -->|No| AA[Error: MIDI port not found]
        AA --> END1
        ZZ -->|Yes| BB[Initialize mapping states]
    end

    subgraph EditConfig ["Edit Configuration Menu"]
        EDIT --> ED1{Select option}
        ED1 -->|Add mapping| ED2[Select control & configure MIDI]
        ED1 -->|Remove mapping| ED3[Select & confirm removal]
        ED1 -->|Edit mapping| ED4[Modify MIDI settings/recalibrate]
        ED1 -->|Change channel| ED5[Set new default channel]
        ED1 -->|Save| ED6[Save to file]
        ED1 -->|Continue| ED7[Exit edit menu]
        ED2 --> ED1
        ED3 --> ED1
        ED4 --> ED1
        ED5 --> ED1
        ED6 --> ED1
    end

    U --> BB
    BB --> CC[Start input monitor thread]

    subgraph Monitoring ["Real-time Monitoring Loop"]
        CC --> DD[Display monitoring status]
        DD --> EE{For each mapping}
        EE --> FF{Value changed?}
        FF -->|Yes| GG{Button or Axis?}
        GG -->|Button| HH[Send Note On/Off or CC]
        GG -->|Axis| II[Calculate MIDI value 0-127]
        II --> JJ[Send CC message]
        HH --> KK{More mappings?}
        JJ --> KK
        FF -->|No| KK
        KK -->|Yes| EE
        KK -->|No| LL{Quit signal?}
        LL -->|No| DD
        LL -->|Yes| MM[Cleanup & Exit]
    end

    subgraph InputThread ["Input Monitor Thread"]
        direction TB
        NN[Read HID input events] --> OO{For each mapping}
        OO --> PP{Event matches control?}
        PP -->|Yes| QQ[Update value & set changed flag]
        PP -->|No| RR{More mappings?}
        QQ --> RR
        RR -->|Yes| OO
        RR -->|No| SS{Quit flag set?}
        SS -->|No| NN
        SS -->|Yes| TT[Thread exit]
    end

    CC -.-> NN
    MM --> END2[Exit]

    style A fill:#90EE90
    style END1 fill:#FFB6C1
    style END2 fill:#90EE90
    style MM fill:#87CEEB
```

### Data Flow

```mermaid
flowchart LR
    subgraph Input ["Input Layer"]
        JS[Joystick/Gamepad]
        JS -->|HID Events| WIN[Windows Raw Input API]
        JS -->|Input Events| LIN[Linux /dev/input]
    end

    subgraph Processing ["Processing Layer"]
        WIN --> MT[Monitor Thread]
        LIN --> MT
        MT -->|Update values| MS[(Mapping States)]
        MS -->|Check changes| ML[Main Loop]
    end

    subgraph Output ["Output Layer"]
        ML -->|MIDI Messages| RTMIDI[RtMidi Library]
        RTMIDI --> MIDI[MIDI Output Port]
    end

    subgraph Config ["Configuration"]
        JSON[(JSON Config File)]
        JSON <-->|Load/Save| CFG[MidiMappingConfig]
        CFG --> ML
    end

    style JS fill:#FFD700
    style MIDI fill:#9370DB
    style JSON fill:#FFA07A
```

## Get Latest Release

[![Latest Release](https://img.shields.io/github/v/release/serifpersia/joystickmidi?label=latest%20release&style=flat-square&logo=github)](https://github.com/serifpersia/joystickmidi/releases/latest)

Click the badge above to download the latest pre-compiled version for your operating system.

## Requirements

### For Windows
*   Windows 7 or later.
*   A C++ Compiler (MinGW or Visual Studio).
*   CMake.
*   Git.

### For Linux (Debian/Ubuntu-based)
*   A modern Linux distribution (e.g., Debian 12, Ubuntu 22.04).
*   `build-essential`, `cmake`, `git`.
*   Development libraries: `libasound2-dev` (for ALSA/MIDI) and `libudev-dev` (for device detection).
    ```bash
    sudo apt update
    sudo apt install build-essential cmake git libasound2-dev libudev-dev
    ```

### General
*   An HID-compliant joystick or gamepad.
*   A MIDI output device (virtual or physical).

## Getting Started (Build from Source)

1.  **Clone the repository:**
    ```bash
    git clone https://github.com/serifpersia/joystickmidi.git
    cd joystickmidi
    ```

2.  **Run the appropriate build script for your OS:**

    ### On Windows
    ```batch
    build.bat
    ```
    *   This script will check for CMake and a compiler. It may offer to install them using `winget` if not found.
    *   It downloads dependencies (RtMidi, nlohmann/json) if needed.
    *   It configures and builds the project using CMake.
    *   The final executable will be in the `build` directory (`build\JoystickMIDI.exe`).

    ### On Linux
    First, make the script executable:
    ```bash
    chmod +x build.sh
    ```
    Then, run the script:
    ```bash
    ./build.sh
    ```
    *   This script downloads dependencies if they are missing.
    *   It configures and builds the project using CMake and Make.
    *   The final executable will be in the `build` directory (`build/JoystickMIDI`).

## Usage

1.  Run the executable from your command line (`build/JoystickMIDI` or `build\JoystickMIDI.exe`).
2.  **First Run / New Configuration:**
    *   Follow the on-screen prompts to:
        *   Select your HID controller.
        *   Choose the specific button or axis you want to map (with descriptive names like "X Axis", "Throttle", etc.).
        *   Select your MIDI output port.
        *   Set the default MIDI channel.
        *   Configure the MIDI message type (Note/CC), channel, number, and values for each control.
        *   Calibrate the axis range if mapping an axis.
        *   Add as many control mappings as needed.
        *   Save the configuration to a `.hidmidi.json` file.
3.  **Load Configuration:** If `.hidmidi.json` files exist in the same directory, you'll be prompted to load one or create a new configuration.
    *   If the configured device is not connected, you'll be prompted to connect it and retry.
4.  **Edit Configuration:** After loading an existing configuration, you can choose to edit it:
    *   Add new control mappings
    *   Remove existing mappings
    *   Edit MIDI settings for any mapping
    *   Recalibrate axes or toggle reverse
    *   Change the default MIDI channel
    *   Save changes to a new or existing file
5.  **Monitoring:** Once configured (or loaded), the application will monitor the selected inputs and send MIDI messages accordingly.
    *   On Windows, close the console window to exit.
    *   On Linux, press `Enter` to exit.

## License

This project is licensed under the [MIT License](LICENSE).
