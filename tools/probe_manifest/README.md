# probe_manifest

An xsdk title that saves the avatar description of every profile signed in on the console: the 1000 bytes that say
which assets an avatar wears, in which colours (FORMATS.md, section 4.6). It reads only; it changes nothing on the
console.

For each of the four users who is signed in, it reads the description two ways and writes what comes back beside
the title:

1. `XamAvatarGetManifestLocalUser`, as the library does: `manifest_u<n>_avatar.bin`, the 1000 bytes themselves.
   This is the file to use.
2. `XamUserReadProfileSettings` for the avatar setting (id `0x63E80044`), once with title 0 and once with the
   dashboard's title `FFFE07D1`: `manifest_u<n>_setting_<title>.bin`, the setting record with the same 1000 bytes
   after it. Kept for comparing the two ways.

Everything it does goes to `game:\probe_manifest.log`, with the fields the description is known to hold, in hex.

## Build and run

In WSL, from the repository's top folder, naming your xsdk checkout:

```
tools/probe_manifest/build.sh <xsdk checkout>
```

Sign in the profiles you want, copy `build/probe_manifest/probe_manifest.xex` to a folder on the console, launch it
from the dashboard, and fetch the `.bin` files and the log beside it.

## Using the files

- **On the PC:** `build/pc/outfit_check <FFFE07DF00000002> manifest_u0_avatar.bin [<folder of item packages> ...]`
  puts that avatar together as the console would.
- **As NPCs:** `xsdk_avatar_load_description` loads one in a title; the store sample loads every `.bin` in an
  `Avatars` folder beside it.

A description is a player's own: keep it, and anything made from it, out of any repository unless its owner agrees.
