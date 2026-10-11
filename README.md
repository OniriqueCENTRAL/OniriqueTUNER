# ONIRIQUE TUNER — correction de hauteur et vocodeur pour la voix (VST3)

Autotune dans l'esprit d'Auto-Tune Pro, avec un vocodeur piloté par la voix
elle-même et la charte du site ONIRIQUE : dégradé sang → nuit, sigil
incandescent dans son réticule, potards à arc braise, étoiles à quatre
branches.

![Interface](Apercu%20interface.png)

- **Format** : VST3, Windows 64 bits, mono ou stéréo, avec sortie MIDI.
- **Fichier** : `build/OniriqueTuner_artefacts/Release/VST3/ONIRIQUE TUNER.vst3`
- **Installé dans** : `C:\Program Files\Common Files\VST3\ONIRIQUE TUNER.vst3`
  (relancer la recherche de plugins du DAW pour le voir apparaître).

## Les réglages

| Réglage | Rôle |
|---|---|
| **LIVE / HQ** | LIVE : aucune latence, pour enregistrer avec le retour casque. HQ : la voix est retardée de 24 à 50 ms selon le registre (35 ms en Alto / Tenor, compensés par le DAW) pour une correction plus précise, à utiliser au mix. |
| **Input Type** | Registre de la voix (Soprano, Alto / Tenor, Low Male) ou instrument. Aide le détecteur et fixe la latence du mode HQ. |
| **Key / Scale** | Tonalité et gamme. Les notes allumées sont celles vers lesquelles la voix est ramenée. |
| **Notes** | Un clic retire une note de la gamme, ou en ajoute une. Changer de tonalité ou de gamme remet les notes à zéro. |
| **Retune Speed** | 0 ms : correction instantanée (effet « hard-tune »). 20 à 50 ms : correction serrée mais naturelle. Au-delà : très discret. |
| **Flex-Tune** | Ne corrige que lorsque la voix s'approche d'une note ; laisse libres les glissés et les effets volontaires. |
| **Humanize** | Relâche la correction sur les notes tenues, sans toucher aux notes brèves. |
| **Natural Vibrato** | Atténue (vers la gauche) ou accentue (vers la droite) le vibrato du chanteur. |
| **Formant** | Éteint : son classique, le timbre suit la note. Allumé : le timbre est préservé, utile pour les grosses transpositions. |
| **Throat** | Longueur de gorge (avec Formant) : plus long = voix plus sombre, plus court = plus claire. |
| **Transpose / Detune** | Transposition en demi-tons ; diapason (440 Hz par défaut). |
| **Tracking** | Exigence du détecteur : à baisser pour une voix soufflée ou bruitée, à monter si des bruits déclenchent la correction. |
| **Mix / ON** | Dosage voix traitée / voix d'origine ; mise hors circuit (reliée au bypass du DAW). |
| **VOCODER** | Met le vocodeur en marche. Rien d'autre à brancher : la voix est convertie en notes, et ces notes jouent le synthé du vocodeur. |
| **Mono / Octave / Fifth / Chord** | Ce que joue le synthé autour de la note chantée : la note seule, avec ses octaves, avec sa quinte, ou un accord de trois sons pris dans la gamme choisie. |
| **Blend** | Dosage entre la voix accordée (0 %) et le vocodeur (100 %). |

L'afficheur central montre la note visée, l'écart de la voix (point clair) et
ce que le plugin corrige (arc braise). Avec le vocodeur, il indique aussi le
numéro de la note MIDI tirée de la voix. La bande du dessous trace la hauteur
chantée et la hauteur corrigée sur les lignes de la gamme.

L'interface se redimensionne par le coin en bas à droite ; double-clic sur
un potard pour revenir à sa valeur par défaut.

### Le vocodeur en pratique

- Il suit la tonalité : Key et Scale décident des notes jouées, Transpose les
  décale, Retune Speed règle le glissement d'une note à la suivante (0 ms =
  notes en escalier).
