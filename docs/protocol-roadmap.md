# Protocol Roadmap

`rmtdos-file-commander` can already use the rmtdos-cga-web stop-and-wait file
transfer packets for single-file upload and download. A real commander workflow
needs the DOS TSR to grow a small file-system command surface.

## Phase 1: Directory Listing

Add request/response packet types for listing a DOS directory:

- `V1_DIR_LIST_BEGIN`: client asks the TSR to open/list a DOS path.
- `V1_DIR_LIST_DATA_REQ`: client asks for a chunk of serialized entries.
- `V1_DIR_LIST_DATA`: TSR returns entries and status.
- `V1_DIR_LIST_END`: client releases any TSR-side listing state.

Suggested entry fields:

- DOS filename, including 8.3 display name initially.
- attribute byte.
- file size.
- DOS packed date and time.

The first implementation can be flat and stop-and-wait like file transfer. Long
filename support is not required for MS-DOS 5 / HP 200LX targets.

## Phase 2: Remote Path State

The current `--put` and `--get` protocol treats remote filenames as names in the
DOS current directory. The commander needs explicit path handling:

- absolute DOS paths in transfer requests, or
- a `V1_CHDIR` command that sets TSR-side file-manager context per session.

Absolute paths are easier to reason about in a two-pane UI and survive packet
loss better because each command is self-contained. A practical limit of 128
bytes is enough for classic DOS paths.

## Phase 3: Mutating Operations

Once listing and paths are stable:

- `V1_MKDIR`
- `V1_DELETE`
- `V1_RENAME`

These should share a small generic operation ACK with an rmtdos-specific status
code plus DOS error code where available.

## UI Contract

The Linux file manager should treat every remote operation as asynchronous and
recoverable. The DOS side runs work from idle-time hooks, so large operations
may need visible progress and retry/cancel affordances.

