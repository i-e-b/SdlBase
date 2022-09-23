# SdlBase

A minimum dependency experimental CLion project for larger game and graphics projects.

This will be a port from a bunch of other projects. Don't expect anything
useful for quite a while.

This should be pretty plain C code.

## The plan

* [x] Bring in the container structures and SDL hook-ups from MECS, but not the compiler / runtime engine.
* [ ] Add joystick stuff (supporting multiple)
* [ ] frame-delay guessing to reduce latency (need to change buffering strategy)
* [ ] Bitmap drawing for the scan buffer thing (rectilinear, then affine)
  * [x] texture atlas drawing
    * [x] fix uncovering issue
    * [x] allow materials to have offsets & offset mode (screen, edge)
    * [x] edit material (change Z, change offset)
  * [ ] PNG loading (PicoPNG? SDL?)
  * [ ] some tool to pack sprites down to minimal size
  * [ ] some format to save/load texture atlas material defs.
  * [ ] sprite splat
* [ ] Easy way to mix plain graphics and the scan-buffer/depth graphics
* [ ] Port the graphics format and compression bits in ImageTools from C#
* [ ] Port StreamDb over as a file format

### Using the base for projects

Your application-specific code should go in `src/app`.
Start with the event handlers and frame routines in src/app/app_start.c