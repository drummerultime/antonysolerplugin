# FRAGOR — Dubstep Destruction Synth

Instrument virtuel VST3 / AU / Standalone (JUCE 8, C++17) dédié au sound design dubstep : wobbles, growls, yoi, riddim, tearout, screech, reese, sub et FX.

## Nouveautés 1.1

- **Courbes de LFO dessinées** : forme CUSTOM sur les 3 LFO, éditeur plein écran (bouton EDIT ou double-clic sur la forme). Clic = point, glisser = déplacer (Maj = hors grille), clic droit = supprimer, Alt+glisser un segment = courbure. 10 formes prêtes (stutter, triplets, talk, gate...), INVERT / REVERSE, grille 1/4 à 1/32.
- **Sur-échantillonnage des voix** (VOICE > QUALITY) : 1x ECO, 2x HQ (défaut), 4x ULTRA, plus une correction polyBLEP du hard sync. Mesure sur une scie synchronisée aiguë : repliement à −40 dB en 1x, −62 dB en 2x, −76 dB en 4x (contre −25 dB avant).
- **Filtre 2** (onglets F1 / F2) en série ou en parallèle, avec ses propres destinations de modulation (CUTOFF 2, RESO 2, FLT2 MORPH) et ENV 3 dédiée.
- **Distorsion 2** et **chaîne d'effets libre** : page EFFECTS, glisser les modules dans la barre pour changer l'ordre, clic pour activer/couper.
- **Import de wavetables** : bouton WAV de chaque oscillateur, ou glisser un fichier sur l'affichage. Les fichiers au format wavetable (cycles de 2048 échantillons, exports Serum/Vital) sont lus tels quels ; un son quelconque est découpé en 16 cycles par détection de période. Les tables USER A / USER B sont enregistrées dans le projet et les presets.
- **Anneaux de modulation** : chaque bouton modulé affiche sa plage (arc violet) et la valeur en direct (point rouge).
- **Glisser-déposer des modulations** : puces à poignée (LFO 1-3, ENV2/3, M1-M4, VEL, MW, AT, SLIDE, NOTE, RND, P.ENV) à lâcher sur un bouton. Alt+glisser sur le bouton = quantité, clic droit = liste / retirer / ajouter.
- **Volume normalisé** : presets d'usine, GENERATE et MUTATE sont calés à environ −10 dBFS RMS (désactivable : SET > AUTO GAIN).
- **Navigateur de presets** : tags, recherche, favoris (étoile), presets utilisateur avec catégorie ; **UNDO / REDO** sur presets, GENERATE, MUTATE et imports.
- **MIDI / live** : sortie MIDI (LFO 1 en CC et/ou une note par pas du séquenceur), aftertouch canal et polyphonique, MPE (bend par note, pression, slide CC74), **8 snapshots de macros** avec morphing (Maj+clic = mémoriser).
- 6 nouveaux presets : Custom Wub Stutter, Riddim Triplet Gate, Double Dist Grinder, Formant Split, Hi-Def Screech, MPE Growl Lead.

## Compiler sur macOS

```bash
xcode-select --install      # si besoin
brew install cmake          # si besoin
cd FRAGOR
chmod +x build_mac.sh
./build_mac.sh
```

Le script télécharge JUCE 8.0.4, compile en binaire universel (Apple Silicon + Intel) et installe :

- `~/Library/Audio/Plug-Ins/VST3/FRAGOR.vst3`
- `~/Library/Audio/Plug-Ins/Components/FRAGOR.component` (AU)
- l'appli autonome dans `build/Fragor_artefacts/Release/Standalone/`

Dans Ableton Live : Préférences > Plug-ins > Rescan. FRAGOR apparaît comme instrument (fabricant « Antony Soler »).

## Architecture du son

```
OSC A ─┐                                         ┌─ DIST (x4 oversampling) ─ CRUSH ─┐
OSC B ─┼─ FILTRE (par voix) ─ AMP ─ [voix 1..8] ─┤              (ordre inversable) ├─ CHORUS ─ DELAY ─ REVERB ─ MASTER
NOISE ─┘                                         └─ OTT (3 bandes haut/bas) ────────┘
SUB ─────── (direct ou à travers le filtre)
```

### Oscillateurs A & B
- **12 wavetables** générées (16 positions, anti-repliement par mipmaps) : BASIC, GROWL, YOI, METAL, SCREECH, FOLD, HOLLOW, DIGITAL, PULSE, REESE, TALK, CHAINSAW.
- **10 modes de warp** : BEND+, BEND−, SYNC, PWM, MIRROR, QUANTIZE, FM depuis l'autre osc, FM depuis le sub, RING.
- Unisson 8 voix (detune, blend, largeur stéréo), phase aléatoire, octave / demi-ton / fine.

