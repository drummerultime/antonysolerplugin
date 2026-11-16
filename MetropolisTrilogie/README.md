# Trilogie Metropolis : Maria, Rotwang, Grot

Trois plugins d'effet (AU / VST3, JUCE 8) conçus pour un ciné-concert improvisé sur *Metropolis* de Fritz Lang. Ils partagent la même architecture : hasard dosé (Chaos, Seed, Evolve), dés et mutations, 8 scènes, macros, MIDI learn, interface redimensionnable, bouton PANIC.

| Plugin | Rôle |
|---|---|
| **Maria** (1.6) | Partenaire d'improvisation, dans l'esprit d'OMax (Ircam) : elle écoute un musicien, apprend une mémoire glissante de son jeu et lui répond avec son propre son. Attitudes Dialogue, Duo, Écho, Contrepoint, Contraste, Machine ; leitmotivs ; chapitres de mémoire ; écoute harmonique ; suivi de la pulsation des musiciens ; sortie MIDI virtuelle « Maria ». |
| **Rotwang** (1.0) | Sound design très randomisé pour instruments à vent, kalimbas, etc. : textures arythmiques et éléments rythmiques, 4 slots d'effets, matrice de modulation. |
| **Grot** (1.0) | Sound design pour batterie et percussions : détection des coups, profils de source, chance par coup, mêmes slots d'effets. |

Maria publie la pulsation qu'elle entend (`Shared/PulseBus.h`) ; Rotwang et Grot peuvent s'y caler.

## Dossiers

- `Shared/` : code commun aux trois (DSP, interface, pulsation partagée).
- `Maria/`, `Rotwang/`, `Grot/` : un projet CMake par plugin, avec son banc de tests (`Tests/`).
- `binaires/` : plugins compilés pour Mac Apple Silicon (AU + VST3).

## Compiler

```bash
cd Maria            # ou Rotwang, Grot
cmake -B build -DCMAKE_BUILD_TYPE=Release            # JUCE est téléchargé automatiquement
# ou avec une copie locale : -DMA_JUCE_PATH=/chemin/JUCE  (RW_JUCE_PATH, GR_JUCE_PATH)
cmake --build build -j8
```

Tests sans DAW : ajouter `-DMA_BUILD_TESTS=ON` (`RW_…`, `GR_…`) puis lancer la cible `MariaTest` (`RotwangTest`, `GrotTest`).
