# 3DS HOME Menu logo banner source

This folder contains the lightweight source assets used to generate
`../banner.cgfx` for the Nintendo 3DS HOME Menu banner.

Input source for v2.9.2:

- `lttp_logo/lttp_logo.blend` from `3dmodel/lttp_logo2.0/`
- `lttp_logo/lttp_logo.obj`
- `lttp_logo/lttp_logo.mtl`
- `lttp_logo/logo_base_saturated_128.png`
- `lttp_logo/logo_normal_64.png`

Processing:

- Exported the 2.0 Blender logo as a glTF banner scene.
- Relinked the available project diffuse and normal textures.
- Used constant roughness because the referenced roughness PNG is not present
  in the supplied 1.0 or 2.0 logo folders.
- Applied light decimation so the generated CGFX stays under the 512 KB 3DS
  HOME Menu banner limit.

The resulting `banner.cgfx` uses the 2.0 logo model and remains within the
3DS HOME Menu CGFX banner size limit.
