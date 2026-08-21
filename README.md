# Vigil — Local AI Security Camera
A motion-triggered security camera written in **pure C**. It watches the webcam and detects motion via frame differencing. Only then it sends the captured frame to a local vision-language model for a description. No openCV, no Python runtime and no data leaves the system/machine.

--------------------
# Why?
Sending every frame to a vision model is wasteful. A 1080p stream each frame costs about 2500 tokens and several seconds of inference. Most frames show nothing is happening.
Vigil puts a cheap motion filter in front of the expensive model. Detection runs in about **3 ms**, inference takes about **3700 ms**. It is measured over a live session, motion gating with a cooldown reduced model invocations by roughly **93%** compared to per-frame processing.

That gap which is the three orders of magnitude between filtering and inference is the whole design rationale.

--------------------
## How it works:
webcam ──► MJPEG frame ──► decode to RGB ──► compare with previous frame
│
motion above threshold?
│
cooldown expired? ──► save JPEG
│
base64 ──► JSON ──► Ollama
│
description

- **Capture** — Video for Windows, MJPEG at 1920×1080
- **Decode** — `stb_image` (single-header) to RGB
- **Detection** — per-pixel brightness differencing, sampling every 4th pixel
- **Gating** — percentage threshold plus a 10-second cooldown
- **Transport** — hand-written base64 encoder, JSON builder and parser, libcurl POST
- **Model** — Qwen2.5-VL 3B running locally via Ollama

Everything except JPEG decoding and HTTP transport is written from scratch, including the base64 encoder (bit-level) and the JSON serialization and parsing.

--------------------
## Measured performance

| Stage | Time |
|---|---|
| Frame differencing (1080p, 1-in-4 sampling) | ~3 ms |
| Model inference (Qwen2.5-VL 3B, local) | ~3,700 ms |
| Model cold start (first call) | ~25,000 ms |

Reduction in model calls vs. per-frame processing: **~93%** over a live session with frequent movement. Idle scenes push this far higher.

Test hardware: Intel Core Ultra 9 275HX, RTX 5060 (8 GB), 32 GB RAM, Windows 11.

--------------------
## Architecture
- **src/**
- **vigil.c** — main loop, motion detection, Ollama client
- **camera.h** — platform-independent camera interface
- **camera_win32.c** — Video for Windows implementation\


Platform-specific capture sits behind a five-function interface:

```c
int   camera_init(void);
unsigned char *camera_grab(int *width, int *height);
void  camera_free_frame(unsigned char *pixels);
const unsigned char *camera_last_jpeg(long *len);
void  camera_close(void);
```

No Windows types appear outside `camera_win32.c`. Porting to Linux means implementing these five functions against V4L2 — nothing else changes.

---

## Building

**Requirements**
- A C compiler (developed with [w64devkit](https://github.com/skeeto/w64devkit))
- [libcurl](https://curl.se/windows/) — mingw build
- [Ollama](https://ollama.com) with a vision model: `ollama pull qwen2.5vl:3b`
- `stb_image.h` is included in `lib/`

**Setup**

Place libcurl's headers at `lib/curl/`, its import library at `lib/`, and `libcurl-x64.dll` next to the executable.

mkdir build build/detections
gcc -Wall -Ilib src/vigil.c src/camera_win32.c -o build/vigil.exe -lvfw32 -Llib -lcurl


**Running**

cd build
./vigil.exe


Detected events are written to `build/detections/` as timestamped JPEGs.

---

## Configuration

Tunable constants at the top of `src/vigil.c`:

| Constant | Default | Effect |
|---|---|---|
| `PIXEL_THRESHOLD` | 25 | Brightness change to count a pixel as changed |
| `MOTION_PERCENT` | 2.0 | Percentage of changed pixels to declare motion |
| `SAMPLE_STEP` | 4 | Check every Nth pixel |
| `COOLDOWN_MS` | 10000 | Minimum gap between model calls |
| `OLLAMA_MODEL` | qwen2.5vl:3b | Vision model |

---

## Limitations

- **Windows only.** Capture uses Video for Windows. The interface is isolated for portability, but no other backend is implemented.
- **MJPEG-dependent.** The camera used exposes only MJPEG through VfW, so every frame is JPEG-decoded before comparison. A camera offering raw formats would skip that step.
- **Frame-to-frame differencing** detects change, not objects. Lighting shifts register as motion.
- The cooldown can miss events that occur while it is active.

---

## Prototypes

`prototypes/` contains the standalone programs each pipeline stage was built and verified in before integration. See `prototypes/README.md`.

