# FAVIO Client

FAVIO is a modded client for Minecraft Bedrock Edition (MCBE), forked from
[Flarial's open-source base](https://github.com/flarialmc/dll-oss) and rebranded.
It enhances gameplay with performance tools, HUD modules, and quality-of-life
features for a smoother gaming experience.

## How this fork differs from upstream

- **Name/brand**: FAVIO everywhere user-visible (window title, dialogs, chat
  `[FAVIO]` watermark, config folder `%appdata%/../RoamingState\FAVIO`,
  binary `FAVIO.dll`, version `1.00`).
- **Privacy defaults**: `apiusage` and `promotions` default to **off**, and the
  60s heartbeat only runs when API usage is enabled. API endpoints point at
  `https://api.favmc.net` (no backend yet — API-backed features such as TabList
  role priority and online-user watermarks stay dormant until one exists).
- **v1 default modules**: FPS, CPS, Coordinates, Keystrokes, Ping, Toggle
  Sprint, Toggle Sneak, AutoGG, Zoom, TabList and Debug Menu are enabled on
  first run. Everything else stays off until toggled.
- **Links**: website `https://favmc.net`, Discord
  `https://discord.gg/PSp3WXFEse`.
- Internal `FlarialGUI::` engine identifiers and `IDR_*` resource IDs are
  intentionally unchanged (renaming them adds compile risk with no user benefit).

## Supported Platforms

- **Operating System:** Windows 10/11 (64-bit processors only)
- **Minecraft Version:** Compatible with MCBE versions 1.20 and above.

## Troubleshooting

If you encounter issues, join our community on
[Discord](https://discord.gg/PSp3WXFEse). We're here to help!

---
## Build Guide

### Prerequisites:
1. **CMake**: Ensure CMake is installed on your system. You can download it from [cmake.org](https://cmake.org/download/) and follow the installation instructions for your operating system.

2. **Git**: Install Git if you haven't already. You can download Git from [git-scm.com](https://git-scm.com/downloads) and follow the installation instructions.

3. **MSVC**: Ensure the MSVC compiler is installed on your system (**Clang would not work). You can download it from [the Visual Studio downloads page](https://visualstudio.microsoft.com/downloads).

### Steps to Clone and Build the Project:

Method 1 - You can open this project in CLion or Visual Studio, and make sure to use the MSVC toolchain, preferably combined with Ninja. If you don't know what any of this means, please do some research on this matter.
Method 2 - You can use **build.bat** file as well.

By following these steps, you should be able to clone and build the project using CMake successfully.

**CI note**: `.github/workflows/build-latest.yml` uploads release builds to
`FAVXXYT/favio-cdn` (create that repo and set the `ACTIONS_GITHUB_TOKEN`
secret before expecting green CDN uploads). Debug/commit builds only need the
standard `GITHUB_TOKEN`.

---
## Acknowledgements

This application incorporates various icons provided by Flaticon ([www.flaticon.com](http://www.flaticon.com)). We appreciate their valuable resources.

Upstream project: [Flarial Client](https://flarial.xyz/) by
[flarialmc](https://github.com/flarialmc) (AGPL-3.0 — see `LICENSE`).