- Les consonnes passent par un bruit : les paroles restent compréhensibles.
- Il ne sort jamais plus fort que la voix qui le pilote.
- Les notes tirées de la voix sortent aussi en **MIDI** (canal 1) tant que le
  vocodeur est allumé : on peut les envoyer vers un autre instrument depuis le
  routage MIDI du DAW.

## Comment ça marche

1. **Détection** (`DSP/PitchDetector`) : mesure de périodicité calculée par
   FFT sur un signal d'analyse décimé, toutes les 2,7 ms. Le détecteur est
   réglé pour la voix : les comparaisons entre périodes candidates se font
   en proportion du souffle présent, la décision s'appuie sur plusieurs
   mesures consécutives quand la voix est bruitée, et une note accrochée
   n'est lâchée ou changée d'octave que sur une preuve qui dure.
2. **Tri** (`DSP/TuneEngine`) : ce qui est très en dessous du niveau récent
   de la voix (repisse du casque, queue de réverbération) est laissé intact.
3. **Choix de la note** (`DSP/PitchCorrector`) : note la plus proche dans la
   gamme (avec hystérésis), puis Retune Speed, Flex-Tune, Humanize, vibrato.
4. **Transposition** (`DSP/TuneEngine`) : grains synchrones à la période de la
   voix (PSOLA). En son classique chaque grain est relu plus ou moins vite ;
   avec Formant il est replacé tel quel à la nouvelle période.
5. **Vocodeur** (`DSP/Vocoder`) : banc de 32 filtres sans latence. La note
   choisie à l'étape 3 pilote des dents de scie désaccordées ; chaque bande
   du synthé est ramenée au niveau de la même bande de la voix.

La différence entre LIVE et HQ tient au placement des grains :

- **LIVE** ne connaît que le passé. Il prédit la période à venir à partir des
  derniers cycles et réajuste la vitesse de lecture à chaque mesure.
- **HQ** voit venir la voix : chaque période est mesurée de part et d'autre du
  grain, et la courbe de hauteur est lissée avec de l'avance.

Avec un Retune Speed de 20 ms ou plus, la voix est simplement transposée par
la correction : elle garde ses micro-variations naturelles, dans les deux
modes. En dessous, la hauteur de sortie est imposée (voix parfaitement
plate à 0 ms).

## Ce qui a été vérifié

`Tests/engine_test` fait tourner le moteur seul, sans DAW. Tous les essais
passent, y compris sous AddressSanitizer (aucune erreur mémoire signalée).

**Justesse**, sur des voix de synthèse à la hauteur et au timbre connus :

- un son non voisé (bruit, consonnes) ressort **bit pour bit** ;
- hard-tune : à **moins de 0,1 cent** de la note sur une voix (0,5 cent au
  pire, sur un instrument très aigu), aussi propre que la voix d'origine ;
- vibrato de ± 40 cents, Retune 0 : il en reste 0,06 cent en HQ et 1,8 cent
  en LIVE ; avec un Retune lent il est intact.

**Voix réalistes** (irrégularité d'un cycle à l'autre, souffle, voyelles qui
changent, consonnes, silences, bruit de fond ; neuf voix : homme et femme,
claire, soufflée, rauque, bruitée, grave, soprano) : 100 % des notes tenues
reconnues, 0,1 % de consonnes ou silences pris pour une note, moins de 1 %
d'erreurs d'octave.

**Vraies prises** (ad-libs rap non corrigés, do mineur, Retune 0), hauteur
obtenue remesurée par un second détecteur : la voix brute est en moyenne à
45 cents de la gamme ; elle tombe à **environ 4 cents en HQ** et **5 à
9 cents en LIVE**. Aucun clic détecté.

**Vocodeur** : le synthé joue la note de la gamme à moins d'un demi-cent
(ses deux dents de scie sont volontairement écartées de 7 cents), sort au
niveau de la voix (± 1 dB), suit son timbre à 95 %, laisse passer les
consonnes, construit ses accords dans la gamme, se tait quand la voix
s'arrête, ne dépasse jamais les crêtes de la voix. Chaque note MIDI ouverte
est refermée.

