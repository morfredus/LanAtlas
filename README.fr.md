# LanAtlas

*Lire dans une autre langue : [English](README.md) · **Français** (ce document).*

[![Version](https://img.shields.io/badge/version-0.7.7-blue)](CHANGELOG.md)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus)
![Qt](https://img.shields.io/badge/Qt-6-41CD52?logo=qt)
![License](https://img.shields.io/badge/License-GPL--3.0--only-blue)

J'ai voulu voir, depuis le PC, **qui est accroche a la Livebox et a quel Deco X50**. GatewayLab sur ESP32 m'avait deja appris les protocoles. Il reste utile. Il n'est pas assez a l'aise pour une cartographie complete : memoire courte, pas d'API Orange, pas d'API mesh TP-Link.

LanAtlas est l'application de bureau Windows et Linux qui porte cette responsabilite.

## Responsabilite

LanAtlas **cartographie** le reseau local et **rattache** chaque equipement a la Livebox ou au nœud Deco qui le porte.

## Usage

1. Compiler (presets `mingw` ou `linux`).
2. Ouvrir Parametres : mot de passe admin Livebox, et le **mot de passe du compte
   TP-Link / appli Deco** (pas un second admin). Si le Deco est derriere la
   Livebox, indiquer l'IP du nœud principal (souvent 192.168.1.x).
3. Scanner.

Les secrets restent dans le magasin de l'utilisateur, pas dans le depot.

L'ecran d'accueil est une **carte pleine largeur** (cartouches courts).
L'inventaire et l'historique ont leurs onglets. Les absents peuvent apparaitre
sous la carte, en pointille. Le pourcentage de scan est a cote de la barre,
pas sur le fond vert.

L'onglet **Parc morfSystem** ecoute UDP 45454 et groupe les services par hote. Pas de scan : ils s'annoncent.

De la meme facon, LanAtlas ecoute en continu les **appareils Tuya** (ampoules, bandes LED, prises) qui diffusent leur presence sur le LAN (UDP 6666/6667). Ils sont etiquetes sur la carte (fabricant Tuya, objet connecte, version de protocole) sans passer par le cloud.

Pour un appareil dont on sait parler, un panneau **Piloter** apparait a cote du dossier : marche/arret, luminosite, temperature de blanc et couleur. Le pilotage utilise le protocole LAN 3.5 (session chiffree AES-GCM sur TCP 6668) et demande la **local key** de l'appareil, absente du LAN : on l'obtient une fois via le cloud Tuya (assistant tinytuya) et on la colle dans le panneau. Elle est rangee dans les reglages, jamais dans le depot.

Sur la carte : filtre, ports nommes, capacites, recoupement avec les apps morfSystem du meme hote. Apres un scan, LanAtlas interroge des bases OUI (maclookup.app, macvendors.com) et ip-api pour le WAN : ce que le LAN ne dit pas.

Port HTTP reserve : **8883**. Capacite beacon : `network_map`.

## Licence

GPL-3.0-only. Auteur : morfredus.
