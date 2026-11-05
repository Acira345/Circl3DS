[Español](README.md) · **English** · [Polski](README_pl.md) · [Русский](README_ru.md) · [Deutsch](README_de.md)

# Circl3DS 🌸

**Circl3DS** is a high-performance standalone rhythm game and engine for the **Nintendo 3DS, 2DS, New 3DS, and New 2DS** console family, programmed from scratch in C++20 using `libctru`, `Citro2D`, and `Citro3D`, featuring full compatibility with community beatmaps in `.osz` and `.osu` formats.

The game is optimized to run at a rock-solid **60 FPS** even on original (Old 3DS) consoles, taking full advantage of the dual screens, touchscreen stylus, and **real stereoscopic 3D depth**.

---

### 📲 Quick Install (.CIA)

Download or transfer `circl3ds.cia` and install it via the **FBI** app on your console (`Remote Install` or SD card).

> *Requires Custom Firmware (Luma3DS).*

---

## ✨ Key Features

### 🎮 The 4 Rhythm Game Modes
1. **Standard (Circles):**
   - **Direct Touch Mode:** Tap circles, trace sliders, and spin spinners directly on the touchscreen with the stylus.
   - **Virtual Tablet Mode:** Look at the top screen while using the touchscreen as an active digitizer tablet.
   - GPU-rendered curved Bézier sliders, smooth snaking sliders, precise approach circles, and spinners.
2. **Taiko (Drums):**
   - Dynamic note stream featuring Don (red), Katsu (blue), Drumrolls (yellow), and Denden spinners.
   - Animated mascot that dances in sync with the song's BPM.
   - 4-zone interactive drum on the touchscreen alongside full physical button support (D-Pad, ABXY, L/R bumpers).
3. **Catch (Fruit Catch):**
   - Catch fruits, droplets, and bananas using the catcher and plate.
   - Visual **Hyperdash** effect with red streaks and speed acceleration.
   - Analog Circle Pad, D-Pad, or touch button controls.
4. **Mania (Piano/Keys):**
   - 4K and 7K column modes with column lighting and hit burst effects.
   - 3D meshes for hold notes.
   - On-the-fly **Scroll Speed adjustment** with the D-Pad or shoulder buttons.

---

### 👓 Real Stereoscopic 3D Effect
- Take advantage of the **3D Depth Slider** on the top screen of the Nintendo 3DS.
- **Dynamic Layered Depth:** Hit objects travel from the depths of the 3D playfield toward the player, while judgement popups (`300`, `100`, `50`, `Miss`) float in front of the screen.

### 🎬 Background Video Playback
- Smooth background video playback optimized for 3DS hardware, synchronized to the millisecond with the audio track.
- Includes the `convert_videos_3ds.ps1` helper script to encode videos.

### ⌨️ QWERTY Touch Keyboard & Live Filtering
- Interactive touch search keyboard on the bottom screen that filters your song library **in real-time as you type**, without pausing or freezing.
- **Quick filter chips:** Filter instantly by mode (`[STD]`, `[TAIKO]`, `[CATCH]`, `[MANIA]`), minimum difficulty (`[*4+]`, `[*5+]`), or reset with `[ALL]`.

### ⚡ 15 Gameplay Modifiers (Mods)
- **Difficulty & Speed:** Easy (EZ), NoFail (NF), HalfTime (0.75x HT), HardRock (HR), SuddenDeath (SD), Perfect (PF), DoubleTime (1.5x DT), Nightcore (NC).
- **Visuals & Assistance:** Hidden (HD), Flashlight (FL), SpunOut (SO), Relax (RX), AutoPilot (AP), Auto, and Cinema.

### 📊 Local Records & Performance Points (PP)
- Real-time **PP (Performance Points)** calculation based on star difficulty, accuracy, and mods.
- Automatic local high score persistence on the SD card per song and mode.
- Results screen with detailed judgement breakdown, max combo, strain graphs, and grades (SS, S, A, B, C, D, F).

### 📡 Integrated Wi-Fi Song Transfer
- Built-in HTTP server: upload `.osz` packages or folders straight from your computer or smartphone web browser without removing your SD card.

---

## 📥 Installation

### Requirements
- A Nintendo 3DS / 2DS console with Custom Firmware (Luma3DS).
- SD card with free space for songs.

### Installation Methods:
1. **CIA Format (Recommended):**
   - Copy `circl3ds.cia` to your SD card and install it with **FBI**.
   - It will appear directly on your HOME Menu with its custom 3D banner and icon.
2. **3DSX Format (Homebrew Launcher):**
   - Copy `circl3ds.3dsx` and `circl3ds.smdh` to `sdmc:/3ds/circl3ds/`.
   - Launch it from the **Homebrew Launcher**.

---

## 🎵 Adding Songs (Beatmaps)

Place your songs in `.osz` format or as extracted folders in any of these SD paths:
- `sdmc:/3ds/circl3ds/songs/`
- `sdmc:/circl3ds/songs/` *(legacy paths like `sdmc:/3ds/osu-3ds/songs/` remain supported for backward compatibility)*

> **Note:** Upon launch, any `.osz` files placed in these directories are automatically extracted and indexed. You can also press **[Y]** in the song select screen to enable the Wi-Fi server and upload them via web browser.

---

## 🕹️ Controls

| Button | Action |
| :--- | :--- |
| **Stylus / Touchscreen** | Hit circles/sliders, navigate menus, use search keyboard |
| **A / Touch** | Select song / Start gameplay |
| **B** | Back / Close search |
| **D-Pad Up / Down** | Browse song list |
| **D-Pad Left / Right** | Switch difficulty |
| **SELECT** | Open/Close Mods overlay |
| **START** | Change sorting criteria (Title, Artist, Stars, PP) |
| **X** | Settings & Audio Offset Calibration |
| **Y** | Activate Wi-Fi transfer server |
| **ZL / ZR** | Open search keyboard |

---

## 🛠️ Building from Source

Requires **devkitARM** and **libctru**.

```bash
# Clone the repository
git clone https://github.com/Acira345/Circl3DS.git
cd Circl3DS

# Build binaries (.3dsx, .elf, .cia)
make -j4
```

---

## ⚖️ Disclaimer & Credits
- **Circl3DS** is an independent open-source rhythm game for the Nintendo 3DS homebrew community. It is not affiliated with, endorsed by, or connected to ppy Pty Ltd or Dean Herbert.
- Compatible with open community beatmap formats (`.osz` / `.osu`).
- Homebrew libraries: *devkitPro*, *libctru*, *Citro2D*, *Citro3D*, *bannertool*, *makerom*.
