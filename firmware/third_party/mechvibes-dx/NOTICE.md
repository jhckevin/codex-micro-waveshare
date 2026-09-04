# MechvibesDX audio attribution

The generated PCM data in
`components/codex_audio/generated/switch_samples_generated.cpp` is derived
from the `Cherry MX Red ABS` and `Cherry MX Brown ABS` config-v2 sound packs
distributed with MechvibesDX.

- Project: https://github.com/hainguyents13/mechvibes-dx
- Source revision: `fa3da3a46985687696bc335714fc7679ea4fe07f`
- Source files: `soundpacks/keyboard/cherrymx-red-abs` and
  `soundpacks/keyboard/cherrymx-brown-abs`
- License: MIT; see `LICENSE` in this directory
- Transformation: four key variations per profile, separate key-down and
  key-up ranges from each config-v2 definition, mono PCM16 at 22.05 kHz,
  DC removal, conservative normalization, and short edge fades.

The original OGG files are intentionally not redistributed in this firmware
repository.
