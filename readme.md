# gmsv_apakr_plugin

A Garry's Mod server plugin that speeds up Lua File downloading by packing everything into a BSP and using FastDL to download it.

### Installation Steps

* Download the respective plugin for your server. Rename it to `gmsv_apakr_plugin.so`, and place it in `lua/bin/` (Create the folder if it doesn't exist).
    * Ensure you use the proper version for your server's architecture. You can run `lua_run print((system.IsLinux() and "Linux" or "Unsupported") .. " (" .. (jit.arch == "x64" and "64" or "32") .." bit)")` in the server console to see which version you need.
* Create a file in `addons` named `apakr.vdf` and write:
    ```vdf
    Plugin
    {
        file "lua/bin/gmsv_apakr_plugin.so"
    }
    ```

### Differences

| What        | gluapack                                               | apakr                                                                                               |
| ----------- | ------------------------------------------------------ | --------------------------------------------------------------------------------------------------- |
| Server Lag  | ❌ During auto refresh & pack building.                | ✔ No lag due to multi-threading.                                                                   |
| Connections | ❌ Blocks connections during pack build & upload.      | ✔ Connection is not blocked. `IClient::Reconnect` is used during build.                            |
| Loading     | ❌ Kicks people who are loading in during pack build.  | ✔ Loading is as normal, when pack is rebuilt, `IClient::Reconnect` is called.                      |
| Reliability | ❌ Will turn off completely if health check fails.     | ✔ Seamless and does not require health checks.                                                     |
| Performance | ❌ Uses C->Lua->C communication which is slow.         | ✔ Uses C interfaces for communication, and does not use Lua states at all.                         |
| Usability   | ❌ You must self-host a php file / use given hosting.  | ✔ Relies on the server itself, along with support for external FastDL via `apakr_clone_directory`. |


### Information

`apakr_clone_directory` - You can use this to specify an absolute path to clone the data packs to that directory.

> Example: `apakr_clone_directory "/fastdl/"` - If you use `Pterodactyl` or `WISP` and set up a mount path, you can have it directly copy the files to it. Thus using the full capabilities of FastDL if it is on the same machine.

> You can change the encryption method if you want, you just need to ensure it's the same on the client & server. The logic is contained in `source/apakr/plugin/encryption.h`.

Please keep the credits in `source/apakr/plugin/shellcode.h`. Edit it with whatever you may need, but do not remove the credits.

Data packs support up to 16.7 (`FFFFFF`) mega-byte lua files each. If you go over this limit, consider chunking the file.
> This limit can be changed if necessary, however you will need to adjust the `PaddedHex` amount as well as the lua reader in the shellcode.

Please do not try and sell this or claim it as your own work. This isn't a big ask, just don't be malicious.

This is heavily inspired by gluapack, however is very different.
