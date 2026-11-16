# Plugins audio d'Antony

Plugins audio (AU / VST3 / Standalone) écrits avec [JUCE 8](https://juce.com), pour la scène et le studio.

| Plugin | Description |
|---|---|
| [**Palindrome**](Palindrome/) | Capture une phrase jouée par un instrument et la rejoue en différé : reverse / normal, ordre des notes au hasard, tonalité, écho façon Memory Man, banque de captures, découpage legato et accords, MIDI learn. |

## Installer (Mac Apple Silicon)

Les plugins compilés sont dans le dossier `binaires/` de chaque plugin (ex. [Palindrome/binaires](Palindrome/binaires/)). Clique sur un .zip puis *Download raw file*, dézippe, puis :

1. copie `*.component` dans `~/Library/Audio/Plug-Ins/Components` et `*.vst3` dans `~/Library/Audio/Plug-Ins/VST3` ;
2. les plugins ne sont pas signés par Apple : dans le Terminal, lance
   `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Palindrome*.component ~/Library/Audio/Plug-Ins/VST3/Palindrome.vst3`.

## Compiler

Chaque plugin a son propre `CMakeLists.txt` :

```bash
cd Palindrome
cmake -B build -DJUCE_PATH=/chemin/vers/JUCE   # sinon JUCE 8.0.4 est téléchargé
cmake --build build --config Release -j8
```

## Licence

GPLv3 (voir [LICENSE](LICENSE)), compatible avec la licence gratuite de JUCE.
