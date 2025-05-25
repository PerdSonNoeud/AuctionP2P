# AuctionP2P - Système d'enchères pair-à-pair

Un système d'enchères distribué implémenté en C utilisant IPv6 et les protocoles UDP/TCP pour la communication peer-to-peer.

## 📋 Table des matières

- [Aperçu](#-aperçu)
- [Fonctionnalités](#-fonctionnalités)
- [Architecture](#️-architecture)
- [Prérequis](#-prérequis)
- [Installation](#-installation)
- [Utilisation](#-utilisation)
- [Protocole de communication](#-protocole-de-communication)
- [Structure du projet](#-structure-du-projet)

## 🎯 Aperçu

AuctionP2P est un système d'enchères décentralisé où chaque participant (pair) peut :

- Rejoindre un réseau P2P existant ou créer un nouveau réseau
- Créer des enchères avec un prix initial
- Participer aux enchères en proposant des offres
- Superviser le processus de validation des enchères

Le système utilise IPv6 pour la communication réseau et implémente un protocole de consensus pour assurer la cohérence des données entre tous les pairs.

## ✨ Fonctionnalités

### Fonctionnalités implémentées

- ✅ **Gestion des pairs** : Connexion/déconnexion au réseau P2P
- ✅ **Communication multicast** : Découverte de réseau et annonces
- ✅ **Communication unicast** : Échanges directs entre pairs
- ✅ **Gestion des IDs** : Attribution d'identifiants uniques
- ✅ **Système d'enchères basique** : Création et participation aux enchères
- ✅ **Support IPv6** : Communication moderne sur réseau

## 🏗️ Architecture

Le système utilise une architecture modulaire :

```bash

┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Application   │    │   Enchères      │    │   Pairs P2P     │
│   (main.c)      │◄──►│   (auction.c)   │◄──►│   (pairs.c)     │
└─────────────────┘    └─────────────────┘    └─────────────────┘
         │                       │                       │
         └───────────────────────┼───────────────────────┘
                                 ▼
                    ┌─────────────────┐    ┌─────────────────┐
                    │   Messages      │    │   Sockets       │
                    │   (message.c)   │◄──►│   (sockets.c)   │
                    └─────────────────┘    └─────────────────┘
                                 ▲
                    ┌─────────────────┐
                    │   Utilitaires   │
                    │   (utils.c)     │
                    └─────────────────┘
```

### Adresses réseau

- **Adresse de liaison** : `ff12::` port `8080` (découverte de pairs)
- **Adresse d'enchères** : `ff12::` port `8081` (communications d'enchères)
- **Adresses personnelles** : IPv6 + port TCP/UDP par pair

## 🔧 Prérequis

- **Système d'exploitation** : Linux (testé sur Ubuntu/Debian)
- **Compilateur** : GCC avec support C17
- **Bibliothèques** :
  - pthread (threads POSIX)
  - Bibliothèques réseau standard (socket, netinet, arpa)
- **Interface réseau** : `eth0` (modifiable dans le code)

## 📦 Installation

1. **Cloner le projet**

```bash
git clone https://moule.informatique.univ-paris-diderot.fr/jinc/projet-reseau-2024-2025.git
cd projet-reseau-2024-2025
```

2.  **Compiler le projet**

```bash
make clean
make
```

3. **Exécuter le programme**

```bash
make run
# ou directement
./bin/AuctionP2P
```

## 🚀 Utilisation

### Démarrage

1. Lancez le programme sur plusieurs machines du réseau
2. Le premier utilisateur créera automatiquement un nouveau réseau P2P
3. Les suivants rejoindront le réseau existant

### Interface utilisateur

```bash
===== Bienvenue dans le système P2P =====

Recherche de système P2P existant...
  Entrez votre ID souhaité (laissez vide pour défaut 1): 42
  Tentative de connexion avec ID=42...

Commandes disponibles :
  1 - Créer une enchère
  2 - Faire une offre
  3 - Afficher les enchères actives
  q - Quitter le programme
```

### Exemple d'utilisation

```bash
# Terminal 1 (Premier pair - crée le réseau)
./bin/AuctionP2P
> Réseau P2P non trouvé, création d\'un nouveau réseau...

# Terminal 2 (Deuxième pair - rejoint le réseau)
./bin/AuctionP2P
> Réseau P2P trouvé, vous êtes maintenant connecté.
```

## 📡 Protocole de communication

### Codes de messages principaux

| Code | Type | Description |
|------|------|-------------|
| 3 | `CODE_DEMANDE_LIAISON` | Demande pour rejoindre le système |
| 4 | `CODE_REPONSE_LIAISON` | Réponse avec adresse personnelle |
| 5 | `CODE_INFO_PAIR` | Envoi d'informations de pair |
| 6 | `CODE_INFO_PAIR_BROADCAST` | Diffusion d'informations de pair |
| 7 | `CODE_INFO_SYSTEME` | Informations système (enchères + pairs) |
| 8 | `CODE_NOUVELLE_VENTE` | Lancement d'une nouvelle enchère |
| 9 | `CODE_ENCHERE` | Offre d'un pair |
| 13 | `CODE_QUIT_SYSTEME` | Quitter le système |
| 50/51 | `CODE_ID_ACCEPTED/CHANGED` | Validation/changement d'ID |

### Format des messages

```bash
Connexion : CODE=3
Réponse   : CODE=4|ID|IP|PORT
Info pair : CODE=5|ID|IP|PORT|CLE
Système   : CODE=7|ID|IP|PORT|NB|[ID|IP|PORT|CLE]...
```

## 📁 Structure du projet

```bash
projet-reseau-2024-2025/
├── src/
│   ├── main.c              # Point d'entrée principal
│   ├── pairs.c             # Gestion des pairs P2P
│   ├── auction.c           # Système d'enchères
│   ├── message.c           # Structures de messages
│   ├── sockets.c           # Communication réseau
│   ├── utils.c             # Utilitaires (sérialisation)
│   ├── adr.txt             # Formats de messages
│   └── include/
│       ├── pairs.h
│       ├── auction.h
│       ├── message.h
│       ├── sockets.h
│       └── utils.h
├── obj/                    # Fichiers objets compilés
├── bin/                    # Exécutable final
├── Makefile               # Configuration de compilation
└── README.md              # Documentation
```

## 👥 Contributeurs

- **Développeurs principaux** : JIN Cristophe, PIGET Mathéo, MELILA Yanis
- **Contexte** : Projet universitaire L3 Informatique - Programmation Réseau

---
