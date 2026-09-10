# Changelog

## [0.7.11] - 2026-09-10

### Changed

- Resynced vendored morfUpdate to 0.8.0 (opt-in self-update, stage 2). No behaviour
  change in this application: the affected code is the update agent, which desktop
  apps do not run.

## [0.7.10] - 2026-09-10

### Changed

- Resynced vendored morfUpdate to 0.7.0 (self-update state contract, stage 1, plus
  a Windows journal-rewrite fix). No behaviour change in this application: the
  affected code is the update agent, which desktop apps do not run.

## [0.7.9] - 2026-09-10

### Fixed — update dialog offered the checksums file instead of the binary

- Resynced vendored morfUpdate to 0.6.0. The "Check for updates" dialog now picks
  the release asset matching the running OS and CPU architecture (the Windows
  `.zip`, the arch-matched Linux `.deb`/`.AppImage`) instead of the first asset,
  which was often `checksums.sha256`. No API change.

## [0.7.8] - 2026-09-10

### Fixed — arm64 `.deb` now cross-built from WSL, not only on the Pi

- `package-deb.sh` now detects an aarch64 binary on a non-aarch64 host (a cross
  build, `build-arm64-cross/`) and, in that case, labels the package `arm64` and
  resolves its `Depends` from the sysroot's `.shlibs` (via the vendored morfdeploy
  `cross_depends`) instead of the host's `dpkg`/`ldd`, which are blind to an arm64
  ELF. Combined with the parc tooling fix (morfTools 0.35.12), `publish-releases
  --with-arm64-cross` on an x86_64 WSL host now produces the full Linux set — amd64,
  arm64 and AppImage — so the arm64 `.deb` no longer depends on a build on the Pi.
  It uses the `linux-arm64-cross` preset added in 0.7.7.

## [0.7.7] - 2026-09-09

### Fixed — arm64 `.deb` missing from every release

- Added the `linux-arm64-cross` CMake preset (configure + build), aligned on the
  vendored toolchain `third_party/morf/morfdeploy/cmake/linux-aarch64.cmake` and
  the QEMU sysroot (`MORF_SYSROOT`), exactly as ComponentHub already declares it.
  Without this preset, `publish-releases --with-arm64-cross` on an x86_64 WSL host
  had nothing to cross-build, so no arm64 binary was produced and every release
  shipped without its `lanatlas-<version>-linux-arm64.deb`. The `linux-arm64-deb`
  target was declared in 0.7.5, but its cross preset was never added; this closes
  that gap. The rest was already at parity (the `package-deb.sh` build search
  covers `build-arm64-cross`, vendored morfdeploy 0.20.6, same toolchain).

## [0.7.6] - 2026-09-09

### Fixed

- **Build on Qt < 6.5** (the system Qt 6.4 of Debian / WSL / Raspberry Pi OS).
  `main.cpp` detected the dark theme with `QStyleHints::colorScheme()` /
  `Qt::ColorScheme`, both added in Qt 6.5, so the native Linux and arm64 builds
  failed to compile (Windows, on Qt 6.9, was fine). The detection is now guarded
  by a Qt version check with a portable fallback (window-colour lightness of the
  palette). No behaviour change on Qt 6.5+.

## [0.7.5] - 2026-09-09

### Changed — full release-packaging parity with the parc apps

- LanAtlas now declares the same binary target set as the other desktop apps
  (ComponentHub, PhotoHub...): `linux-amd64-deb`, `linux-arm64-deb`,
  `linux-amd64-appimage`, `windows-x86_64-zip`. The parc release chain
  (`package-all.py`) builds each on its native machine, so a published release is
  complete on every platform.
- Rewrote `scripts/linux/package-deb.sh` on the parc reference: it now searches
  `build-arm64/`, installs a `.desktop` entry and hicolor icons, auto-detects the
  Qt runtime dependencies (plus `libxcb-cursor0`), and bundles `data/oui.json`
  next to the binary so vendor lookup works once installed. Added
  `scripts/linux/package-appimage.sh` (linuxdeploy + Qt plugin) and
  `scripts/linux/lanatlas.desktop`. No change to the application itself.

## [0.7.4] - 2026-09-09

### Changed — join the morfSystem work ecosystem

