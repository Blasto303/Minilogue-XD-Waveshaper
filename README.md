# Minilogue-XD-Waveshaper

Unités utilisateur pour le KORG minilogue xd (logue SDK 1.1). Firmware du xd requis : 2.00 ou plus.

| Unité | Slot | Nom sur le xd | Fichier |
|---|---|---|---|
| Waveshaper | MULTI ENGINE (USR) | `WaveShpr` | `oscillators/waveshaper/waveshaper.mnlgxdunit` |
| Générateur à clusters | MULTI ENGINE (USR) | `Swarm` | `oscillators/swarm/swarm.mnlgxdunit` |
| Frequency shifter | MOD FX | `FreqShft` | `effects/freqshift/freqshift.mnlgxdunit` |
| Frequency shifter à échos | MOD FX | `Spiral` | `effects/spiral/spiral.mnlgxdunit` |
| Delay chaotique, grondement sub | DELAY | `DngGrowl` | `effects/dungeon/growl/dnggrowl.mnlgxdunit` |
| Delay chaotique, métallique | DELAY | `DngMetal` | `effects/dungeon/metal/dngmetal.mnlgxdunit` |
| Delay chaotique, résonance de cloche | DELAY | `DngBell` | `effects/dungeon/bell/dngbell.mnlgxdunit` |
| Delay chaotique, diffus | DELAY | `DngVoid` | `effects/dungeon/void/dngvoid.mnlgxdunit` |
| Granulaire, grains moyens | REVERB | `GrnCloud` | `effects/grain/cloud/grncloud.mnlgxdunit` |
| Granulaire, shimmer à l'octave | REVERB | `GrnShimr` | `effects/grain/shimmer/grnshimmer.mnlgxdunit` |
| Granulaire, grains minuscules | REVERB | `GrnDust` | `effects/grain/dust/grndust.mnlgxdunit` |

## WaveShpr (oscillateur)

Une source sinus ou triangle passe dans un wavefolder, avec un anti-aliasing ADAA du premier ordre.

