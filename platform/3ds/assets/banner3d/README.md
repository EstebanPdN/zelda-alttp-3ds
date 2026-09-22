# 3DS HOME Menu box banner source

This folder contains the lightweight source assets used to generate
`../banner.cgfx` for the v1.6 CIA HOME Menu banner.

Input source:

- `3dmodel/box/snes_the_legend_of_zelda_link_to_the_past_box/scene.gltf`
- `3dmodel/box/snes_the_legend_of_zelda_link_to_the_past_box/textures/SNES_baseColor.jpeg`

Processing:

- Baked the source node transforms into the mesh.
- Rotated the model so the box front faces the HOME Menu banner camera.
- Scaled the model to fit the 3DS banner camera framing.
- Downsampled the base texture to `256x256`.
- Removed normal and metallic/roughness maps for size and compatibility.

The resulting `banner.cgfx` is intentionally kept under the 3DS HOME Menu CGFX
banner size limit.