- LanAtlas is now a declared **application** of the morfSystem ecosystem (in
  morfTools `ecosystem.json`): port 8883 reserved in the app range (8880-8899),
  listed among the vendored consumers. It stays a standalone desktop app with no
  runtime dependency on morfSystem; it is not a parc service (no systemd unit, no
  required heartbeat or permanent port).
- Resynced the vendored copies to the canonical sources: morfBeacon 0.7.1,
  morfUpdate 0.5.3, morfdeploy 0.20.6. `scripts/sync-morf.{sh,ps1}` now also
  refreshes morfdeploy (it only handled beacon and update before). No functional
  change to the app.
- Restored the executable bit on the shipped scripts so a fresh clone on Linux
  can run them.

## [0.7.3] - 2026-09-09

### Changed — settings as a plain file in the standard app folder

- Settings (Livebox/Deco credentials, scan options, Tuya local keys) are now an
  INI file in the same per-user app folder as the session state, instead of the
  Windows registry:
  - Windows: `%APPDATA%\morfredus\LanAtlas\settings.ini`
  - Linux: `~/.local/share/morfredus/LanAtlas/settings.ini`
- One-time **migration** copies existing settings from the old native store
  (registry / legacy `.conf`) into the new file, then clears the old store so no
  secret is left behind. Nothing is lost on upgrade.

## [0.7.2] - 2026-09-09

### Fixed

- "Piloter" panel no longer loops ("Communication..." then the state, over and
  over). A map refresh (Tuya broadcasts, web enrichment) re-selected the row and
  re-triggered a network read each time. `setDevice` is now idempotent: it reads
  the device only when the selected device actually changes.

## [0.7.1] - 2026-09-09

### Changed — editable local keys, no more dead end

- Tuya local keys are now managed in **Settings > "Pilotage - local keys Tuya"**:
  one editable row per device (id + key), changeable or removable at any time.
- The "Piloter" panel keeps an **inline key entry** for the first setup, and now
  re-shows it (plus a shortcut to Settings) after a failure. A wrong key made the
  device silently drop the encrypted handshake, so the read timed out with no way
  to fix it; the key can now always be corrected.

## [0.7.0] - 2026-09-09

### Added — control Tuya smart devices ("Piloter")

- LanAtlas moves from map-only to **map + remote control**: a "Piloter" panel
  appears for a controllable device (Tuya today) next to its dossier.
  Power, brightness, white temperature, and colour (HSV) for the LED strip.
- **Full Tuya 3.5 LAN control protocol** in C++, no Python dependency, validated
  against the real device on the LAN:
  - `AesGcm::encrypt` added next to decrypt (CTR + GHASH).
  - `TuyaClient` (`src/control`): TCP 6668 session-key negotiation (16-byte
    nonces, HMAC-SHA256, GCM-derived session key), 6699 frame pack/unpack,
    `queryStatus()` and `setDps()`. Control commands carry the required 3.5
    version header and preserve JSON key order (the firmware silently ignores
    a re-ordered payload); a short connection per action avoids the
    incremental-status stream a persistent connection emits.
- **Generic `IDeviceDriver` interface** + `TuyaDriver` (datapoint mapping:
  20 power, 21 mode, 22 brightness, 23 temperature, 24 colour hex) so other
  brands can be added later without touching the UI. `TuyaController` runs the
  blocking calls in a worker thread.
- Per-device **local keys** stored in the settings (`QSettings`), never in git.
  The key is not on the LAN; it is obtained once from the Tuya cloud.

## [0.6.0] - 2026-09-09

### Added — Tuya smart devices on the map

- **Passive Tuya discovery**: a permanent listener binds UDP 6666 and 6667 and
  reads the periodic broadcast every Tuya device (bulbs, LED strips, plugs...)
  emits by itself. No extra scan step: like the morfBeacon tab, they announce
  themselves. Both frame formats are handled: the older `55aa` (protocols 3.1
  plaintext / 3.2-3.4 AES-ECB) and the newer `6699` (protocol 3.5, AES-GCM).
- Heard devices are overlaid on the inventory by IP (same idea as morfSystem
  services): vendor `Tuya`, category `iot`, `gwId`, product key and protocol
  version, capability `tuya-lan`, source `tuya`. A device seen only through the
  broadcast is added to the map on its own.
- **AES-128-ECB and AES-128-GCM** added next to the existing CBC core (no
  OpenSSL), reusing the same AES block cipher. They decrypt the announce with
  Tuya's fixed public discovery key, and will serve the control protocol
  (3.3 ECB, 3.5 GCM) in the next release.

