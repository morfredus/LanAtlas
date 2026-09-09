# LanAtlas

*Read in another language: **English** (this document) · [Français](README.fr.md).*

[![Version](https://img.shields.io/badge/version-0.7.4-blue)](CHANGELOG.md)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus)
![Qt](https://img.shields.io/badge/Qt-6-41CD52?logo=qt)
![License](https://img.shields.io/badge/License-GPL--3.0--only-blue)

**LanAtlas maps a home LAN**: the Orange Livebox, TP-Link Deco X50 mesh nodes, and every host hanging off them. It is a native desktop application (Windows and Linux), not firmware.

GatewayLab remains the ESP32 probe. LanAtlas does not replace it and does not read its internals. It uses the same observation idea with a PC-class scanner plus the vendor APIs of the box and the mesh.

## Responsibility

LanAtlas **maps** the local network and **attaches** each device to the Livebox or Deco node that carries it.

## What a scan collects

- Local interface, CIDR, assumed gateway
- ICMP sweep + ARP table (IP, MAC)
- SSDP/UPnP, mDNS, NetBIOS names
- TCP ports on discovered hosts
- Orange Livebox sysbus hosts (needs admin password)
- TP-Link Deco mesh (needs the **same password as the Deco / TP-Link app**, plus the main node IP if it sits behind the Livebox)
- OUI vendor lookup and French ISP heuristics
- Tuya smart devices (bulbs, LED strips, plugs) via their LAN broadcast (UDP
  6666/6667): vendor, product and protocol version, no cloud needed

A **"Piloter"** panel appears next to the dossier for a controllable device
(Tuya today): power, brightness, white temperature and colour. Control uses the
3.5 LAN protocol (AES-GCM session on TCP 6668) and needs the device's local key
(obtained once from the Tuya cloud, then stored in the settings, never in git).

Passwords stay in the user settings store (`QSettings`), never in the git tree.

The first screen is a **map of what is present now** (full width). Inventory and
history have their own tabs. Historical hosts can show on the map as dashed
cards. Scan progress percent is printed next to the bar, not on the dark fill.

A second tab, **Parc morfSystem**, listens to UDP 45454 and groups morfBeacon services by host. No extra scan: they announce themselves.

On the map, filter the inventory, read named ports as hints, overlay morfSystem apps after a beacon or a qualifying `/status`. After a scan, LanAtlas queries OUI databases (maclookup.app, macvendors.com) and the WAN via ip-api.com for facts the LAN does not provide.

## Build

Qt 6, CMake 3.21+, Ninja.

```
cmake --preset mingw          # Windows
cmake --build --preset mingw

cmake --preset linux          # Linux x86_64
cmake --build --preset linux
```

Copy `data/oui.json` next to the binary (CMake does this after the link).

## morfSystem

HTTP port **8883** (`/status`, `/healthz`). Capability `network_map`. Heartbeat on UDP 45454. The app works alone; morfBeacon consumers are optional.

## License

GPL-3.0-only. Author: morfredus.
