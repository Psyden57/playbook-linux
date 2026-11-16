# 251 — `epitaph`'s panel segfault fixed: the glyph atlas, and the dual-source text draw

Date: 2026-11-16 (session 60, Linux-era 54).
Bench: the Nura root read-write from hd3, region 0 **`0x294F8E36`** (kernel
`7.3.3 #77`), grown to 56.5 G with SSH.  The patched `epitaph` 1.4.0 is
installed as `1.4.0-r10005`; the panel now starts, stays up, and renders its
text and icons.

## 0. Headline

* **The crash was a texture created without a GL context.**  `epitaph`'s lazy
  `SizedRenderer` rewrite (upstream commit `5696bdf`, first released in
  v1.0.3) builds the `GlRasterizer` — and with it the 4096×4096 glyph-atlas
  texture — in `Renderer::new`, **before the EGL context exists**.  GL calls
  without a current context are silent no-ops, so `glGenTextures` leaves the
  name at 0 and `glTexImage2D` defines nothing; once the context is current,
  the first `glTexSubImage2D` fails with
  `GL_INVALID_OPERATION (invalid texture level 0)` and sampling that
  undefined texture crashes the software rasterizer.
* **The text shader's dual-source path is a second crash.**  The fragment
  shader requires `GL_EXT_blend_func_extended` and writes
  `gl_SecondaryFragColorEXT`.  Amber Mesa **softpipe advertises the extension
  and compiles the shader**, but drawing with it segfaults (same null
  dereference in `swrast_dri.so`); with the extension disabled at runtime the
  app survives.
* **Fix:** create the rasterizer with the EGL context in `SizedRenderer::new`,
  cap the atlas at `GL_MAX_TEXTURE_SIZE`, and replace the dual-source text
  shader with an equivalent single-output premultiplied one
  (`ONE`/`ONE_MINUS_SRC_ALPHA`).
* **Verified:** `timeout 6 epitaph` returns 143 (alive), and a `grim`
  screenshot shows the bar with `Thu. 1`, `03:20` and the signal/Wi-Fi/battery
  icons.
* The panel's permissions are installed too: group `catacomb`, the
  backlight udev rule and the Wi-Fi/cellular polkit rule.

## 1. The driver, measured

`epitaph` is a Wayland EGL client, so it does **not** get the PowerVR path the
compositor uses: the log says

```
epitaph: GL renderer "softpipe", version "OpenGL ES 3.1 Mesa 21.3.9 Amber (git-53b2b224dc)", dual-source=true
```

`softpipe` lives inside `swrast_dri.so` (`/usr/lib/xorg/modules/dri/`), which
is why the earlier `LD_PRELOAD` tracer attributed the fault there.  The
`MESA_LOADER_DRIVER_OVERRIDE=pvr` in the tinydm env does not give a Wayland
window client the SGX path.

Two limits were measured with small ctypes EGL/GLES2 probes:

| context | `GL_MAX_TEXTURE_SIZE` | 4096×4096 `TexImage2D` |
|---|---|---|
| SGX540 (DDK, surfaceless) | **2048** | `GL_INVALID_VALUE` (0x501) |
| softpipe (surfaceless) | 16384 | OK |

So the atlas's 4096 is wrong for the SGX540 (and for any 2048 driver), and the
fixed 4096 also hides the context-order bug.

## 2. The context-order proof

A probe created a texture **before** `eglMakeCurrent`, then uploaded after it:

```
NO-CONTEXT: GenTextures -> id=12345 err=0x0     (name unchanged — the call was a no-op)
NO-CONTEXT: TexImage2D err=0x0                  (no error, nothing defined)
CONTEXT NOW: PowerVR SGX 540
TexSubImage2D(id=12345) err=0x502              (GL_INVALID_OPERATION, invalid level 0)
```

That is exactly the error `epitaph` logged on every run.  In the app the name
starts at 0, so the upload targets the default texture and the first text draw
samples an incomplete texture.

## 3. The dual-source proof

With r10004 (atlas fix, original shader) the app still segfaults.  With the
extension removed from the context:

