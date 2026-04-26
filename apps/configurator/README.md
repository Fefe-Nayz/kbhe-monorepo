# KBHE Configurator

Bienvenue sur le dépôt du **Configurateur KBHE**, l'application compagnon officielle et open-source pour les claviers magnétiques équipés du firmware KBHE. 

Cette application de bureau (construite avec Tauri et React) permet aux utilisateurs de personnaliser tous les aspects de leur clavier magnétique en temps réel.

## 🌟 Fonctionnalités Principales

- **Éditeur de Mapping (Keymap)** : Configurez chaque touche, gérez vos calques (layers) et utilisez des fonctions avancées.
- **Paramètres des Switches Magnétiques** :
  - **Calibration** : Calibrez précisément chaque touche magnétique.
  - **Courbes Analogiques & Rapid Trigger** : Ajustez la distance d'activation, la sensibilité et le comportement dynamique des frappes.
  - **Émulation Manette (Gamepad)** : Assignez des axes analogiques à vos touches de clavier.
- **Gestion de l'Éclairage (RGB)** : Personnalisez les couleurs, les effets et les préréglages (presets) de votre clavier.
- **Profils** : Créez, sauvegardez et basculez facilement entre différentes configurations selon vos besoins (jeu, dactylographie, etc.).
- **Performances & Diagnostics** : Surveillez la réactivité du clavier, testez les frappes, et ajustez les paramètres globaux de performance.
- **Mise à jour du Firmware** : Flashez ou mettez à niveau le microprogramme de votre clavier directement depuis l'application.
- **Support des Encodeurs Rotatifs** : Configurez les actions des molettes de votre clavier.

## 🛠️ Stack Technique

Ce projet est conçu pour être rapide, léger et multiplateforme en utilisant des technologies modernes :

- **Frontend** : [React](https://reactjs.org/) + [Vite](https://vitejs.dev/) + [TypeScript](https://www.typescriptlang.org/)
- **UI** : [Tailwind CSS](https://tailwindcss.com/) + [shadcn/ui](https://ui.shadcn.com/)
- **Backend / Desktop** : [Tauri v2](https://v2.tauri.app/) (Rust)
- **Routage** : React Router Dom

## 🚀 Installation et Développement

Assurez-vous d'avoir [Node.js](https://nodejs.org/) (ou Bun/Pnpm) et l'environnement de développement ciblé par [Tauri](https://v2.tauri.app/v2/guides/getting-started/prerequisites) (Rust, C++ build tools, etc.) installés.

```bash
# Installer les dépendances (l'auteur initial utilise apparemment bun)
bun install

# Lancer l'environnement de développement
bun tauri dev

# Compiler l'application pour la production
bun tauri build
```

## 🤝 Contribution

Les contributions (signaler des bugs, proposer des fonctionnalités, améliorer le code) sont les bienvenues ! 
N'hésitez pas à ouvrir des *Issues* ou soumettre des *Pull Requests*.
