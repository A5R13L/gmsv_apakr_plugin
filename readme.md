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
| Server Lag  | ❌ During auto refresh & pack building.                | ✔ No lag due to multi-threading.                                                                                    |
| Connections | ❌ Blocks connections during pack build & upload.      | ✔ Connection is not blocked. `IClient::Reconnect` is used during build.                                                                                       |
| Loading     | ❌ Kicks people who are loading in during pack build.  | ✔ Loading is as normal, when pack is rebuilt, `IClient::Reconnect` is called.                                                                     |
| Reliability | ❌ Will turn off completely if health check fails.     | ✔ Seamless and does not require health checks.                                                                                             |
| Performance | ❌ Uses C->Lua->C bridging which is slow.              | ✔ Does not interact with Lua states besides setting a boolean.                                                                                                |
| Usability   | ❌ Cannot use same-server FastDL for download.         | ✔ Can use same-server FastDL via `apakr_clone_directory`. |

### Information

`apakr_clone_directory` - You can use this to specify an absolute path to clone the data packs to that directory.

> Example: `apakr_clone_directory "/fastdl/"` - If you use `Pterodactyl` or `WISP` and set up a mount path, you can have it directly copy the files to it. Thus using the full capabilities of FastDL if it is on the same machine.

`apakr_upload_url` - You can modify this to use your own data pack uploading & serving infrastructure.

> AWS & Cloudflare are both used by default which have very little, if any, downtime. Caching is also used to speed up downloads for data packs (which are already very quick to download).

> You can change the encryption method if you want, you just need to ensure they are exactly the same on the client & server. The logic is contained in `source/apakr/plugin/encryption.h`.

Please keep the credits in `source/apakr/plugin/shellcode.h`.

Data packs support up to 16.7 (`FFFFFF`) mega-byte lua files each. If you go over this limit, consider chunking the file.
> This limit can be changed, however you will need to adjust the `PaddedHex` amount as well as the lua parser in the shellcode.

Please do not try and sell this or claim it as your own work. This isn't a big ask, just don't be malicious.

This is heavily inspired by gluapack, however is very different (and in my opinion, a lot better).

## Preprocessors

Preprocessors are essentially macros you control, you can use them to write custom syntax, have helper-esque calls, and much more.

This feature works with auto-refreshes on both the server & client.

To use them, simply create an `apakr.templates` file in the `garrysmod` directory of the server (commonly `/home/container/garrysmod`).

They are `JSON` format, and should be an array of objects containing a `Pattern` regex and `Replacement` regex.

An example of how they can be used is below:

```json
[
    {
        "Pattern": "local (\\w+): (\\w+) = (.+)",
        "Replacement": "local $1 = $3\nassert(type($1) == \"$2\", \"$1 is not a $2\")"
    },
    {
        "Pattern": "-MACRO CHECK-\\(\\)",
        "Replacement": "print('omg it works!')"
    },
    {
        "Pattern": "case\\s+\"([^\"]+)\"\\s*: \\s*\\{([^}]*)\\}",
        "Replacement": "if case(\"$1\") then\n    $2\nend\n"
    },
    {
        "Pattern": "switch\\(([^)]*)\\)\\s*\\{([^}]*)\\s*\\}",
        "Replacement": "switch($1)\n$2"
    }
]
```

This set of preprocessors allow a rudimentary way to do type checking, switch formats, and case formats.

You can also use them to implement optional chaining (`a?.b?.c?.d`) and much more.

```lua
switch(value) {
      case "apple": {

      }
}
```

turns into

```lua
switch(value)
if case("apple") then

end
```

## Infrastructure

AWS - API Gateway (Request Router) -> Lambda (Request Processor) -> S3 (Storage) -> Cloudfront (Caching)
Cloudflare - Caching & Protection
