# Minilogue-XD-Waveshaper

Oscillateur utilisateur pour le KORG minilogue xd (logue SDK 1.1). Il charge un wavefolder dans le MULTI ENGINE : une source sinus ou triangle passe dans un shaper, avec un anti-aliasing ADAA du premier ordre.

Firmware du minilogue xd requis : 2.00 ou plus.

## Contrôles

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

## Charger l'oscillateur dans le xd

1. Récupère `waveshaper.mnlgxdunit` : onglet Actions du repo, dernier build, artefact `waveshaper`. Ou compile-le (voir plus bas).
2. Ouvre le **minilogue xd Sound Librarian** de Korg, branche le xd en USB.
3. Glisse le fichier dans un slot libre de la liste User Oscillators, puis envoie au synthé.
4. Sur le xd : MULTI ENGINE en mode USR, choisis `WaveShpr`.

En ligne de commande, `logue-cli` (dans `logue-sdk/tools/logue-cli`) fait la même chose.

## Compiler

```
git clone --recursive https://github.com/Blasto303/minilogue-xd-waveshaper
cd minilogue-xd-waveshaper
git -C logue-sdk submodule update --init platform/ext/CMSIS
make           # -> oscillators/waveshaper/waveshaper.mnlgxdunit
make render    # -> renders/*.wav
```

Il faut `arm-none-eabi-gcc` (paquet `gcc-arm-none-eabi` sous Ubuntu/Debian, ou la toolchain Korg via `logue-sdk/tools/gcc`), `make` et `zip`.

## Écouter avant de charger

`make render` compile le même code DSP pour l'ordinateur et écrit des WAV (48 kHz, mono) dans `renders/` :

| Fichier | Contenu |
|---|---|
| `01_shape_sweep_*.wav` | SHAPE de 0 à 100 % puis retour, pour chaque type, sur un do grave |
| `02_bias_sweep_sinefold.wav` | Balayage du biais (SHIFT+SHAPE) |
| `03_source_sub_lfo.wav` | Morph sinus vers triangle, sub à 60 %, pli modulé |
| `04_aliasing_ab_off_then_on.wav` | Note aiguë à pli max : 2 s sans ADAA, puis 2 s avec |
| `05_bass_phrase_pluck.wav` | Basse, le pli retombe à chaque note |
| `06_lead_keytrack_saturate.wav` | Lead en saturation avec KeyTrack |

## Structure

```
oscillators/waveshaper/
  waveshaper_core.hpp   DSP, C++ sans dépendance au SDK (partagé avec le rendu)
  waveshaper.cpp        branchement à l'API oscillateur du xd
  manifest.json         nom et paramètres affichés sur le xd
  Makefile, project.mk  build via le template Korg
  tpl/, ld/             copies du template dummy-osc de Korg (licence BSD)
tools/render/           rendu WAV sur ordinateur
logue-sdk/              sous-module : https://github.com/korginc/logue-sdk
```

## Limites connues

- Aliasing mesuré à pli maximal, sans KeyTrack : invisible jusqu'à environ 260 Hz (sous le plancher de mesure de -43 dB). Vers 1 kHz, il monte à -28 dB (sine fold) et -29 dB (triangle fold). KeyTrack à 100 % le ramène à -32 dB à 1 kHz.
- Pas encore testé sur le synthé. La charge CPU sur le xd est à vérifier, surtout en polyphonie 4 voix.
- Compilé avec GCC 13 au lieu du GCC 5.4 fourni par Korg. L'exemple officiel `waves` compile et se package de la même façon, mais seul un test sur la machine le confirmera.
