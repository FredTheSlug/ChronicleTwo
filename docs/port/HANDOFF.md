# Native port entry point

Read [PC.md](../PC.md) for delivered behavior, build/run instructions and source exceptions. The native port and PS2 build live together; native replacements compile through `PORT_OVERRIDE` and build copies.

- Shared-code pin: read `git ls-tree HEAD tools/kazarov` for the exact dependency.
- Supported bounded checkpoints: boot/title, costume selection, opening, Palm Brinks and the first Underground Channel floor. Campaign completion and all sub-games are not established.
- Debug defaults off. Config or scene-jump opt-in and controls are described in [DEBUG.md](../DEBUG.md).
- [Headless replay timing](headless-replays.md) defines deterministic mixer ownership and capture conditions.
- [Build measurement](buildperf.md) describes native build options and measurement procedure.
- [Abort-stub inventory](stubs.md), [pointer truncations](truncations.md) and [disc records](records.md) describe remaining source and layout boundaries.

Every final candidate requires native build/test gates and a separate PAL byte-identity check after retail source edits. Headless replay identity and interactive play are separate qualifications. Historical process documents are evidence of their recorded versions; they do not override current source or these behavior contracts.
