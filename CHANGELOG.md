V3:
Fixed scaling of local map textures.
External shaders and Lua scripts embedded in mod files are now ignored.
Added the launch parameter --local-map-size to configure the output image size of local maps.
Added the launch parameter --use-original-settings to use the resolution, window mode and some graphics options from settings.cfg instead of forcing windowed mode with minimum resolution.
Added a prompt to overwrite existing local maps.

V2:
New world map save format.
Added the ability to outline the borders of areas above water level for the world map texture.
Added new launch parameters: --world-map-border, --world-map-pixelsPerCell.
Changed the behavior of the --tilemap-downscale-factor launch parameter.