### Sub & bruit
Sub sinus / triangle / carré à −2, −1 ou 0 octave, en option hors filtre (PURE) pour garder un sub propre. Bruit à couleur variable.

### Filtre (par voix)
LP 24, LP 12, BAND, HIGH, NOTCH, **VOWEL** (formants A-E-I-O-U morphables), **COMB +/−** (accordable sur la note avec KEY à 100 %). Drive, key-track, mix, enveloppe 2 dédiée.

### Modulation
- 3 LFO synchronisables (9 formes dont WUB, S&H, DRIFT), courbe, lissage, phase, retrig ou calés sur la grille de l'hôte.
- **Séquenceur de vitesse du LFO 1** (jusqu'à 16 pas) : chaque pas choisit une division (1/1 à 1/64, triolets), `--` (fermé) ou OPEN (ouvert). C'est le cœur des wobbles rythmés. Glisser sur la grille pour régler, clic droit pour choisir.
- 3 enveloppes (AMP, ENV 2, ENV 3) + enveloppe de pitch (impacts, lasers, punch du sub).
- **Matrice 12 slots** : 14 sources (LFO, env, vélocité, mod wheel, note, aléatoire, 4 macros, pitch env) vers 26 destinations, y compris les effets (drive de disto, crush, profondeur OTT, mix delay/reverb).
- 4 macros nommables (double-clic sur le nom).

### Effets
- **DIST** sur-échantillonnée x4 : TUBE, HARD CLIP, FOLDBACK, SINE FOLD, RECTIFY, DIODE, OVERDRIVE ; bias, tonalité, mix.
- **CRUSH** : bits + downsampling.
- **OTT** : compression multibande haut/bas façon OTT (profondeur, temps, upward, downward, sortie), mixage en phase.
- Ordre DIST > OTT ou OTT > DIST.
- CHORUS, DELAY synchro (ping-pong, tonalité), REVERB.
- MASTER : basses en mono sous une fréquence réglable, largeur stéréo, clipper doux, gain.

### Créativité
- **GENERATE** : construit un patch complet et cohérent dans le style choisi (WOBBLE, GROWL, RIDDIM, TEAROUT, SCREECH, REESE, YOI, CHAOS) : tables, warps, filtre adapté, séquence rythmique, routings et macros.
- **MUTATE** : fait muter le patch courant (positions, warps, séquence, disto…) pour trouver des variations.
- 42 presets d'usine + sauvegarde/chargement de presets utilisateur (`~/Documents/FRAGOR/Presets/*.fragor`).

## Conseils de jeu
- Les wobbles se calent sur le tempo de l'hôte. Avec RETRIG, le séquenceur repart à chaque note ; sans RETRIG, il suit la grille de la session.
- Macro 1 (SPEED) double la vitesse du LFO 1 : parfait pour les fins de phrase.
- Pour un growl « qui parle » : table GROWL ou TALK, LFO → WT POS, filtre VOWEL, LFO 2 → VOWEL.
- Pour un comb accordé : COMB +/−, KEY à 100 %, CUTOFF à 261 Hz (= la note jouée).
- Le sub reste propre en mode PURE et MONO BASS activé ; la disto et l'OTT ne s'appliquent qu'après le mix, pensez au MIX de la disto pour préserver le bas.

## Structure du code
```
Source/
  Params.*            paramètres (APVTS), listes de choix
  Presets.*           presets d'usine, générateur, mutation
  PluginProcessor.*   audio, MIDI, état, presets utilisateur
  PluginEditor.*      interface (1200x840, redimensionnable)
  dsp/Wavetables.*    génération des tables + mipmaps
  dsp/Warp.h          déformations de phase
  dsp/Engine.*        voix, LFO, enveloppes, filtres, matrice
  dsp/Effects.*       disto, crush, OTT, delay, chaîne master
  ui/*                look & feel, widgets (wavetable 3D, scope, séquenceur)
  render/RenderMain   outil de test hors-ligne (-DFRAGOR_BUILD_RENDER=ON)
```

Outil de test : `cmake -B build -DFRAGOR_BUILD_RENDER=ON && cmake --build build --target FragorRender`, puis
`FragorRender <dossier>` (rend tous les presets en WAV), `FragorRender <dossier> --stress` (fuzz + aller-retour d'état).
