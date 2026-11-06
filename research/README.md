# research

Python prototypes behind SPECS.md sections 3 and 4 (Python 3, standard library only; run them in WSL). Our own code; no third-party code copied.
They need your own `asset_v2.bin` files (extract with Avatar Thrift Store's `avtool extract <package> asset_v2.bin <out>`);
never put asset files or their outputs in this repository.

```
python3 -I strb.py <dir of .bin>              # list STRB blocks
python3 -I model.py <dir of .bin> <out dir>   # LZX-expand model blocks (writes <name>.model and <name>.texture)
python3 -I mesh.py <out dir>                  # decode mesh headers, check counts against the model header
```

The system's stock pack (`AvatarAssetPack.toc`, extracted from package `FFFE07DF00000002` with
`avtool extract <package> AvatarAssetPack.toc <out>`; tools/probe_pack copies the package off the console):

```
python3 -I toc.py AvatarAssetPack.toc [n]                       # header and the first n records
python3 -I catalog.py AvatarAssetPack.toc catalog.txt           # every record: name, blocks, metadata
python3 -I dump_blobs.py AvatarAssetPack.toc <out dir> 0,1,2    # write records' STRB blobs as <index>.bin
python3 -I sweep.py AvatarAssetPack.toc                         # decode every model in the pack, check counts
python3 -I bones.py 0000.bin ...                                # bone indices and weight sums per skinned mesh
python3 -I infer_skel.py 0000.bin                               # bone tree and joint positions from skin weights
python3 -I anim_survey.py AvatarAssetPack.toc                  # every animation: header, channel count, data end
python3 -I anim.py <blob>.bin                                   # one animation: avatar and prop parts, rotation vectors of the first frames
python3 -I anim_frames.py <blob>.bin                            # SUPERSEDED: reads frames in the header's bone order (wrong); use anim.py frames()
python3 -I manifest.py <manifest.bin> AvatarAssetPack.toc        # decode a profile's avatar description, name its items
python3 -I render.py out.png 2,1.632993,1.732051 0000.bin 0002.bin ...   # flat-shaded front and side view (k per axis)
python3 -I texture.py <name>.texture <out prefix> [linear|tiled]  # a texture block (from model.py) to PNG, per frame
python3 -I model_tex.py <png dir> <name>.model ...               # a model's texture table, its textures as PNG
python3 -I tex_survey.py AvatarAssetPack.toc                     # every texture block and model texture table
python3 -I material.py AvatarAssetPack.toc                       # every model's material records
python3 -I texrender.py out.png AvatarAssetPack.toc <manifest.bin>   # textured, coloured stock outfit of a description
python3 -I shape.py AvatarAssetPack.toc [--lines]             # shape blocks (hiding lists) and record links; --lines as avatar_dump shapes
python3 -I skeleton.py <strb file> ...                         # skeleton blocks (props): parent, rest position, rotation per bone
python3 -I pose.py <prop.bin> <anim.bin> out.png [f,f,...]      # pose a prop (sheep 1097/1098, teddy 1101/1102) through its animation, draw it
python3 -I avatar_skeleton.py <body.bin> [<anim.bin> out f,f,... | --score <anim dir>]   # the derived avatar skeleton (body: record 0 or 1); pose, draw, check
```

Renders and decoded files come from console content: keep them in the scratchpad, never in the repository.