| Contrôle | Rôle |
|---|---|
| SHAPE | Quantité de pli (gain d'entrée de 1 à 12). Le LFO du xd en mode SHAPE le module. |
| SHIFT + SHAPE | Biais : rend le pli asymétrique et ajoute des harmoniques paires. Symétrique à 0 %, maximum vers 50 %, de nouveau symétrique (inversé) à 100 %. |
| Type | 3 shapers, dans l'ordre : sine fold (style West Coast), triangle fold (plus brillant), saturation douce. |
| Source | 0 % = sinus, 100 % = triangle. |
| Sub | Sinus une octave en dessous, mélangé avant le shaper. Les plis deviennent plus complexes. |
| KeyTrack | Réduit le pli sur les notes aiguës (gain divisé par 2 vers 1 kHz à 100 %). Limite la dureté et l'aliasing. |
| Damp | Passe-bas après le shaper. 0 % = ouvert. |
| Dry | Mélange avec la source non traitée. 0 % = 100 % traité. |

Tous les paramètres à 0 donnent un réglage utilisable : sinus pur, SHAPE ouvre le pli.

## Swarm (oscillateur)

Générateur inspiré de la section « generator » du Forge TME Vhikk X, recréé à partir de sa description publique (pas de son code). 4 oscillateurs à tables d'ondes qui se modulent en chaîne, et un « cluster » de 8 points : chaque point donne à chaque oscillateur une hauteur et une forme d'onde. L'oscillateur 0 reste toujours sur la note jouée.

Sur le xd, la chaîne façon Vhikk X est : Swarm → filtre et VCA analogiques → Dungeon en slot DELAY.

| Contrôle | Rôle |
|---|---|
| SHAPE | MORPH : parcourt les 8 points du cluster. Chaque oscillateur garde une hauteur de base et ne s'en écarte que légèrement ; ce sont surtout ses formes d'onde et ses décalages de phase (jusqu'à une demi-période) qui glissent. Le LFO du xd en mode SHAPE le module. |
| SHIFT + SHAPE | WARP, neutre à midi. À gauche : modulation de phase en chaîne entre oscillateurs. À droite : modulation de fréquence exponentielle croisée. |
| Span | Écart de hauteur entre les oscillateurs : unisson à 0 %, jusqu'à ±1 octave à 100 %. Les déphasages s'entendent le plus quand les hauteurs sont proches : 15 % est un bon point de départ. |
| Field | Filtre en peigne accordé sur la note, de la fondamentale à 2 octaves au-dessus. À gauche : son creux. À droite : résonance. Neutre à 0 %. |
| Fuse | À gauche : les fréquences sont secouées par du bruit filtré (grain, instabilité). À droite : wavefolder. Neutre à 0 %. |
| Scan | Choisit l'un des 101 jeux de formes d'onde (8 formes : sinus, triangle, dent de scie, carré, pulse, formant, orgue, creux). |
| Seed | Choisit l'un des 101 jeux de décalages de hauteur du cluster. |
| Algo | Comment Span est appliqué, dans l'ordre : libre (avec un léger désaccord qui bat), demi-tons, gamme mineure, série harmonique. |

L'oscillateur du xd est mono par voix : les modes stéréo du Vhikk X ne sont pas transposables. Les oscillateurs tournent librement, sans remise à zéro de phase à chaque note.

Code : 4 Ko. Mémoire totale : 21 Ko (limite 32 Ko).

## FreqShft et Spiral (MOD FX)

Un frequency shifter décale chaque fréquence du même nombre de Hz. Un do à 262 Hz décalé de +50 Hz passe à 312 Hz, sa 2e harmonique de 524 à 574 Hz. Les rapports entre harmoniques sont cassés : le son devient métallique, proche d'une cloche.

Les 2 effets partagent le même code. Le canal droit reçoit une porteuse décalée de 90°, donc les décalages lents tournent dans l'image stéréo. En mono, la rotation s'entend comme une variation de volume.

**TIME : le décalage.**
- Au centre : aucun décalage.
- À droite : vers le haut. À gauche : vers le bas.
- La course est exponentielle pour garder de la précision près du centre. 10 % du centre donne environ 0,2 Hz, 30 % donne 16 Hz, 50 % donne 125 Hz, la butée donne 2000 Hz.

**DEPTH : la quantité d'effet.**
- De 0 à 50 % : le signal décalé monte de 0 à 100 %. Dans FreqShft, le son direct disparaît à 50 %. Dans Spiral, il reste toujours présent.
- De 50 à 100 % : le feedback monte de 0 au maximum.

**Les 2 versions.**
- **FreqShft :** boucle de feedback très courte. Avec un décalage de quelques dixièmes de Hz et du feedback, on obtient un phaser « barber pole » qui monte ou descend sans fin. Avec 20 à 100 Hz, le son devient métallique.
- **Spiral :** boucle de feedback de 250 ms, assombrie à chaque passage. Chaque écho est décalé une fois de plus, donc les répétitions montent ou descendent en spirale.

Réjection de la bande opposée mesurée : 44 à 50 dB entre 50 Hz et 15 kHz.

## Dungeon : DngGrowl, DngMetal, DngBell, DngVoid (DELAY)

Inspiré du processeur « dungeon » du Forge TME Vhikk X, recréé à partir de sa description publique (pas de son code). Le but : un delay qui grogne, sature, auto-oscille et ne se répète jamais.

Dans la boucle de feedback :
- 6 taps de lecture espacés irrégulièrement, qui dérivent sous l'effet d'un attracteur de Lorenz et sautent au hasard vers de nouvelles positions (en glissant, sans clic)
- une modulation de la lecture par la sortie de la boucle elle-même, à fréquence audio : modulation de fréquence entretenue, grognement
- 3 diffuseurs courts
- un frequency shifter ; vers le bas, les fréquences se replient sous 0 Hz en grondement grave
- un filtre résonant grave, dont la fréquence suit l'attracteur
- une saturation asymétrique qui pousse plus fort quand FEED monte ; la boucle reste sombre, pour que les harmoniques de la saturation s'éteignent au lieu de s'accumuler
- un bruit infime injecté en permanence : l'effet peut démarrer seul, sans aucun son en entrée
- sur DngGrowl et DngVoid, des grains : des fenêtres de 5 à 80 ms qui s'ouvrent au hasard, chacune avec sa propre saturation. Absents avec FEED au centre, ils montent avec FEED et sont à plein vers 70 %

| Contrôle | Rôle |
|---|---|
| TIME | Temps de l'écho principal, de 10 ms à 1,5 s (course exponentielle, midi = 120 ms). Le tourner pendant que ça résonne fait glisser la hauteur. |
| DEPTH | FEED, bipolaire. Midi = un seul écho. À gauche : le signal décalé seul repart dans la boucle (spirales), avec plus de modulation de fréquence. À droite : décalé + brut (encoches mobiles), avec plus de grondement résonant. Au-delà d'environ 70 % de chaque côté, la boucle auto-oscille. L'attracteur fait varier le gain autour de 1 : le son enfle, s'étouffe et repart tout seul. |
| SHIFT+DEPTH | Dosage effet / son direct, comme les delays d'origine du xd. À 50 %, les deux sont à plein niveau. À 100 %, effet seul. |

Sur le Vhikk X, FORM, CELL et SEED sont des knobs. Le slot DELAY du xd n'a que 3 contrôles, donc ils sont figés dans 4 variantes, avec le reste de la voix :

| Variante | Décalage | Caractère |
|---|---|---|
| DngGrowl | -7 Hz | Grondement sub dans une boucle sombre, chaos rapide, grains |
| DngMetal | +31 Hz | Taps serrés, drive léger, métallique |
| DngBell | +31 Hz | Résonance aiguë à 320 Hz qui sonne comme une cloche, chaos au maximum |
| DngVoid | -0,8 Hz | Nuage diffus et sombre, résonance très grave, chute lente, grains |

Les 4 variantes démarrent seules sans entrée quand FEED dépasse le seuil d'oscillation. Aucun écrêtage de sortie au stress test, même aux réglages extrêmes.

Le code contient aussi un fuzz gate (coupures par rafales) et un bégaiement (fragments rejoués), testés mais désactivés dans les 4 variantes.

Les 4 occupent 4 des 8 slots DELAY. Code : 6 Ko (limite 12 Ko). Mémoire : 1 Mo de SDRAM (limite 2,4 Mo).

## Grain : GrnCloud, GrnShimr, GrnDust (REVERB)

Granulaire inspiré du mode granulaire de Mutable Instruments Clouds, écrit de zéro (pas de code de Clouds). L'entrée est enregistrée en continu dans environ 5,5 s de mémoire stéréo, et jusqu'à 16 grains en relisent des morceaux fenêtrés.

| Contrôle | Rôle |
|---|---|
| TIME | POSITION : à quelle distance dans le passé les grains vont lire, de 5 ms à environ 5 s (course plus fine près du présent). Tout à droite : FREEZE, l'enregistrement s'arrête et les grains se dispersent sur toute la mémoire figée. |
| DEPTH | DENSITY, bipolaire comme sur Clouds. Midi : aucun nouveau grain. À gauche : grains réguliers, de plus en plus serrés. À droite : grains aléatoires, de plus en plus denses. |
| SHIFT+DEPTH | Dosage effet / son direct. À 50 %, les deux sont à plein niveau. |

Taille des grains, transpositions, forme des fenêtres, étalement stéréo, feedback et diffusion sont fixés par variante :

| Variante | Grains | Caractère |
|---|---|---|
| GrnCloud | ~120 ms, sans transposition | Nuage doux, fenêtres arrondies |
| GrnShimr | ~180 ms, unisson, octave et quinte au-dessus | Shimmer : le feedback renvoie les grains transposés dans la mémoire, ils montent en cascade |
| GrnDust | ~25 ms, octave en dessous, quinte, octave au-dessus | Poussière : grains courts aux bords francs, très étalés en stéréo |

Au maximum de densité, 11 à 12 grains tournent en moyenne, avec des pointes au plafond de 16. Code : 3,5 Ko (limite 12 Ko). Mémoire : 2 Mo de SDRAM (limite 2,4 Mo).

Le template Korg place les unités REVERB et DELAY aux mêmes adresses mémoire. Il est donc probable qu'un Grain et un Dungeon ne puissent pas tourner en même temps. Un delay d'origine du xd devant un Grain devrait fonctionner. À vérifier sur la machine.

## Charger une unité dans le xd

1. Récupère le fichier `.mnlgxdunit`. Il est dans l'onglet Actions du repo (dernier build, artefact `units-and-demos`), ou tu peux le compiler (voir plus bas).
2. Ouvre le **minilogue xd Sound Librarian** de Korg et branche le xd en USB.
3. Glisse le fichier dans un slot libre (User Oscillators ou User Modulation FX), puis envoie-le au synthé.
4. Sur le xd, sélectionne l'unité dans le MULTI ENGINE en mode USR, ou dans la section MOD FX en mode USER.

En ligne de commande, `logue-cli` (dans `logue-sdk/tools/logue-cli`) fait la même chose.

## Compiler

```
git clone --recursive https://github.com/Blasto303/minilogue-xd-waveshaper
cd minilogue-xd-waveshaper
git -C logue-sdk submodule update --init platform/ext/CMSIS
make           # compile toutes les unités
make render    # -> renders/*.wav
```

Il faut `arm-none-eabi-gcc` (paquet `gcc-arm-none-eabi` sous Ubuntu/Debian, ou la toolchain Korg via `logue-sdk/tools/gcc`), `make` et `zip`.

Le Makefile ne suit pas les modifications des fichiers `.hpp`. Après une modification d'un fichier de `common/` ou d'un `*_core.hpp`, lance `make clean` avant `make`.

## Écouter avant de charger

`make render` compile le même code DSP pour l'ordinateur et écrit des WAV en 48 kHz dans `renders/`.

**Oscillateur (mono)**

| Fichier | Contenu |
|---|---|
| `01_shape_sweep_*.wav` | SHAPE de 0 à 100 % puis retour, pour chaque type, sur un do grave |
| `02_bias_sweep_sinefold.wav` | Balayage du biais (SHIFT+SHAPE) |
| `03_source_sub_lfo.wav` | Morph sinus vers triangle, sub à 60 %, pli modulé |
| `04_aliasing_ab_off_then_on.wav` | Note aiguë à pli max : 2 s sans ADAA, puis 2 s avec |
| `05_bass_phrase_pluck.wav` | Basse, le pli retombe à chaque note |
| `06_lead_keytrack_saturate.wav` | Lead en saturation avec KeyTrack |

**Swarm (mono, sauf la dernière)**

| Fichier | Contenu |
|---|---|
| `30_swarm_morph_sweep.wav` | MORPH parcourt les 8 points et revient, sur un do grave, Span à 15 % |
| `31_swarm_warp_sweep.wav` | WARP de tout à gauche (modulation de phase) à tout à droite (modulation de fréquence) |
| `32_swarm_seeds.wav` | 6 valeurs de Seed, 2 s chacune |
| `33_swarm_algos.wav` | Les 4 Algo, 3 s chacun, sur une petite phrase |
| `34_swarm_field_sweep.wav` | Field de tout à gauche à tout à droite |
| `35_swarm_fuse_sweep.wav` | Fuse de tout à gauche à tout à droite |
| `36_swarm_through_dngvoid.wav` | La chaîne complète : Swarm → filtre et VCA (approximation du xd) → DngVoid, en stéréo |

**Effets (stéréo)**

| Fichier | Contenu |
|---|---|
| `10_dry_pad.wav`, `10_dry_drums.wav` | Sources sans effet, pour comparer |
| `11_freqshft_sweep_pad.wav` | Pad, décalage balayé de 0 à +300 Hz puis jusqu'à -300 Hz |
| `12_freqshft_barberpole_pad.wav` | Barber pole : +0,4 Hz pendant 4 s, puis -0,4 Hz |
| `13_freqshft_metal_drums.wav` | Batterie à +45 Hz puis -45 Hz, 60 % d'effet |
| `14_spiral_up_arp.wav` | Arpège, échos qui montent de 40 Hz à chaque passage |
| `15_spiral_down_arp.wav` | Arpège, échos qui descendent de 15 Hz, feedback max |
| `16_freqshft_max_feedback_sweep.wav` | Test de stabilité : DEPTH à 100 %, TIME balayé sur toute la course |
| `20_dungeon_from_silence.wav` | Aucune entrée : chaque variante démarre seule (9 s chacune : Growl, Metal, Bell, Void) |
| `21_dnggrowl_pad.wav` | Pad, FEED à gauche, poussé jusqu'à l'auto-oscillation |
| `22_dngmetal_drums_feed_sweep.wav` | Batterie, 45 ms, FEED balayé de tout à gauche à tout à droite |
| `23_dngmetal_arp_time_bend.wav` | Arpège, puis TIME tourne pendant que ça crisse |
| `24_dngvoid_pad.wav` | Pad, côté encoches |
| `25_dngbell_pad.wav` | Pad, mêmes gestes que la démo 21 |
| `26_dungeon_stress_test.wav` | Test de stabilité : 4 variantes, FEED aux 2 extrêmes, 10 ms et 1,5 s |
| `40_grncloud_arp_density.wav` | Arpège, DENSITY du centre vers la gauche (régulier), puis du centre vers la droite (aléatoire) |
| `41_grncloud_freeze_pad.wav` | Pad capturé, puis FREEZE à 5 s : les grains rejouent la mémoire figée |
| `42_grnshimmer_pad.wav` | Pad dans le shimmer |
| `43_grndust_drums.wav` | Batterie en poussière, grains aléatoires |
| `44_grndust_arp_regular.wav` | Arpège, grains réguliers, POSITION qui recule |
| `45_grain_stress_test.wav` | Test de stabilité : 3 variantes, DENSITY aux 2 extrêmes, position courte et FREEZE |

## Structure

```
common/
  dsp_math.hpp            sinus, racine, soft clip (sans libm)
  freqshift_core.hpp      DSP du frequency shifter
  dungeon_core.hpp        DSP de Dungeon (réutilise le Hilbert du shifter)
  grain_core.hpp          DSP du granulaire
oscillators/waveshaper/
  waveshaper_core.hpp     DSP du waveshaper
  waveshaper.cpp          branchement à l'API oscillateur du xd
oscillators/swarm/
  swarm_core.hpp          DSP de Swarm
  swarm.cpp               branchement à l'API oscillateur du xd
effects/freqshift/        FreqShft : freqshift.cpp + projet MOD FX
effects/spiral/           Spiral : même source, compilée avec -DFS_SPIRAL
effects/dungeon/          dungeon.cpp + 4 projets DELAY (growl, metal, bell, void)
effects/grain/            grain.cpp + 3 projets REVERB (cloud, shimmer, dust)
tools/render/             rendu WAV sur ordinateur
logue-sdk/                sous-module : https://github.com/korginc/logue-sdk
```

Chaque projet contient aussi `manifest.json` (nom et paramètres affichés sur le xd), `Makefile`, `project.mk` et des copies des dossiers `tpl/` et `ld/` du template Korg (licence BSD).

## Limites connues

- Rien n'est encore testé sur le synthé.
- WaveShpr, aliasing mesuré à pli maximal sans KeyTrack : invisible jusqu'à environ 260 Hz (sous le plancher de mesure de -43 dB). Vers 1 kHz, il monte à -28 dB (sine fold) et -29 dB (triangle fold). KeyTrack à 100 % le ramène à -32 dB à 1 kHz.
- WaveShpr : la charge CPU est à vérifier en polyphonie 4 voix.
- Swarm : 4 oscillateurs par voix, avec modulation croisée à chaque échantillon. C'est l'unité la plus gourmande du repo, la charge CPU en 4 voix est le premier point à tester.
- Swarm : les tables d'ondes ont 32 harmoniques. Au-dessus d'environ 750 Hz de fondamentale (sol 5), les harmoniques hautes replient. WARP et Span montent aussi certains oscillateurs au-dessus de la note.
- Swarm : les tables sont calculées au chargement de l'oscillateur. À vérifier qu'il n'y a pas de blanc audible quand on le sélectionne.
- Swarm : Field et Fuse sont bipolaires. Je ne sais pas si le xd les initialise au centre (0 %) ou en butée gauche (-100 %) au premier chargement.
- Les unités sont compilées avec GCC 13 au lieu du GCC 5.4 fourni par Korg. L'exemple officiel `waves` compile et se package de la même façon, mais seul un test sur la machine le confirmera.
