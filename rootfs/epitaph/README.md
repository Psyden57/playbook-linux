# epitaph — SGX portability patches

The panel client (`epitaph`) is packaged from `local/work/nura/pmaports/main/epitaph`
with `pkgrel=10008`.  These two patches live here so a check out of the
repository alone can reproduce the build; the APKBUILD itself is in the
gitignored pmaports tree.

## `bind-attribs.patch` (pkgrel 10008, current)

epitaph passes literal `0/1/2` to `glVertexAttribPointer` /
`glEnableVertexAttribArray` and writes its projection with `glUniform4f(0, …)`.
GLES2 leaves those indices to the linker, and the PowerVR compiler assigns
different ones than Mesa's soft rasterizer — measured on the unit by linking
epitaph's own text shaders:

| symbol | pvr (SGX540) |
|---|---|
| `a_Position` | 2 |
| `a_UV` | 1 |
| `a_Flags` | 0 |
| `u_Projection` | 1 |
| `u_Texture` | 2 |

Under `pvr_dri` the glyph positions therefore came from the wrong attribute, the
projection was never set (glyphs collapse to a point) and the write to location
0 hit the sampler and returned `GL_INVALID_OPERATION`.  The fix binds the
attribute locations explicitly before linking and queries `u_Projection` by
name.  It is a general portability fix, not an SGX one; worth upstreaming.
Report 269 §4h, trap 348, DECISIONS §103.

## `debug-framebuffer.patch` (diagnostic only — do not ship)

Adds a one-shot `glReadPixels` histogram of the client's own framebuffer,
printed once.  It is what proved the bar was a single colour `(24,24,24)` with
`glerr = 1282` — i.e. the client drew only its clear colour.  Kept for the next
port, not in the package's `source=` list.
