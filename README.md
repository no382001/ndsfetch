# ndsfetch

downloads and launches NDS ROM files over WiFi on a DSi.

hacked together in an afternoon. only tested on an NDSi. barely works, but does the job, if it crashes, atleast you will have your latest rom saved on your sdcard.

## what it does

- connects to a WiFi network (WEP or WPA2)
- fetches an `.nds` file from an HTTP server you're running somewhere
- saves it to the SD card
- optionally launches it using the TWiLight Menu++ ARM7/ARM9 bootloader hacks

## building

requires [BlocksDS](https://blocksds.github.io/docs/) and the Wonderful toolchain. a Docker setup is included if you don't want to install them:

```sh
just build
```

or directly with make if you have the toolchain installed.

## Usage

drop `ndsfetch.nds` on your SD card and launch it via TWiLight Menu++ or similar.

on first run, press **Y** to set your server IP, port, ROM path, and WiFi credentials. These are saved to `ndsfetch.cfg` on the SD card. In plain text! You can pre-load this into `/` save some time.

- **A** — download and launch the configured ROM
- **X** — browse the server's file listing and pick a ROM
- **Y** — edit settings
- **START** — reboot

the server just needs to serve files over plain HTTP. a directory listing (like `python3 -m http.server`) works fine for browsing.

## Notes

- ROM size limit is 4MB
- the bootloader is vendored from [TWiLight Menu++](https://github.com/DS-Homebrew/TWiLightMenu) — all code in `bootloader/vendor/` is from that project
- everything in `source/` is my own code
- DSi only — the WPA2 support and internal SD access are DSi-specific, plus i only ever tried it on a DSi
- for the best result i suggest running a `copyparty` instance and exposing a folder where you upload and pull from