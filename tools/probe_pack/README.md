# probe_pack

An xsdk title that copies the system's avatar asset pack off the console, where FTP can't open it
(SPECS.md, section 4.3). It reads only; it changes nothing on the console.

1. Opens `FFFE07DF00000002` on the system extended partition (`\SEP\...` and
   `\Device\Harddisk0\SystemExtPartition\...`), sharing all access, and copies it to `game:\FFFE07DF00000002`.
   It lists that folder first.
2. Lists the kernel's object directories `\`, `\Device` and `\??`, with each symbolic link's target, and tries
   `<entry>\AvatarAssetPack.toc` under every entry, in case XAM has the package mounted. The first that opens is
   copied to `game:\AvatarAssetPack.toc`.

Everything goes to `game:\probe_pack.log`. The object-directory calls are untested on the Xbox 360 (their arguments
come from the original Xbox kernel, nxdk), so each is logged before it runs: if the title crashes, the log's last line
says where.

## Build and run

```
wsl.exe -d Debian --exec bash -c 'cd /mnt/c/Users/fabia/projekte/xbox360_avatar_lib && tools/probe_pack/build.sh'
```

Copy `build/probe_pack/probe_pack.xex` to a folder on the console, launch it from Aurora, wait until it returns
to the dashboard, then fetch `probe_pack.log` and any `FFFE07DF00000002` or `AvatarAssetPack.toc` beside it.
Put the copied files in `references/`, never in the repository.