```
MESA_EXTENSION_OVERRIDE=-GL_EXT_blend_func_extended
epitaph: … dual-source=false
epitaph: shader (type 35632) compile failed: … extension `GL_EXT_blend_func_extended' unsupported
epitaph: text program failed to link …
RC=143   (alive; no glyph draw happened)
```

So the crash is the **draw** with the advertised dual-source path, not the
atlas alone.  (The atlas bug is still real: it produced the 0x502 error and
would break the glyph upload on its own.)

## 4. The patch

`local/work/nura/pmaports/main/epitaph/fix-renderer-init.patch` (324 lines,
sha512 `f035f4c5…`) against `epitaph` v1.4.0:

* `src/renderer.rs`: `Renderer` keeps the font name/size instead of a
  `GlRasterizer`; `SizedRenderer::new` creates the rasterizer **after**
  `create_surface()` has made the context current.  The text renderer uses
  `glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)`.  A short
  `epitaph: GL renderer … dual-source=…` line and shader/program link-failure
  logs were added.
* `src/text.rs`: `Atlas::new()` queries `GL_MAX_TEXTURE_SIZE`, uses
  `min(4096, max)` and creates the first texture; `insert()` uses the stored
  size for the row/UV math.  `Atlas::new()` replaces `Atlas::default()` in
  `GlRasterizer::new` and `set_scale_factor`.
* `shaders/text.f.glsl`: single output, `precision mediump float;`,
  `gl_FragColor = tex` for multicolor glyphs and `vec4(tex.rgb, tex.r)` for
  coverage glyphs (premultiplied white).  The atlas stores colour glyphs
  premultiplied (tiny-skia/resvg) and grayscale glyphs as coverage in RGB, so
  the result is the same as the dual-source version.

## 5. Verification

```
epitaph: GL renderer "softpipe", version "OpenGL ES 3.1 Mesa 21.3.9 Amber (git-53b2b224dc)", dual-source=true
RC=143   (timeout 6, no crash)
```

`grim /tmp/panel2.png` with `foot` closed shows the bar: **`Thu. 1`** at the
left, **`03:20`** in the centre, and the cellular, Wi-Fi and battery glyphs at
the right.  The panel process stays up (PID 12840 for the whole session after
the test).  The only log line is the long-standing benign UPower
`GetAll` warning on `/`.

## 6. Permissions

`epitaph`'s own `rules/` files were installed (it ships them but Alpine's
APKBUILD does not):

* group `catacomb` created (gid 111), `playbook` added;
* `/etc/udev/rules.d/66-epitaph.rules` — `chgrp catacomb` + `chmod g+w` on the
  backlight (and the absent flashlight) brightness;
* `/etc/polkit-1/rules.d/66-epitaph.rules` — `NetworkManager.enable-disable-wifi`
  and `ModemManager1.Device.Control` for group `catacomb`;
* the current `/sys/class/backlight/backlight/brightness` was chgrp'd
  immediately (`-rw-rw-r-- root:catacomb`) and `udevadm trigger` run.

Measured: `su playbook -c 'v=$(cat …); echo $v > …'` → `BACKLIGHT_WRITE_OK`.
(Whether the Wi-Fi button now succeeds needs a tap; the polkitd rule file is
in place and polkitd was HUP'd.)

## 7. Build and package

A local pmaport `main/epitaph/` (Alpine's APKBUILD + the patch) builds in the
pmbootstrap armv7 chroot:

```
local/work/nura/work/packages/edge/armv7/epitaph-1.4.0-r10005.apk
1 842 006 B, sha256 aee3fdc8…
```

The APKBUILD sets `CARGO_TARGET_DIR=/home/pmos/cache/cargo-target-epitaph` so
future rebuilds reuse the compiled dependency graph.  Builds are slow because
`--no-cross` runs the whole armv7 toolchain under qemu-user (crossdirect is
blocked by a host/chroot rustc mismatch) and abuild wipes `$srcdir` each time;
`zbus`/`zvariant_derive` and `resvg` dominate.

## 8. Remaining

* **~2 s slider/button latency** — the whole-surface redraw on softpipe plus
  the compositor's upload path, not input delivery.  This is the tracked
  upload-path item (`PBGL_PROFILE`); the simpler shader may help a little.
* **Volume is not in the 1.4.0 drawer.**  The drawer is three sliders
  (brightness, DPI scale, flashlight) and four toggles (cellular, Wi-Fi, GPS,
  orientation lock).
* The panel is a `Layer::Bottom` surface with an exclusive zone, so a
  fullscreen toplevel (`foot`, `netsurf`) covers it; it is visible over the
  wallpaper only.
