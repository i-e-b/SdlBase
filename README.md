# SdlBase

A minimum dependency experimental CLion project for larger game and graphics projects.

This will be a port from a bunch of other projects. Don't expect anything
useful for quite a while.

This should be pretty plain C code.

## The plan

* Bring in the container structures and SDL hook-ups from MECS, but not the compiler / runtime engine.
* Port the graphics format and compression bits in ImageTools from C#
* Port StreamDb over as a file format

### Using the base for projects

Your application-specific code should go in `src/app`.
Start with the event handlers and frame routines in src/app/app_start.c