Resident model room for `GGUF_Context_Fix`.

At runtime this folder receives:
- `<model>.manifest.json`
- local hotcache path ownership for `<model>.hotcache`

The Falcon GGUF stays at its real model path, but this package records the roommate manifest here so the model and runtime live under one coherent house surface.
