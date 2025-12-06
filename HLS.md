# Custom FFmpeg HLS Extensions

This document describes custom options added to FFmpeg for enhanced HLS streaming support.

---

## 0. RTSP Transport Default

**Location:** `libavformat/rtsp.c`

RTSP transport protocol default changed from UDP to TCP for more reliable streaming.

| Setting | Original Default | New Default |
|---------|-----------------|-------------|
| `rtsp_transport` | (auto/UDP) | **TCP** |

### Override to UDP if needed

```bash
ffmpeg -rtsp_transport udp -i rtsp://camera/stream ...
```

---

## 1. Auto H.264 Codec (`-c:v auto_h264`)

**Location:** `fftools/ffmpeg_mux_init.c`, `fftools/ffmpeg_mux.h`

Automatic codec selection for HLS streaming that detects input codec and decides whether to passthrough or re-encode.

### Behavior

| Input Codec | Action | Description |
|-------------|--------|-------------|
| H.264 | Stream copy | No re-encoding, preserves original quality |
| Other (HEVC, etc.) | Encode with libx264 | Re-encodes to H.264 for HLS compatibility |

### Default Encoding Options (when re-encoding)

When input is not H.264, the following defaults are applied automatically:

| Option | Value | Description |
|--------|-------|-------------|
| `preset` | `veryfast` | Fast encoding preset |
| `tune` | `zerolatency` | Optimized for low-latency streaming |
| `x264-params` | `scenecut=0` | Disable scene cut detection |
| `pix_fmt` | `yuv420p` | Standard pixel format for compatibility |
| `flags` | `+global_header` | Required for HLS |
| `fps_mode` | `passthrough` | Preserve original frame timing |
| `force_key_frames` | `expr:if(isnan(prev_forced_t),1,gte(t,prev_forced_t+hls_time))` | Auto-aligned to HLS segment boundaries |

### Usage Example

```bash
ffmpeg -rtsp_transport tcp -i rtsp://camera/stream \
    -map 0:v:0 -an \
    -c:v auto_h264 \
    -f hls \
    playlist.m3u8
```

### Override Defaults

All auto_h264 defaults can be overridden via command line:

```bash
# Override preset
ffmpeg -i input -c:v auto_h264 -preset medium -f hls output.m3u8

# Override fps_mode
ffmpeg -i input -c:v auto_h264 -fps_mode cfr -f hls output.m3u8
```

---

## 2. HLS Muxer Default Values

**Location:** `libavformat/hlsenc.c`

Optimized defaults for RTSP-to-HLS live streaming.

### Changed Defaults

| Option | Original Default | New Default | Description |
|--------|-----------------|-------------|-------------|
| `hls_time` | 2 sec | **1 sec** | Shorter segments for lower latency |
| `hls_list_size` | 5 | **0** | Infinite playlist (keep all segments) |
| `hls_flags` | (none) | **append_list+omit_endlist+program_date_time** | Live streaming flags |
| `hls_pts_discontinuity_exit` | 0 | **1** | Exit on stream issues |
| `hls_pts_discontinuity_threshold_neg` | 0 | **0.1** | 100ms backward jump threshold |
| `hls_pts_discontinuity_threshold_pos` | 1 | **1.0** | 1 sec forward jump threshold |
| `hls_drift_startup_window` | 1 | **10** | 10 frames for sync |
| `hls_drift_window` | 1 | **30** | 30 frames averaging |

### Default hls_flags Explained

- **append_list**: Append to existing playlist instead of overwriting
- **omit_endlist**: Don't add #EXT-X-ENDLIST (for live streams)
- **program_date_time**: Add #EXT-X-PROGRAM-DATE-TIME tags

---

## 3. Frame Output Options

**Location:** `libavformat/hlsenc.c`

Real-time frame extraction during HLS encoding for external processing (e.g., AI analysis).

### Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `hls_frame_output` | string | NULL | Path to write current frame as BGR raw data |
| `hls_frame_interval` | int | 1 | Write every Nth frame |
| `hls_frame_meta_output` | string | NULL | Path to write frame metadata (append mode) |
| `hls_frame_meta_interval` | int | 1 | Write metadata every Nth frame |
| `hls_frame_buffer_output` | string | NULL | Template for numbered frame files (e.g., `/path/frame_%09d.raw`) |
| `hls_frame_buffer_interval` | int | 1 | Write every Nth frame into frame buffer |
| `hls_frame_buffer_size` | int | 0 | How many last frames to keep in buffer (0 = unlimited) |

### Frame Output Format

Frames are written as raw BGR24 data:
- **Format:** BGR24 (3 bytes per pixel: Blue, Green, Red)
- **Size:** `width * height * 3` bytes
- **Header:** None (raw pixel data only)

### Frame Metadata Format

When `hls_frame_meta_output` is specified, metadata is appended as text lines:

```
<segment_name> <time_offset_in_segment> <width> <height> <drift>
```

Example:
```
segment_00001.ts 1.234567 1920 1080 0.003
segment_00001.ts 1.267890 1920 1080 0.002
```

### Usage Example

