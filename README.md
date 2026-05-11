# rmtdos-file-commander

`rmtdos-file-commander` is a Midnight Commander / Norton Commander-style
ncurses file manager for Linux machines talking to a DOS host running the
`rmtdos-cga-web` TSR.

The first build is intentionally small and protocol-aware:

1. Probe the LAN for rmtdos servers.
2. Select a DOS machine.
3. Open a dual-pane commander UI.
4. Treat the left pane as the remote DOS side.
5. Treat the right pane as the local Linux side.
6. Copy files with the existing rmtdos-cga-web `--put` and `--get` packet flow.

Remote directory browsing requires a `cgaweb.com` build that includes the
`V1_DIR_LIST_*` protocol extension. Older TSR builds still work for prompted
single-file transfers, but the remote pane cannot list directories.

## Current Status

- LAN server selector: implemented.
- Dual-pane ncurses shell: implemented.
- Local Linux directory listing and navigation: implemented.
- Linux to DOS upload: implemented with existing `V1_FILE_PUT_*` packets.
- DOS to Linux download: implemented by prompting for a remote filename and using
  existing `V1_FILE_GET_*` packets.
- Remote DOS directory listing: implemented with the matching updated
  `cgaweb.com` TSR.
- Remote chdir/path handling: implemented client-side for directory browsing.
- Remote mkdir/delete/rename: documented future work.

## Build

Install ncurses development headers, then build on Linux:

```sh
make
```

The binary is written to:

```sh
out/rmtdos-file-commander
```

Like `rmtdos-cga-web-client`, the program uses raw Ethernet frames and normally
needs root or Linux capabilities:

```sh
sudo ./out/rmtdos-file-commander -i enp2s0
```

Optional EtherType override:

```sh
sudo ./out/rmtdos-file-commander -i enp2s0 -e 80ab
```

## Keys

- `0`-`9`: select a DOS host from the startup selector.
- `Tab`: switch between remote and local pane focus.
- `Up` / `Down`: move selection in the focused pane.
- `Enter`: enter the selected directory, upload a selected local file, or
  download a selected remote file.
- `u`: upload the selected local file to the focused remote DOS directory.
- `d`: download the selected or prompted DOS filename into the current local
  directory.
- `r`: refresh the focused directory listing.
- `q`, `Esc`, `Ctrl-]`: quit.

## Relationship to rmtdos-cga-web

This is a new standalone project, not a fork. It reuses the Linux-side raw
Ethernet, host discovery, and file-transfer protocol ideas from
`rmtdos-cga-web`, and is licensed GPL-2.0-or-later to remain compatible with
that foundation.

See [docs/protocol-roadmap.md](docs/protocol-roadmap.md) for the proposed next
TSR protocol features.
