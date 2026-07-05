About this fork
----------------

This is a fork of OpenMW 0.51 designed for extracting map images from Morrowind.
The tool generates a world height map at a resolution of 32x32 pixels per cell, local map tiles at 256x256 pixels per cell, and a world map composed of these tiles.


OpenMW
======

* License: GPLv3 (see [LICENSE](https://gitlab.com/OpenMW/openmw/-/raw/master/LICENSE) for more information)
* Website: https://www.openmw.org

Font Licenses:
* DejaVuLGCSansMono.ttf: custom (see [files/data/fonts/DejaVuFontLicense.txt](https://gitlab.com/OpenMW/openmw/-/raw/master/files/data/fonts/DejaVuFontLicense.txt) for more information)
* DemonicLetters.ttf: SIL Open Font License (see [files/data/fonts/DemonicLettersFontLicense.txt](https://gitlab.com/OpenMW/openmw/-/raw/master/files/data/fonts/DemonicLettersFontLicense.txt) for more information)
* MysticCards.ttf: SIL Open Font License (see [files/data/fonts/MysticCardsFontLicense.txt](https://gitlab.com/OpenMW/openmw/-/raw/master/files/data/fonts/MysticCardsFontLicense.txt) for more information)


Usage
-----

1. In the `openmw-launcher` (from this fork or any OpenMW >= 0.49), select the content list you want to extract the map from.
2. On the Display tab, it is recommended to set the game to windowed mode with the minimum resolution (640x480) so the extraction can run in the background. Warning: minimizing the game window will pause the process.
3. Run the `openmw` executable from this fork (with or without command-line options) and wait for the generation to complete.

When extraction finishes, the message "Map extraction complete." will be shown; at that point, you can close the game.

By default textures are written to `./textures/advanced_world_map/` inside the application directory.


Note about scripts
------------------

Files with the `.omwscripts` extension are not loaded by this build.


Command-line options (fork-specific)
-----------------------------------
```
--world-map-output=""            Directory to save the world map texture. Default: `./textures/advanced_world_map/custom/`
--local-map-output=""            Directory to save local map textures. Default: `./textures/advanced_world_map/local/`
--clear-output-dirs              Clear output directories (png and yaml files) before starting.
--overwrite-maps                 Allow overwriting existing local map files. By default local maps are not overwritten.
--tilemap-downscale-factor=-1    If set to a positive integer N, enables generation of the world map texture based on local map tiles downscaled by a factor of N.
--world-map-pixelsPerCell=32     The number of pixels per cell for the world map texture. Default: 32.
--world-map-border=2             The number of pixels to add as a border around terrain above water level in the world map texture. Default: 2.
--world-map-disable-water-alpha  Disable water transparency for the world map texture.
--local-map-size=256             Size of output local map images in pixels. Default: 256. Overrides 'local map resolution' setting from settings.cfg.
--use-original-settings          Use settings for resolution, window mode and some graphics options from settings.cfg instead of forcing windowed mode with minimum resolution.
--use-constant-scale             Use a constant scale for interior local map textures instead of scaling them to fit the unused texture space.
```

Notes
-----

This fork was created for personal use and is not intended for major development. Much of the code in this repository was generated with assistance of an LLM; the author does not know C++ at all.
