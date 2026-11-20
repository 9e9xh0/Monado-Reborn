# ADR 0011 — Conservative optimization and explicit quality

- **Status:** Implemented · **Date:** 2026-11-04
- **Tasks:** T259–T261

The existing caches and state filtering are extended, keeping complete keys and the GPU lifetime. Temporary
resources are reused on the CPU and auxiliary indices replace linear searches without changing their precedence. No
barriers, clears or observable writes are removed to reduce calls.

Quality is an action shared by both frontends, independent of the FPS target. Dynamic resolution uses absolute limits
so that a 4K output does not turn an integrated-GPU profile into a 1080p minimum render. Profiles are not applied
when loading old files and new effects are not cut by default without visual acceptance.

Dear ImGui/SDL is kept and a simple theme is shared. No second UI platform, decorative textures or animations that
consume resources while playing are added.

Measurement distinguishes native frames, detailed instrumentation and lightweight observation. Visual equality and a
reproducible comparison are required before claiming FPS improvements. Local Clang serves to verify this environment;
it does not replace ADR 0007's decision on the default compiler.
