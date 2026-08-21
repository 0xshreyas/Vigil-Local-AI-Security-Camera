# Prototypes

Programs that were built while developing Vigil. Each piece of the pipeline is a separate part and was verified independently before being integrated into `src/`.

- `main.c` — Reads a file into the memory with malloc/free
- `base64_test.c` — A base64 encoder program built from scratch and verified against known vectors
- `encode_image.c` — File reading + Base64 encoding + JSON Request building to the LLM Model
- `http_post.c` — HTTP Client via winHTTP which is now replaced by libcurl
- `list_cameras.c` — Camera enumeration using escapi
- `vfw_test.c` — Video for Windows Driver Detection and Format Query
- `capture_test.c` — The full pipeline before the platform-interface split

Kept for reference and not part of the build.
