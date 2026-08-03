# lttp_logo Blender Banner Source

This directory preserves the HOME Menu banner source for v2.8.3.

- `lttp_logo.blend` is the supplied Blender source model.
- `lttp_logo_banner.gltf` and `.bin` are the exported banner scene.
- `logo_base_256.png` is a 128x128 diffuse texture despite its historic export
  filename; `logo_normal_128.png` is its 64x64 normal map.
- `../../banner.cgfx` is the generated Nintendo 3DS banner model used by the
  build.

The source Blender file contains 1,734 vertices and 2,620 polygons across its
two meshes. The CGFX conversion is sized to leave room for the banner audio
inside the 512 KB HOME Menu banner limit.

The original Blender file refers to external texture paths. This export relinks
the available project diffuse and normal maps, then downscales them so the
complete CGFX remains valid for Nintendo 3DS banners.

Credit: Phibonacci (https://github.com/Phibonacci) for the logo work.
