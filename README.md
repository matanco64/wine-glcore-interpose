# wine-glcore-interpose

Two small fixes for OpenGL applications running under **Wine on macOS** (CrossOver, Sikarugir/Kegworks/Wineskin,
Whisky, upstream Wine), delivered as one `DYLD_INSERT_LIBRARIES` dylib. No files inside Wine or the game are modified.

Written to get [Project Reclaimer](https://projectreclaimer.dev) (Halo 3 custom games, a Rust/egui client that hosts
the MCC Halo 3 engine) running on an Apple Silicon Mac. Both fixes are generic.

## The two problems

**1. "requires OpenGL 3.0 or later" — Wine hands out GL 2.1 on macOS.**
When a Windows program creates a default (non-attributed) WGL context, Windows and Linux drivers return their newest
*compatibility* context (GL 4.x). Wine's Mac driver asks CoreGL for a *legacy* pixel format, and legacy on macOS means
GL 2.1. glutin/egui and anything else that expects "default context ≥ 3.0" fails.
The interposer hooks `CGLChoosePixelFormat` and, only for the request winemac.drv makes on behalf of the app
(accelerated, minimum policy, no renderer id, no profile), adds `kCGLPFAOpenGLProfile = GL4 core` (or 3.2 core).
Wine's own probes and renderer enumeration are left alone so its GL capability table stays correct.
macOS core profiles reject `#version 130/140`, so `glShaderSource` is hooked to rewrite those to `#version 150`.

**2. Beachball / frozen window — winemac.drv main-thread self-deadlock.**
When a window hosts both a Metal-backed surface (D3DMetal / MoltenVK swap chain) and an OpenGL context attached
after the window is already visible, winemac.drv's `viewWillDraw` → `macdrv_update_opengl_context` ends up calling
`OnMainThread(^{ [context update]; })` **on the main thread**. `OnMainThread` queues the block for the main run loop
and then waits on a dispatch semaphore forever — behind its own wait. The Cocoa main thread never services the window
again (beachball), and every Wine thread that needs the main thread hangs behind it, while the Metal side keeps
presenting at ~1 fps.
The interposer hooks `dispatch_semaphore_wait`; if the call is on the main thread, uses `DISPATCH_TIME_FOREVER`, and
the caller is inside `winemac.so`, it waits in 2 ms slices and runs the main run loop between them so the queued block
executes and signals the semaphore. All other semaphore waits pass through untouched.

Relevant upstream code (Wine 10): `dlls/winemac.drv/cocoa_window.m` (`viewWillDraw`, `addGLContext`),
`cocoa_opengl.m` (`macdrv_update_opengl_context`), `cocoa_event.m` (`OnMainThread`).

## Build

```sh
make            # -> libglcore_interpose.dylib (x86_64; Wine on Apple Silicon runs under Rosetta)
```
Requires Xcode command line tools. Nothing else.

## Use

```sh
./wine-glcore /path/to/wine 'C:\path\to\app.exe'
# or by hand:
DYLD_INSERT_LIBRARIES=/path/libglcore_interpose.dylib GLCORE_INTERPOSE=1 wine app.exe
```
The dylib does nothing unless `GLCORE_INTERPOSE=1` is set, so it is safe to leave in a wrapper's environment.

| Variable | Effect |
|---|---|
| `GLCORE_INTERPOSE=1` | enable (required) |
| `GLCORE_INTERPOSE_PROFILE=41` \| `32` | core profile version requested (default 4.1) |
| `GLCORE_INTERPOSE_PUMP=0` | disable the main-thread deadlock workaround |
| `GLCORE_INTERPOSE_LOG=1` | log each converted pixel format, shader rewrite and caught deadlock to stderr |

**Caveat:** macOS strips `DYLD_*` variables when launching protected system binaries (`/usr/bin/python3`, `/bin/sh`
from `/usr/bin/env`, etc.). If you wrap the launch in such a tool, set `DYLD_INSERT_LIBRARIES` in the *child's*
environment (see `examples/reclaimer-sikarugir.sh`).

## Example: Project Reclaimer in a Sikarugir Steam bottle

1. Install Halo: MCC with Halo 3 (campaign + multiplayer) in the bottle's Windows Steam.
2. Copy `project-reclaimer.exe` to `<bottle>/drive_c/ProjectReclaimer/`.
3. `examples/capture-sikarugir-env.sh ~/Applications/Sikarugir/Steam10.app` (once; records the wrapper's Wine env).
4. `make && examples/reclaimer-sikarugir.sh`

The example launches detached from the terminal, logs to `~/Library/Logs/ProjectReclaimer/`, and raises the game
window (a process started from a terminal is not allowed to take focus, so it would otherwise open behind Steam).

Tested: macOS 26.2, M4 Max, Sikarugir 1.0.11, Wine 10.0 (CrossOver-derived "Sikarugir rev 4") + D3DMetal,
Project Reclaimer 0.9.15. Menu, Spartan preview and music work; the Halo 3 engine renders through D3DMetal.

## Scope

Intentionally **not** a Wine patch: the deadlock belongs in `OnMainThread` (run the block inline when already on the
main thread) and the GL default-version behaviour is a design choice in winemac.drv; both are better fixed upstream.
This dylib is the zero-rebuild workaround until then. It does nothing about anti-cheat, Easy Anti-Cheat, or any
game's online services.

## License

MIT.
