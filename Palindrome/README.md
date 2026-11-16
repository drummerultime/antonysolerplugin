# PALINDROME 2.2

Plugin d'effet audio (VST3 / AU / Standalone, JUCE 8) à mi-chemin entre delay, écho et sampler. Il capture des phrases jouées par un instrument et les rejoue plus tard, en reverse ou à l'endroit, en les découpant note par note. Le hasard, la tonalité, l'écho et l'espace sont entièrement paramétrables.

## Utilisation de base (mode Cycle)

Une guitare joue une phrase de 6 s, qui est capturée (jusqu'à 20 s). Après le **délai**, Palindrome la rejoue selon le **sens** choisi (ex. reverse puis normal), pendant le nombre de **répétitions** voulu. Ensuite vient un **silence** tiré au hasard dans une plage (jusqu'à 2 min), puis une nouvelle capture, et ainsi de suite.

## Modes de capture

| Mode | Comportement |
|---|---|
| Continue | Les phrases sont capturées en continu, à la suite. |
| Au seuil | La capture démarre dès que le signal dépasse le seuil (pré-roll de 30 ms). |
| Manuelle | Bouton *Capturer*, ou paramètre « Déclencher capture ». |
| **Cycle** | Une capture à la fois : capture → délai → N répétitions → silence → capture suivante. |
| **Banque** | N captures (2 à 8). La banque se remplit cycle après cycle, puis elle est figée et rejouée pas à pas : séquence tapée (ex. `3 2 4 1`) ou ordre au hasard, avec un silence entre les pas. |

## Jeu en live

