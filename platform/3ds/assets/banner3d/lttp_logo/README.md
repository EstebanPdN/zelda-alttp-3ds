# lttp_logo 2.0 Blender Banner Source

This directory preserves the HOME Menu banner source for v2.9.2.

- `lttp_logo.blend` is the supplied `3dmodel/lttp_logo2.0` Blender source
  model.
- `lttp_logo.obj` and `lttp_logo.mtl` are the matching offline object export.
- `lttp_logo_banner.gltf` and `.bin` are the exported banner scene.
- `logo_base_saturated_128.png` is the 128x128 diffuse texture.
- `logo_normal_64.png` is the 64x64 normal map.
- `../../banner.cgfx` is the generated Nintendo 3DS banner model used by the
  build.

The original Blender file refers to external texture paths. The 1.0 and 2.0
logo folders do not include the referenced roughness texture, so this export
uses the available project diffuse and normal maps plus constant roughness.

Credit: Phibonacci (https://github.com/Phibonacci) for the logo work.