### Notes

- This release only **identifies** Tuya devices. Controlling them (on/off,
  brightness, colour on TCP 6668) needs each device's per-device local key,
  which is not on the LAN — it comes from the Tuya cloud. That "Piloter"
  feature lands in a following version.

## [0.5.0] - 2026-08-26

### Added — detection plus precise

- **Description UPnP/SSDP** : on suit le `LOCATION` et on lit le XML
  (`friendlyName`, `modelName`, `manufacturer`) - vrais noms des TV, box,
  imprimantes, NAS, lecteurs.
- **mDNS/Bonjour approfondi** : parseur DNS complet (compression) -> types de
  service (AirPlay, Chromecast, imprimante, HomeKit...), hostname et TXT
  `model=`/`md=`. Deduction du role et du modele, y compris codes Apple.
- **Empreinte HTTP** : `<title>` de `/` et en-tete `Server:` pour les hotes web
  sans modele (admin routeur, imprimante, camera, NAS).
- **MAC aleatoire** signalee (bit localement administre) au lieu d'un fabricant
  vide - typique des mobiles.
- **Cache disque OUI -> fabricant** : plus de requete web repetee a chaque scan
  (`oui-cache.json`).

### Added — experience utilisateur

- **Renommer** un appareil (nom personnalise persistant, `labels.json`) via le
  bouton, le menu contextuel ou double-clic.
- **Badges nouveau / disparu** depuis le dernier scan (fond vert / rouge).
- **Pictogramme de type** par ligne (telephone, PC, TV, imprimante, camera...).
- **Carte interactive** : double-clic sur une carte -> fiche dans l'Inventaire ;
  **export PNG** de la carte.
- **Rescan automatique** (option, toutes les 5 min) dans le menu Scan.

## [0.4.3] - 2026-08-26

### Added

- Boutons de l'Inventaire (Copier IP/MAC, Ping, HTTP, Completer) : petit retour
  visuel a l'endroit du clic (IP copiee, pas d'IP, maj en cours, deja
  complete...), pour confirmer ou non l'action.

## [0.4.2] - 2026-08-26

### Fixed