**Pannes** : 500 valeurs invalides injectées dans le son (NaN, infini,
démesuré) sans corrompre la sortie ; mode, registre, vocodeur, formant et
bypass changés pendant que ça chante sans clic ; de 8 à 384 kHz ; appels
limites ; **aucune allocation mémoire** pendant le traitement ; repisse du
casque laissée intacte.

**Charge** : environ 1,3 % d'un cœur pour l'accordage, 3 % avec le vocodeur
en stéréo (à 48 kHz).

`OniriqueTunerHostTest` charge le fichier `.vst3` dans un hôte VST3 : nom,
30 paramètres, bypass, latence déclarée égale au retard réel (0 en LIVE,
35,4 ms en HQ à 48 kHz), sauvegarde et rechargement, sortie MIDI du
vocodeur, 1200 blocs avec tous les réglages automatisés au hasard, interface
ouverte et fermée.

Limites connues :

- En LIVE, sur une voix qui bouge très vite, la correction instantanée est
  un peu moins exacte qu'en HQ (c'est le prix de l'absence de latence).
- Le mode LIVE ne déclare aucune latence, mais pendant une correction le son
  sort avec un retard de l'ordre d'une demi-période de la voix (2 à 5 ms).
- Le vocodeur suit une seule voix à la fois ; la sortie MIDI demande d'être
  routée dans le DAW pour servir à autre chose.
- Les essais sont des mesures ; l'écoute dans le DAW reste le juge final.
  Des extraits traités sont dans `Tests/out/`.

## Recompiler

Il faut CMake et les Build Tools de Visual Studio 2022. JUCE 8.0.4 est
téléchargé automatiquement ; pour réutiliser la copie déjà présente sur ce
PC (aucun téléchargement) :

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DFETCHCONTENT_SOURCE_DIR_JUCE="C:/Users/mathe/Downloads/OniriqueBased/build/_deps/juce-src"
cmake --build build --config Release --target OniriqueTuner_VST3 --parallel
```

Outils de développement (non construits par défaut) :

```bash
# moteur seul : tous les essais, ou un seul groupe
# (justesse, charge, robustesse, voix, vocodeur, pannes)
cmake -S Tests -B build-tests -G "Visual Studio 17 2022" -A x64
cmake --build build-tests --config Release
build-tests/Release/engine_test.exe
build-tests/Release/engine_test.exe vocodeur

# moteur seul sur des fichiers : écrit les versions LIVE, HQ et vocodeur dans Tests/out
build-tests/Release/engine_test.exe Tests/out "chemin/prise.wav"

# rendu de l'interface dans une image, sans DAW
cmake --build build --config Release --target OniriqueTunerSnapshot
build/Release/OniriqueTunerSnapshot.exe apercu.png 1.0 hq vocoder

# chargement du .vst3 dans un hôte de test
cmake --build build --config Release --target OniriqueTunerHostTest
build/OniriqueTunerHostTest_artefacts/Release/OniriqueTunerHostTest.exe "build/OniriqueTuner_artefacts/Release/VST3/ONIRIQUE TUNER.vst3"
```

## Structure

```
Source/DSP/          moteur audio, indépendant de JUCE
Source/GUI/          charte (Theme.h), contrôles, afficheurs, habillage
Source/GUI/Assets/   sigil et polices du site
Source/Plugin*.cpp   liaison avec le DAW, paramètres, interface
Tests/               banc de test du moteur, voix de synthèse réaliste
Tools/               rendu de l'interface, hôte VST3 de test
```

## Polices

Unbounded, Grenze Gotisch, Hanken Grotesk et JetBrains Mono, les polices du
site, sont intégrées au plugin. Elles sont distribuées sous licence SIL Open
Font License 1.1, qui autorise leur intégration dans un logiciel.
