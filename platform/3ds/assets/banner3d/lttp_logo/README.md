# lttp_logo Blender Banner Source

This directory preserves the HOME Menu banner source for v2.8.4.

- `lttp_logo.blend` is the supplied Blender source model.
- `lttp_logo_banner.gltf` and `.bin` are the exported banner scene.
- `logo_base_saturated_128.png` is the 128x128 diffuse texture;
  `logo_normal_64.png` is its 64x64 normal map. The older filenames remain as
  aliases from the original export.
- `../../banner.cgfx` is the generated Nintendo 3DS banner model used by the
  build.

The source Blender file contains 1,734 vertices and 2,620 polygons across its
two meshes. The CGFX conversion is sized to leave room for the banner audio
inside the 512 KB HOME Menu banner limit.

The original Blender file refers to external texture paths. This export relinks
the available project diffuse and normal maps, then downscales them so the
complete CGFX remains valid for Nintendo 3DS banners.

For v2.8.4, the exported logo is scaled to 0.19 (from 0.16), its diffuse
texture receives a restrained 18% saturation increase, and the PBR roughness
is increased to 0.84. The latter retains a small specular response while
reducing the glossy appearance in the HOME Menu lighting.

Credit: Phibonacci (https://github.com/Phibonacci) for the logo work.