- Panneau de detail (bas de l'Inventaire) : la position de lecture est
  conservee lors d'une actualisation. Le texte n'est reecrit que s'il a change
  et le defilement reste ou il etait (plus de retour en haut ni de clignotement).

## [0.4.1] - 2026-08-26

### Fixed

- Inventaire : la ligne selectionnee est conservee lors d'une actualisation.
  L'onglet Historique partageait la meme variable de selection et l'ecrasait ;
  les deux onglets ont desormais leur propre selection.

## [0.4.0] - 2026-08-26

### Added

- **Persistance de session** : la carte/l'inventaire sont sauvegardes a la fin
  d'un scan et a la fermeture, puis recharges au demarrage. On retrouve la carte
  de la session precedente immediatement, sans re-scanner.
- **Deux modes de scan** : « Scan complet » (repart de zero) et
  « Rescan (mise a jour) » qui garde l'existant, met a jour ce qui est revu et
  bascule les absents en historique - sans recadrer la carte.

### Fixed

- Inventaire : lors d'une actualisation, la vue ne bouge plus du tout - la
  position de l'ascenseur reste exactement la ou elle est, sans saut vers la
  ligne selectionnee.

## [0.3.1] - 2026-08-26

### Fixed

- Inventaire : la ligne selectionnee ne saute plus hors de l'ecran a chaque
  actualisation (scan, enrichissement web). Position de defilement et focus du
  tableau conserves.

### Added

- Modele des clients Deco deduit du `client_type` (Telephone, Tablette, TV,
  Camera, Enceinte...) : utile surtout pour les mobiles a MAC aleatoire dont le
  fabricant est inconnu.
- Carte : legende des couleurs (Deco principal / repeteur / Livebox / client /
  historique).

### Changed

- Tableaux : lignes alternees plus lisibles (fond creme).

## [0.3.0] - 2026-08-26

### Added

- Numero de version affiche dans le titre de la fenetre et menu **Aide**
  (« A propos », « Rechercher les mises a jour » via morfUpdate/GitHub).
- Logo et icone LanAtlas (tuile teal + maillage mesh), decline en icone
  Windows multi-taille (16..256). Source : `resources/lanatlas.svg`.
- Carte : barre d'outils **Ajuster / 100 % / +/-** et filtre de **liaison**
  (Toutes / Wi-Fi / Filaire).

### Fixed

- **Rattachement Deco** : les clients sont enfin mappes sous leur nœud X50. La
  liste globale (`device_mac="default"`) renvoie un `owner_id` vide ; on
  interroge desormais `client_list` **par nœud** (`device_mac` = mac du nœud),
  l'attribution venant de la requete. Le nœud principal, qui n'expose pas de
  `device_id`, est rattache par son mac.
- Noms des clients Deco decodes depuis le **base64** (« cGk0ZGV2 » -> « pi4dev »).
- Bandes lisibles : `band5` -> « Wi-Fi 5 GHz », `band2_4` -> « Wi-Fi 2.4 GHz ».

### Changed

- Carte : rendu par defaut a l'echelle **1:1 lisible** (fini le « fit » qui
  ecrasait tout a une taille illisible ; l'ajustement est un bouton).
- Carte : **couloir teinte** derriere chaque Deco pour voir d'un coup d'œil
  quels clients lui sont rattaches.

## [0.2.11] - 2026-08-26

### Fixed

- Deco mapping post-login : la signature n'inclut la cle AES (`k=`/`i=`) qu'au
  login. Les requetes `device_list`/`client_list` ne signent que `h=&s=`, comme
  la web UI (getSignature). Renvoyer `k=&i=` partout laissait le mapping vide
  apres un login pourtant reussi.

### Notes

- Rappel : le mot de passe Deco a coller dans Parametres est celui du compte
  TP-Link ID (celui qui ouvre tplinkdeco.net). Un `error_code=-5002` avec
  tentatives qui decrementent = mot de passe refuse, pas un defaut de chiffrement.

## [0.2.10] - 2026-08-25

### Fixed

- Deco `error_code=-5002`: AES session key must be 16 decimal digits (web UI /
  ha-tplink-deco), not hex. Follow HTTP 307 to HTTPS instead of staying on
  http (that produced empty luci keys). HTTPS is tried first.

## [0.2.9] - 2026-08-25

### Fixed

- Deco X50 login 403: the web UI uses AES-128-CBC + RSA signed `sign`/`data`,
  not a plain JSON password. HTTP 307 is kept on http (the "Not secure"
  https://tplinkdeco.net in Chrome is the local cert, not CWMP).

## [0.2.8] - 2026-08-25

### Fixed

- History tab empty while Livebox reported archived hosts: an offline lease
  often has no IP, and empty IP was classified as "technical". Those rows
  now go to Historique. Deco login retries as form POST after HTTP 403.

## [0.2.7] - 2026-08-25

### Fixed

- Deco luci keys: `result.password` is a `[modulus, exponent]` array (HTTP 200
  looked like "no RSA keys"). Also POST `operation=read` as form data, like
  the web UI.
- History: do not merge two different MACs that share a DHCP IP. Livebox
  `Active: false` wins over a leftover ping.

## [0.2.6] - 2026-08-25

### Fixed

- Deco luci: follow HTTP 307 to HTTPS on the **node IP**, ignore the internal
  TLS certificate (invalid signature). Login is tried on http then https.
- History empty: ARP is no longer a presence proof; Livebox `Hosts.getDevices`
  is merged with `Devices.get` so archived leases can appear.

## [0.2.5] - 2026-08-25

### Fixed

- Deco login URL no longer doubles the luci `stok` (empty device/client lists
  after a successful login). Clients keep `owner_id` as parent, not as their
  own id. Settings: the Deco field is the **TP-Link / Deco app password**.

### Changed

- Journal logs each Deco IP tried and the luci error. More parent keys for
  attaching clients to a repeater.

## [0.2.4] - 2026-08-25

### Fixed

- History stayed empty: a stale ARP entry counted as "online" even when the
  Livebox said `Active: false`. ARP no longer overrides an API "absent".
  Livebox/Deco flags accept 0/1 and "true"/"false" strings, not only JSON
  booleans. Deco clients no longer default to online when the field is missing.

## [0.2.3] - 2026-08-25

### Changed

- Scan percent sits beside the bar in dark ink (the green chunk no longer hides
  the digits). Chunk fill is honey on cream.
- Map is full-width: compact cards (name, IP, link), larger type. History is
  on by default under the live topology (dashed, grey). Uncheck to hide.
- New tabs: **Inventaire** (listing + dossier, ports and morf proof columns)
  and **Historique** (absents only, last seen first).

## [0.2.2] - 2026-08-25

### Changed

- A known TCP port is a candidate, not an identity. morfSystem is tagged only
  after a `morfbeacon/1` heartbeat on that IP, or a `GET /status` JSON that
  matches the morfSystem contract (`app`, `version`, `state`, plus host/role and
  uptime/metrics/api). `GET /healthz` `{"status":"ok"}` confirms, it never
  qualifies alone. Not by 8883/8888/8080, not by a bare HTTP 200. Overlay
  matches the datagram source IP, not the hostname.
- TCP probe runs after Livebox/Deco so new hosts can be qualified. HTTP
  qualify hits ports 8787-8899 and 8888 only.
- Inventory splits **connected** vs **historical** (Livebox `Active`, ARP, ICMP,
  discovery, TCP, beacon). The map shows connected hosts. Technical addresses
  (broadcast, multicast, `.0`/`.255`) are out of the default list.
- Explicit filters: presence, parent (including unattached), search. Column
  sort. Default: connected, then parent, then name; history by last seen.
- Map cards clip and elide; morf line is `morf: N service(s)`.

## [0.2.1] - 2026-08-25

### Fixed

- Map zoom/pan and inventory selection survive beacon refreshes.
- morfSystem rows in error/warn are red/amber again (readable on the honey
  selection).

### Changed

- After a scan, LanAtlas queries public databases: maclookup.app then
  macvendors.com for OUI/company, ip-api.com for the WAN (ISP/country). Results
  land in the device dossier (`web_maclookup`, `web_wan`). Not a web search
  engine.

## [0.2.0] - 2026-08-25

### Changed

- Selection highlight is honey on dark ink (readable). UI keeps dark text even
  on a dark OS theme.

### Added

- Search/filter on the inventory, filter by parent node, stats strip.
- Named TCP ports, morfSystem ports in the deep scan, capabilities on devices
  (LAN services + morfBeacon overlay by IP/hostname).
- Context menu and buttons: copy IP/MAC, ping, open HTTP, DuckDuckGo, MAC/OUI
  lookup (maclookup.app). CSV export. Ctrl+F.
- morfSystem tab: filter, `/healthz`, no coloured-on-coloured selection.

## [0.1.5] - 2026-08-25

### Added

- Tab "Parc morfSystem": listens to morfBeacon UDP 45454 and lists services
  grouped by host. Selecting a service shows the heartbeat and fetches `/status`.

## [0.1.4] - 2026-08-25

### Fixed

- Scan stuck at 94%: reverse DNS (`QHostInfo::fromName`) has no timeout and
  Windows waits seconds per host with no PTR. That step is skipped; names come
  from Livebox, Deco, mDNS and NetBIOS.

## [0.1.3] - 2026-08-25

### Changed

- Map layout for a Livebox plus three Deco X50: gateway on top, three equal
  repeater columns underneath, clients stacked under the node that owns them.
  Empty "unattached" column is omitted. Map fits in the pane (Ctrl+wheel zoom).

## [0.1.2] - 2026-08-25

### Changed

- Topology is a column per Livebox/Deco node: devices sit under the node that
  carries them. Cards use dark ink on paper (readable on a dark OS theme).
- First screen is a split map + full inventory table + device dossier (every
  API field kept in `extra`).
- Display names never stay empty (hostname, vendor/model, then MAC tail / IP).
- Parent merge: a Deco attachment wins over a generic Livebox parent. Relink
  after the mesh query.

## [0.1.1] - 2026-08-25

### Fixed

- Scan appeared frozen at 70%: TCP probes were sequential and a failed
  `QTcpSocket` destructor could block for seconds. Probes now run in parallel,
  always `abort()`, report `70-77%`, and reverse DNS is capped at 8 hosts.

## [0.1.0] - 2026-08-24

### Added

- First desktop release (Qt 6, Windows and Linux): ARP/ICMP map, SSDP, mDNS,
  NetBIOS, TCP ports, Orange Livebox sysbus adapter, TP-Link Deco mesh adapter,
  topology view, JSON export, morfBeacon `/status` + `/healthz` on port 8883.