```bash
ffmpeg -rtsp_transport tcp -i rtsp://camera/stream \
    -c:v auto_h264 \
    -f hls \
    -hls_frame_output /tmp/current_frame.raw \
    -hls_frame_interval 10 \
    -hls_frame_meta_output /tmp/frames.log \
    -hls_frame_buffer_output /tmp/frames/frame_%09d.raw \
    -hls_frame_buffer_size 100 \
    playlist.m3u8
```

---

## 4. PTS Discontinuity Detection

**Location:** `libavformat/hlsenc.c`

Detects PTS (Presentation Timestamp) discontinuities in the input stream, useful for monitoring RTSP stream stability.

### Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `hls_pts_discontinuity_exit` | bool | **1** | Exit process on PTS discontinuity |
| `hls_pts_discontinuity_threshold_neg` | double | **0.1** | Threshold for backward PTS jump (seconds) |
| `hls_pts_discontinuity_threshold_pos` | double | **1.0** | Threshold for forward PTS jump (seconds) |

### Behavior

- **Negative jump:** PTS goes backward (e.g., stream restart)
- **Positive jump:** PTS jumps forward significantly (e.g., packet loss)

When `hls_pts_discontinuity_exit=1` and a discontinuity is detected:
- FFmpeg exits with error code
- Allows external process manager to restart the stream

### Disable Discontinuity Exit

```bash
ffmpeg -rtsp_transport tcp -i rtsp://camera/stream \
    -c:v auto_h264 \
    -f hls \
    -hls_pts_discontinuity_exit 0 \
    playlist.m3u8
```

---

## 5. Drift Detection and Monitoring

**Location:** `libavformat/hlsenc.c`

Calculates drift between PTS timestamps and wall-clock time for stream health monitoring.

### Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `hls_drift_startup_window` | int | **10** | Number of frames for initial PTS-wallclock sync |
| `hls_drift_window` | int | **30** | Number of frames for drift averaging |

### How Drift Works

1. **Startup phase:** First N frames establish baseline PTS-to-wallclock mapping
2. **Running phase:** Drift is calculated as difference between expected and actual timestamps
3. **Averaging:** Moving average over `drift_window` frames smooths out jitter

### Drift Value in Metadata

When using `hls_frame_meta_output`, drift is included in each line:
- **Positive drift:** Stream is behind real-time
- **Negative drift:** Stream is ahead of real-time
- **Near zero:** Stream is synchronized

---

## 6. Minimal Example

With the new defaults, a minimal RTSP-to-HLS command is simply:

```bash
ffmpeg -rtsp_transport tcp -i rtsp://192.168.1.100/stream \
    -map 0:v:0 -an \
    -c:v auto_h264 \
    -f hls \
    playlist.m3u8
```

This automatically applies:
- Auto codec detection (copy H.264, encode others)
- 1 second segments
- Infinite playlist
- Live streaming flags (append_list, omit_endlist, program_date_time)
- PTS discontinuity detection with exit
- Drift monitoring

---

## 7. Full Example with All Options

Complete example with all custom options explicitly specified:

```bash
ffmpeg \
    -hide_banner -loglevel info \
    -rtsp_transport tcp \
    -i rtsp://192.168.1.100/stream \
    -map 0:v:0 -an \
    -c:v auto_h264 \
    -f hls \
    -hls_time 1 \
    -hls_list_size 0 \
    -hls_flags append_list+omit_endlist+program_date_time \
    -hls_segment_filename segments/segment_%05d.ts \
    -hls_base_url segments/ \
    -hls_frame_output /tmp/last_frame.raw \
    -hls_frame_meta_output /tmp/frames.log \
    -hls_pts_discontinuity_exit 1 \
    -hls_pts_discontinuity_threshold_neg 0.1 \
    -hls_pts_discontinuity_threshold_pos 1.0 \
    -hls_drift_startup_window 10 \
    -hls_drift_window 30 \
    playlist.m3u8
```

---

## Summary Table

| Category | Option | Default | Purpose |
|----------|--------|---------|---------|
| **Input** | `rtsp_transport` | **TCP** | RTSP protocol transport |
| **Codec** | `-c:v auto_h264` | - | Auto passthrough/encode for HLS |
| **Segments** | `hls_time` | **1 sec** | Segment duration |
| **Segments** | `hls_list_size` | **0** | Playlist size (0=infinite) |
| **Flags** | `hls_flags` | **append+omit+pdt** | Live streaming flags |
| **Frame Output** | `hls_frame_output` | NULL | Current frame extraction |
| **Frame Output** | `hls_frame_buffer_output` | NULL | Circular frame buffer |
| **Frame Output** | `hls_frame_meta_output` | NULL | Frame metadata logging |
| **Monitoring** | `hls_pts_discontinuity_exit` | **1** | Exit on stream issues |
| **Monitoring** | `hls_pts_discontinuity_threshold_neg` | **0.1** | Backward jump threshold |
| **Monitoring** | `hls_pts_discontinuity_threshold_pos` | **1.0** | Forward jump threshold |
| **Monitoring** | `hls_drift_startup_window` | **10** | Initial sync frames |
| **Monitoring** | `hls_drift_window` | **30** | Averaging frames |