- **HOLD** : plus aucune nouvelle capture, la phrase en cours de relecture tourne en boucle. Pendant un silence, Hold relance aussitôt la dernière phrase jouée. Relâcher reprend le cycle normal.
- **VIDER** : fondu de 30 ms (pas de clic), puis tout est vidé, sauf les captures verrouillées de la banque.
- **Annuler / rétablir** (flèches de l'en-tête) et **A / B** : deux mémoires de réglages à comparer ; le bouton à côté copie les réglages actuels dans l'autre mémoire.
- **Pas suivant** (Cycle / Banque) : annule ce qui n'a pas commencé, laisse finir la répétition en cours, puis enchaîne l'étape suivante sans silence.
- **MIDI learn intégré** : bouton **MIDI** de l'en-tête, clic sur un réglage ou un bouton (Capturer, Suivant, Vider, Hold, Tap, Annuler, A/B…), puis bouge ta pédale ou joue une note. Clic droit : oublier. Les assignations sont gardées dans le projet et deviennent celles par défaut des nouvelles instances.
  - Une note ou un CC qui passe au-dessus de 64 = un appui. Sur un interrupteur (Hold…), chaque appui bascule. Sur une liste, une note fait défiler les choix ; un CC balaie la plage.
  - **Dans Logic**, un effet audio ne reçoit pas le MIDI : utilise **Palindrome MIDI**. Crée une piste d'instrument, choisis *AU MIDI-controlled Effects > Antony > Palindrome MIDI* comme instrument, puis dans le menu *Side Chain* en haut de la fenêtre du plugin, choisis l'entrée de ta guitare (ou la piste audio de la guitare). La piste d'instrument doit être sélectionnée (ou armée) pour recevoir le pédalier.
  - La version **Palindrome** (effet audio normal) reste pilotable par les *Assignations de contrôleurs* de Logic (⌘K).
- **Presets** : 10 presets d'usine (Violon superposé, Écho qui monte, Memory Man, Chaos contrôlé, Banque en séquence, Nappe granulaire, Bande au ralenti, Écho rythmique, Sound on sound…). Tu peux enregistrer les tiens avec *Enregistrer* ; ils vont dans `~/Library/Application Support/Palindrome/Presets`.

## Banque

- Clic sur un emplacement : le choisir pour **Recapturer** (point rouge).
- **Glisser** un emplacement rempli vers une piste du DAW : export en WAV.
- **Déposer** un fichier audio (wav, aif, flac, mp3…) sur un emplacement : il est chargé dedans (20 s max, la fréquence d'échantillonnage est convertie).
- Les captures de la banque sont **enregistrées dans le projet** (FLAC sans perte) et retrouvées à la réouverture.
- Onglet **Banque : réglages** : chaque emplacement peut avoir son propre sens, sa transposition (ajoutée à la globale) et son nombre de répétitions.

## Onglets

### Découpage
- **Ce que tu joues** : *Auto* reconnaît chaque morceau comme note seule ou accord ; *Notes seules* force la transposition mélodique (PSOLA, la plus précise) ; *Accords* force la transposition polyphonique (vocodeur de phase à verrouillage de phase), pour les accords et les arpèges laissés sonner.
- **Attaques** : sensibilité aux attaques (médiator, doigts).
- **Legato** : repère aussi les changements de note sans attaque (hammer-on, pull-off, slide, changement d'accord tenu), par analyse du contenu harmonique.
- **Fondu** : fondu enchaîné entre les notes ; les coupes sont calées sur un passage par zéro.
- La vue montre le découpage de ta **dernière capture** (traits blancs = attaques, pointillés = legato, points orange = accords, nom de chaque note) et se met à jour quand tu tournes les réglages.

### Cycle & capture
- **Tempo** : *Projet* suit le tempo et les mesures de Logic ; quand le projet est à l'arrêt, une horloge interne prend le relais, donc les captures restent calées sur une grille. *Interne* : ton propre tempo, réglé au **TAP** (le temps 1 tombe sur ton dernier tap).
Mode, délai (1 à 60 s, compté depuis le début ou la fin de la capture), répétitions (1 à 32), écart, silence min / max, capture suivante (immédiate ou au seuil).

**Unité de temps « Tempo »** : longueur, délai, écart et silences se règlent en temps, au tempo du projet. *Départ des captures* cale chaque capture sur le temps ou la mesure la plus proche (quand le projet joue).

### Lecture & hasard
- **Sens** : alterné, reverse ou normal uniquement, au hasard, ou **superposé** (reverse et normal en même temps).
- **Désordre** de l'ordre des notes, et probabilités par note : **inversées**, **muettes**, **répétées**, **sauts d'octave**, **hasard mélodique** (±1 ou 2 degrés dans la gamme). Option **Hasard figé**.
- **Rythme** : **grille** (1/4 à 1/32, triolets) avec **quantification** dosable pour recaler les notes sur le tempo, plus un **flou** rythmique au hasard.

### Tonalité
14 gammes (majeur, mineurs, modes, pentatoniques, blues, par tons, chromatique), avec correction, **transposition** et **dérive par répétition** comptées en degrés de la gamme.

Le changement de hauteur conserve la durée des notes :
- PSOLA synchronisé sur la période jusqu'à ±7 demi-tons ;
- rééchantillonnage + WSOLA au-delà, et pour les sons sans hauteur détectable.

### Écho & bande
- **Décroissance** par répétition, **assombrissement** progressif, couleur (passe-bas), coupe-bas.
- **Vitesse de bande** : demi-vitesse, double vitesse, alternance, ralentit ou accélère au fil des répétitions, au hasard. La hauteur et la durée changent ensemble, comme une bande.
- **Modulation** wow / flutter et **saturation**.
- **Sound on sound** : la relecture est réinjectée dans la capture, et les couches s'empilent au fil des cycles.

### Espace
- **Diffusion** : réverbération sur les répétitions, éventuellement croissante au fil des répétitions, avec réglage de **taille**.
- **Stéréo** : reverse à gauche / normal à droite (ou l'inverse), ping-pong ou au hasard, avec réglage de l'**ouverture**.
- **Ducking** : la relecture baisse quand tu joues et remonte quand tu t'arrêtes.

### Violoning
Montée de volume façon pédale de volume. Elle s'applique à chaque note ou à chaque répétition, aux lectures normales et/ou reverse, et à toutes les notes, une sur deux, une sur trois, une sur quatre ou au hasard.

### Texture
Transforme la relecture en nuage de grains tirés de la phrase. Réglages : part de texture, taille des grains, densité, dispersion.

### Sortie (toujours visible)
- **Relecture seule** : pour une piste auxiliaire (départ depuis la piste de guitare) : coupe le son direct et envoie la relecture à plein niveau, pour la traiter à part. Logic n'accepte pas de sorties supplémentaires sur un effet audio, c'est donc la façon d'avoir la relecture sur sa propre tranche.
Mix direct / relecture et niveau de la relecture.

## Interface

- **Scène d'activité** (en haut) : capture en cours, silence restant et relectures programmées. La position verticale d'un bloc donne son placement stéréo (gauche en haut, droite en bas), sa hauteur donne le niveau de la répétition, et ses chevrons le sens (◀ reverse orange, ▶ normal cyan) et la vitesse. Chaque bloc porte son numéro de répétition, sa transposition et sa vitesse.
- **Phrase rejouée** : la forme d'onde découpée en notes. On y voit l'ordre d'origine des notes, les notes inversées (⇄), les notes muettes et la transposition de chaque note.
- **Banque** (affichée seulement en mode Banque) : les emplacements avec leur forme d'onde, la séquence en pastilles (le pas en cours s'allume), le nombre de captures et l'ordre.
- **Un visuel par onglet**, qui réagit en direct aux réglages :
  - plan du cycle, avec la phase en cours allumée ;
  - aperçu du hasard sur 8 notes (clic = nouveau tirage) ;
  - sens des répétitions ;
  - grille rythmique ;
  - clavier de la gamme (clic sur une touche = tonique) ;
  - hauteur de chaque répétition ;
  - barres de l'écho (niveau, filtre, vitesse, modulation, réinjection) ;
  - champ stéréo vu de dessus ;
  - halos de diffusion ;
  - courbe de ducking ;
  - enveloppes de violoning ;
  - nuage de grains de la texture.
- **Bulles d'aide** sur chaque réglage : laisse la souris une demi-seconde dessus.
- Une couleur par section. Partout, **orange = reverse** et **cyan = normal**.

L'interface est **redimensionnable** : tire le coin en bas à droite, les proportions sont conservées.

## Compilation (macOS)

```bash
cd Palindrome
cmake -B build -DJUCE_PATH=/chemin/vers/JUCE      # sinon JUCE 8.0.4 est téléchargé
cmake --build build --config Release -j8
```

Les plugins sont copiés dans `~/Library/Audio/Plug-Ins/VST3` et `~/Library/Audio/Plug-Ins/Components` (Palindrome.component et Palindrome MIDI.component).

L'interface se redimensionne librement (coin en bas à droite, ou bouton ⤢ pour une taille toute faite) ; la taille est mémorisée.

Tests du moteur sans DAW : `cmake -B build -DPALINDROME_TESTS=ON`, puis lancer la cible `PalindromeTest`.

## Version

Le numéro de version (`project(PALINDROME VERSION x.y.z)` dans `CMakeLists.txt`) est augmenté à chaque modification. Logic Pro garde en cache la description des paramètres d'un AU et ne la relit que si la version change.

## Limites connues

- Sur les **accords**, la correction et la transposition dans la gamme se font sur la note la plus grave : tout l'accord est déplacé d'un bloc (pas de réharmonisation note par note).
- Une relecture démarre au plus tôt 0,5 s après la fin de sa capture : c'est le temps réservé au calcul.
- Le binaire est pour Mac Apple Silicon, signé localement.
- L'**état du projet** contient l'audio de la banque : environ 0,5 à 2 Mo par capture de 20 s.
