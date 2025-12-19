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
| `hls_drift_startup_window` | "1" | **"30"** | 30 frames for baseline selection (or use "1.0s" for seconds) |
| `hls_drift_window` | "1" | **"1,30,300,900"** | Multiple averaging windows (frames or seconds, e.g. "1,30,10s,30s") |
| `hls_fps_window` | "1" | **"30"** | FPS calculation window (frames or seconds, e.g. "1.0s") |

### Default hls_flags Explained

- **append_list**: Append to existing playlist instead of overwriting
- **omit_endlist**: Don't add #EXT-X-ENDLIST (for live streams)
- **program_date_time**: Add #EXT-X-PROGRAM-DATE-TIME tags

### Network/I/O Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `timeout` | duration | -1 (disabled) | Socket I/O operations timeout |
| `ignore_io_errors` | bool | 0 | Ignore I/O errors for stable long-duration runs |
| `http_persistent` | bool | 0 | Use persistent HTTP connections |
| `headers` | string | NULL | Custom HTTP headers |

#### timeout

Sets timeout for socket I/O operations when writing segments/playlists to network destinations (HTTP, S3, etc.). Value is in microseconds.

- **-1** (default): No timeout, wait indefinitely
- **0**: Non-blocking mode
- **>0**: Timeout in microseconds (e.g., `10000000` = 10 seconds)

```bash
# Set 10 second timeout for HTTP uploads
ffmpeg ... -f hls -timeout 10000000 ...
```

#### ignore_io_errors

When enabled, I/O errors during segment/playlist upload don't stop encoding. Useful for unreliable network connections where temporary failures are acceptable.

- **0** (default): Stop on I/O errors
- **1**: Log warning and continue encoding

```bash
# Continue even if segment upload fails
ffmpeg ... -f hls -ignore_io_errors 1 ...
```

#### http_persistent

Reuses HTTP connections instead of opening new connection for each segment. Reduces latency and overhead for HTTP output.

- **0** (default): New connection per request
- **1**: Keep connection alive between requests

```bash
# Use persistent HTTP connections
ffmpeg ... -f hls -http_persistent 1 ...
```

#### headers

Sets custom HTTP headers for all HTTP requests (segment uploads, playlist uploads). Can override default headers.

```bash
# Set custom authorization header
ffmpeg ... -f hls -headers "Authorization: Bearer token123" ...
```

---

## 3. Frame Output Options

**Location:** `libavformat/hlsenc.c`

Real-time frame extraction during HLS encoding for external processing (e.g., AI analysis).

### Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `hls_frame_output` | string | NULL | Path to write current frame as BGR raw data |
| `hls_frame_interval` | string | "1" | Frame output interval: frames (e.g. "25") or seconds (e.g. "2.5s") |
| `hls_frame_meta_output` | string | NULL | Path to write frame metadata (append mode) |
| `hls_frame_meta_interval` | string | "1" | Meta output interval: frames (e.g. "1") or seconds (e.g. "0.5s") |
| `hls_frame_buffer_output` | string | NULL | Template for numbered frame files (e.g., `/path/frame_%09d.raw`) |
| `hls_frame_buffer_interval` | string | "1" | Buffer output interval: frames (e.g. "1") or seconds (e.g. "0.5s") |
| `hls_frame_buffer_size` | int | 0 | How many last frames to keep in buffer (0 = unlimited) |
| `hls_fps_window` | string | "30" | FPS calculation window: frames (e.g. "30") or seconds (e.g. "1.0s") |
| `hls_keyframes_only` | bool | 0 | **Super-economical mode:** decode only keyframes (I-frames) |

**Note:** All intervals with suffix `s` use server wall clock time, not media time.

### Keyframes-Only Mode (`hls_keyframes_only=1`)

Super-economical mode that decodes only keyframes (I-frames), completely skipping P and B frames.

**Benefits:**
- **Drastically reduced CPU usage** — decodes ~1 frame per GOP instead of all frames
- **Lower latency** — no need to wait for frame dependencies
- **Same FPS and GOP size in metadata** — calculated from packet data, not decoded frames

**Behavior:**
- All interval options (`hls_frame_interval`, `hls_frame_buffer_interval`, etc.) are **ignored**
- Every keyframe is automatically output
- FPS and GOP size are still calculated correctly from packet PTS
- Drift detection works (with lower sample rate)

**Use case:** When you only need periodic snapshots (e.g., thumbnail generation) and don't need every frame.

### Frame Output Format

Frames are written as raw BGR24 data:
- **Format:** BGR24 (3 bytes per pixel: Blue, Green, Red)
- **Size:** `width * height * 3` bytes
- **Header:** None (raw pixel data only)

### Frame Metadata Format

When `hls_frame_meta_output` is specified, metadata is appended as text lines with key=value format:

```
name=<segment>,offset=<time>,program_date_time=<iso>,now=<iso>,width=<w>,height=<h>,fps=<fps>,bitrate=<bps>,
pts=<pts>,is_keyframe=<0|1>,frame_type=<I|P|B>,codec=<src_codec>,encoding=<1|0>,gop_size=<n>,drift1=<d>,drift30=<d>,...
```

Fields:
- `fps` — calculated from actual frame PTS intervals (configurable window via `hls_fps_window`), not from stream metadata
- `codec` — исходный кодек потока (например hevc, h264), **не auto_h264**
- `encoding` — 1 если идёт перекодирование (source codec != output codec), 0 если stream copy
- `gop_size` — размер предыдущего завершённого GOP (количество кадров между ключевыми)

Example:
```
name=segment_00001.ts,offset=1.234567,program_date_time=2024-01-15T12:34:56.789Z,now=2024-01-15T12:34:56.791Z,
width=1920,height=1080,fps=25.000,bitrate=4000000,pts=123456,is_keyframe=1,frame_type=I,
codec=hevc,encoding=1,gop_size=24,drift1=0.001234,drift30=0.002345,drift300=0.003456,drift900=0.004567
```

### Usage Example

**Frame-based interval (every Nth frame):**
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

**Time-based interval (every N seconds, server wall clock time):**
```bash
ffmpeg -rtsp_transport tcp -i rtsp://camera/stream \
    -c:v auto_h264 \
    -f hls \
    -hls_frame_output /tmp/current_frame.raw \
    -hls_frame_interval 2.5s \
    -hls_frame_meta_output /tmp/frames.log \
    playlist.m3u8
```

**Keyframes-only mode (super-economical, ~1 frame per GOP):**
```bash
ffmpeg -rtsp_transport tcp -i rtsp://camera/stream \
    -c:v auto_h264 \
    -f hls \
    -hls_keyframes_only 1 \
    -hls_frame_output /tmp/current_frame.raw \
    -hls_frame_meta_output /tmp/frames.log \
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
- **Graceful exit:** Current segment is properly finished and written to playlist
- FFmpeg exits with error code after segment finalization
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
| `hls_drift_startup_window` | string | **"30"** | Startup window: frames (e.g. "30") or seconds (e.g. "1.0s") |
| `hls_drift_window` | string | **"1,30,300,900"** | Comma-separated list of window sizes: frames or seconds (e.g. "1,30,10s,30s") |
| `hls_drift_min_threshold` | string | NULL | Min drift thresholds per window (e.g. `"300:-2"` or `"10s:-2"`) |
| `hls_drift_max_threshold` | string | NULL | Max drift thresholds per window (e.g. `"30:2"` or `"10s:2"`) |

**Note:** All intervals with suffix `s` use server wall clock time, not media time.

### How Drift Works

1. **Startup phase:** First N frames (or N seconds) are analyzed to find the best baseline:
   - First frame is used as temporary baseline (drift = 0)
   - For each subsequent frame, drift relative to first frame is calculated
   - The frame with **minimum drift** (closest to real-time, even if negative) becomes the final baseline
   - This selects the frame that arrived with least network/processing lag
2. **Running phase:** Drift is calculated as difference between expected and actual timestamps
3. **Multiple windows:** Drift is calculated with sliding sum for each window size (O(1) per frame for frame-based windows)

### Multiple Drift Windows

The `hls_drift_window` option accepts a comma-separated list of window sizes:
- **Frame-based (e.g. "30"):** Short windows (1-30 frames) detect sudden changes quickly
- **Time-based (e.g. "10s"):** Better for varying FPS streams, uses server wall clock time

Examples:
- `"1,30,300,900"` — 1, 30, 300, 900 frames
- `"0.04s,1s,10s,30s"` — approximately same as above at 25fps, but independent of actual FPS

Each window uses efficient O(1) calculation per frame:
- **Frame-based:** Sliding sum with circular buffer
- **Time-based:** Binned sliding sum (100 bins per window, no memory limit, no rounding error)

### Drift Value in Metadata

When using `hls_frame_meta_output`, drift values for all windows are included:

```
...,drift1=0.001234,drift30=0.002345,drift300=0.003456,drift900=0.004567
```

- **Positive drift:** Stream is behind real-time (frames arriving late)
- **Negative drift:** Stream is ahead of real-time (frames arriving early)
- **Near zero:** Stream is synchronized

### Drift Threshold Exit

The `hls_drift_min_threshold` and `hls_drift_max_threshold` options allow setting exit thresholds per drift window. When a threshold is exceeded:
- **Graceful exit:** Current segment is properly finished and written to playlist
- FFmpeg exits with error code after segment finalization

**Format:** `"window:threshold,window:threshold,..."`

Supports both frame-based and time-based windows:
- Frame-based: `"300:-2"` (window of 300 frames, threshold -2 seconds)
- Time-based: `"10s:-2"` (window of 10 seconds, threshold -2 seconds)

- **Min threshold:** Exit if drift < threshold (stream running too fast)
- **Max threshold:** Exit if drift > threshold (stream running too slow)

```bash
# Frame-based thresholds
ffmpeg ... -hls_drift_min_threshold "300:-2" ...      # Exit if drift300 < -2 sec
ffmpeg ... -hls_drift_max_threshold "30:2" ...        # Exit if drift30 > 2 sec

# Time-based thresholds
ffmpeg ... -hls_drift_min_threshold "10s:-2" ...      # Exit if drift10s < -2 sec
ffmpeg ... -hls_drift_max_threshold "30s:5" ...       # Exit if drift30s > 5 sec

# Multiple thresholds (can mix frame-based and time-based)
ffmpeg ... \
    -hls_drift_min_threshold "300:-2,10s:-3" \
    -hls_drift_max_threshold "30:2,30s:5" ...
```

### Custom Windows Example

```bash
# Use only short-term drift detection (frame-based)
ffmpeg ... -hls_drift_window "1,10,30" ...

# Use longer windows for stable connections (frame-based)
ffmpeg ... -hls_drift_window "30,60,300,900,1800" ...

# Use time-based windows (FPS-independent)
ffmpeg ... -hls_drift_window "0.1s,1s,10s,30s" ...

# Mix frame-based and time-based
ffmpeg ... -hls_drift_window "1,30,10s,30s" ...
```

---

## 6. Events Log

**Location:** `fftools/ffmpeg.c`, `libavformat/rtsp.c`, `libavformat/hlsenc.c`

Append-only log file for tracking key control events during FFmpeg execution. Enabled via environment variable.

### Environment Variable

| Variable | Description |
|----------|-------------|
| `FFMPEG_EVENTS_LOG` | Path to events log file (append mode, JSON lines format) |

### Events

#### Process-level events (ffmpeg.c)

| Event | Description | Fields |
|-------|-------------|--------|
| `PROCESS_START` | FFmpeg process started | pid |
| `PARSE_OPTIONS_START` | Options parsing begins | - |
| `PARSE_OPTIONS_COMPLETE` | Options parsed, inputs/outputs opened | ret, nb_inputs, nb_outputs |
| `TRANSCODE_START` | Transcoding begins | - |
| `PROCESS_END` | FFmpeg process ending | ret |

#### Input events (ffmpeg_demux.c)

| Event | Description | Fields |
|-------|-------------|--------|
| `INPUT_OPEN_START` | Starting to open input | file |
| `INPUT_OPEN_SUCCESS` | Input opened successfully | file, format, nb_streams |
| `INPUT_OPEN_FAILED` | Input open failed | file, error |
| `PROBE_START` | Stream probing begins | nb_streams |
| `PROBE_LOOP_START` | Probe main loop starts | nb_streams, probesize, analyze_duration |
| `PROBE_PROGRESS` | Probe progress (every 50 packets) | packets, bytes |
| `PROBE_ALL_FOUND` | All stream params found | packets, bytes |
| `PROBE_SIZE_LIMIT` | Probesize limit reached | packets, bytes, limit |
| `PROBE_EOF` | EOF/error during probing | packets, bytes, ret |
| `PROBE_COMPLETE` | Stream probing finished | ret, nb_streams |

#### RTSP events (rtsp.c)

| Event | Description | Fields |
|-------|-------------|--------|
| `RTSP_CONNECT_START` | Starting RTSP connection | host, port |
| `RTSP_CONNECT_SUCCESS` | RTSP connection established | host, port |
| `RTSP_CONNECT_FAILED` | RTSP connection failed | host, port, error |
| `RTSP_OPTIONS_START` | Sending OPTIONS command | - |
| `RTSP_OPTIONS_DONE` | OPTIONS response received | status |
| `RTSP_DESCRIBE_START` | Sending DESCRIBE command | - |
| `RTSP_DESCRIBE_DONE` | DESCRIBE/SDP parsed | ret |
| `RTSP_SETUP_START` | Starting SETUP for streams | - |
| `RTSP_SETUP_DONE` | All streams setup | nb_streams |
| `RTSP_TIMEOUT` | Socket I/O timeout (low level) | timeout_us |
| `RTSP_READ_TIMEOUT` | Read packet timeout | timeout_us, transport |
| `RTSP_RETRY_TCP` | Retrying with TCP after UDP timeout | - |

#### Network events (network.c)

| Event | Description | Fields |
|-------|-------------|--------|
| `TCP_CONNECT_TIMEOUT` | TCP connection timed out | timeout_ms_per_address |
| `TCP_CONNECT_FAILED` | TCP connection failed | error |

#### Scheduler events (ffmpeg_sched.c)

| Event | Description | Fields |
|-------|-------------|--------|
| `SCH_PREPARE_START` | Scheduler preparation | - |
| `SCH_PREPARE_DONE` | Preparation complete | - |
| `SCH_MUX_INIT_START` | Muxer initialization | nb_mux |
| `SCH_MUX_INIT_DONE` | Muxers initialized | - |
| `SCH_ENC_START` | Encoder threads starting | nb_enc |
| `SCH_ENC_DONE` | Encoders started | - |
| `SCH_FILTER_START` | Filter graphs starting | nb_filters |
| `SCH_FILTER_DONE` | Filters started | - |
| `SCH_DEC_START` | Decoder threads starting | nb_dec |
| `SCH_DEC_DONE` | Decoders started | - |
| `SCH_DEMUX_START` | Demuxer threads starting | nb_demux |
| `SCH_DEMUX_DONE` | Demuxers started | - |
| `SCH_START_COMPLETE` | Scheduler fully started | - |

#### Decoder events (ffmpeg_dec.c)

| Event | Description | Fields |
|-------|-------------|--------|
| `DEC_THREAD_START` | Decoder thread started | codec |
| `DEC_FIRST_PACKET` | First packet received by decoder | size |

#### Encoder events (ffmpeg_enc.c)

| Event | Description | Fields |
|-------|-------------|--------|
| `ENC_THREAD_START` | Encoder thread started | type |
| `ENC_FIRST_FRAME` | First frame received by encoder | type, pts |

#### Filter events (ffmpeg_filter.c)

| Event | Description | Fields |
|-------|-------------|--------|
| `FILTER_THREAD_START` | Filter thread started | nb_inputs, nb_outputs |
| `FILTER_CONFIGURE_START` | Filter graph configuration starting | - |
| `FILTER_CONFIGURE_DONE` | Filter graph configured | ret |
| `FILTER_FIRST_FRAME` | First frame through filter | pts |

#### HLS muxer events (hlsenc.c)

| Event | Description | Fields |
|-------|-------------|--------|
| `INIT_START` | HLS muxer initialization starts | output, nb_streams |
| `INIT_COMPLETE` | Initialization completed | nb_varstreams |
| `FIRST_PACKET` | First packet received | pts, type |
| `SEGMENT_START` | New segment opened | seq, file |
| `SEGMENT_COMPLETE` | Segment finalized | seq, file, duration, size |
| `SEGMENT_DELETE` | Old segment removed | file |
| `PLAYLIST_UPDATE` | Playlist written | file, segments, last |
| `PTS_DISCONTINUITY` | PTS jump detected | prev_pts, curr_pts, diff_sec |
| `DRIFT_THRESHOLD` | Drift threshold exceeded | window, drift, threshold, type |
| `GRACEFUL_EXIT_REQUESTED` | Exit requested | reason |
| `SEGMENT_RETRY` | Segment upload retry | file, error |
| `IO_ERROR` | I/O error occurred | op, file, error |
| `SHUTDOWN_START` | Shutdown begins | graceful_exit |
| `SHUTDOWN_COMPLETE` | Shutdown finished | - |
| `DEINIT_COMPLETE` | HLS muxer cleanup finished | - |

### Log Format (JSON Lines)

Each line is a valid JSON object with ISO8601 timestamp:

```json
{"ts":"2025-12-13T12:34:56.100","event":"PROCESS_START","pid":12345}
{"ts":"2025-12-13T12:34:56.150","event":"PARSE_OPTIONS_START"}
{"ts":"2025-12-13T12:34:56.200","event":"RTSP_CONNECT_START","host":"192.168.1.100","port":554}
{"ts":"2025-12-13T12:34:56.500","event":"RTSP_CONNECT_SUCCESS","host":"192.168.1.100","port":554}
{"ts":"2025-12-13T12:34:57.000","event":"PARSE_OPTIONS_COMPLETE","ret":0,"nb_inputs":1,"nb_outputs":1}
{"ts":"2025-12-13T12:34:57.050","event":"TRANSCODE_START"}
{"ts":"2025-12-13T12:34:57.100","event":"INIT_START","output":"output.m3u8","nb_streams":1}
{"ts":"2025-12-13T12:34:57.150","event":"SEGMENT_START","seq":0,"file":"segment0.ts"}
{"ts":"2025-12-13T12:34:57.200","event":"INIT_COMPLETE","nb_varstreams":1}
{"ts":"2025-12-13T12:34:57.250","event":"FIRST_PACKET","pts":0,"type":"video"}
{"ts":"2025-12-13T12:35:03.200","event":"SEGMENT_COMPLETE","seq":0,"file":"segment0.ts","duration":6.006,"size":1234567}
{"ts":"2025-12-13T12:35:03.201","event":"PLAYLIST_UPDATE","file":"output.m3u8","segments":1,"last":0}
```

### Usage Example

```bash
# Set environment variable and run ffmpeg
export FFMPEG_EVENTS_LOG=/var/log/ffmpeg/events.log

ffmpeg -rtsp_transport tcp -i rtsp://camera/stream \
    -c:v auto_h264 \
    -f hls \
    playlist.m3u8

# Or inline
FFMPEG_EVENTS_LOG=/tmp/events.log ffmpeg -i input.mp4 -f hls output.m3u8
```

### Timing Analysis

The events log helps identify where time is spent:

| Gap | What's happening |
|-----|-----------------|
| `PROCESS_START` → `PARSE_OPTIONS_START` | FFmpeg library initialization |
| `PARSE_OPTIONS_START` → `INPUT_OPEN_START` | Command line parsing |
| `INPUT_OPEN_START` → `RTSP_CONNECT_START` | Protocol handler setup |
| `RTSP_CONNECT_START` → `RTSP_CONNECT_SUCCESS` | TCP connection to camera |
| `RTSP_CONNECT_SUCCESS` → `INPUT_OPEN_SUCCESS` | RTSP handshake (DESCRIBE, SETUP, PLAY) |
| `INPUT_OPEN_SUCCESS` → `PROBE_START` | Codec selection setup |
| `PROBE_START` → `PROBE_LOOP_START` | Decoder initialization |
| `PROBE_LOOP_START` → `PROBE_ALL_FOUND` | **Reading packets to detect stream params** |
| `PROBE_ALL_FOUND` → `PROBE_COMPLETE` | Post-processing, flush codecs |
| `PROBE_COMPLETE` → `PARSE_OPTIONS_COMPLETE` | Output file setup |
| `PARSE_OPTIONS_COMPLETE` → `TRANSCODE_START` | Encoder/muxer initialization |
| `TRANSCODE_START` → `SCH_START_COMPLETE` | Scheduler startup |
| `SCH_START_COMPLETE` → `DEC_FIRST_PACKET` | **Waiting for first packet from demuxer** |
| `DEC_FIRST_PACKET` → `FILTER_FIRST_FRAME` | Decoding first frame |
| `FILTER_FIRST_FRAME` → `ENC_FIRST_FRAME` | Filter processing |
| `ENC_FIRST_FRAME` → `INIT_START` | **Encoding first frame** |
| `INIT_START` → `FIRST_PACKET` | HLS muxer initialization |

### Log Size Estimation

- ~200 bytes per event
- At 6-second segments: ~600 events/hour
- **~120 KB/hour** or **~3 MB/day**

---

## 7. Graceful Exit Behavior

**Location:** `libavformat/hlsenc.c`

When exit is triggered by PTS discontinuity or drift threshold:

1. **Bad packet is NOT written** — the packet that triggered the exit is discarded
2. Current segment is properly finalized with only good data
3. Segment is added to the playlist
4. `hls_write_trailer` is called for proper cleanup
5. FFmpeg exits with error code

This ensures segment integrity — the last segment contains only valid data without discontinuities.

---

## 8. Low-Latency Input Options

**Location:** `libavformat/options_table.h`, `libavcodec/options_table.h`

Default values optimized for minimum startup latency with RTSP streams.

### Changed Defaults

| Option | Original Default | New Default | Description |
|--------|-----------------|-------------|-------------|
| `probesize` | 5000000 (5MB) | **32** | Minimal probe buffer for fast start |
| `analyzeduration` | auto | **0** | Don't wait to analyze stream duration |
| `fflags` | (auto_bsf) | **nobuffer+auto_bsf** | Don't buffer input packets |
| `flags` (codec) | (none) | **low_delay** | Enable low-delay mode for codec |

### Override to Original Values

```bash
# If you need more reliable stream detection:
ffmpeg -probesize 5000000 -analyzeduration 5000000 -i rtsp://camera/stream ...

# If you need buffering:
ffmpeg -fflags -nobuffer -i rtsp://camera/stream ...
```

### Frame Output with Low Probesize

When using `-probesize 32`, video dimensions may be unknown at startup (0x0). The HLS frame output system handles this automatically:

1. **At startup:** Decoder is initialized, but `SwsContext` creation is deferred if dimensions are 0x0
2. **After first keyframe:** When the first frame is decoded and dimensions become known, `SwsContext` is created lazily
3. **In logs:** You'll see:
   ```
   [hls] Video dimensions unknown at header time (0x0), SwsContext will be created lazily after first frame decode
   [hls] Creating SwsContext lazily for 1920x1080 format=0
   ```

This allows frame output to work correctly even with minimal probesize.

---

## 9. Minimal Example

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
- PTS discontinuity detection with graceful exit
- Drift monitoring
- Graceful exit on errors (segment properly finalized)

---

## 10. Full Example with All Options

Complete example with all custom options explicitly specified (note: low-latency options are now defaults):

```bash
# Enable events logging via environment variable
export FFMPEG_EVENTS_LOG=/var/log/ffmpeg/events.log

# Low-latency options (-probesize 32, -analyzeduration 0, -fflags nobuffer, -flags low_delay)
# are now defaults and don't need to be specified

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
    -hls_drift_startup_window 30 \
    -hls_drift_window "1,30,300,900" \
    -hls_drift_min_threshold "300:-2,900:-5" \
    -hls_drift_max_threshold "30:2,300:5" \
    -timeout 10000000 \
    -ignore_io_errors 0 \
    playlist.m3u8
```

---

## Summary Table

| Category | Option | Default | Purpose |
|----------|--------|---------|---------|
| **Input** | `rtsp_transport` | **TCP** | RTSP protocol transport |
| **Low-Latency** | `probesize` | **32** | Minimal probe buffer for fast start |
| **Low-Latency** | `analyzeduration` | **0** | No analyze wait |
| **Low-Latency** | `fflags` | **nobuffer** | Disable input buffering |
| **Low-Latency** | `flags` (codec) | **low_delay** | Low-delay codec mode |
| **Codec** | `-c:v auto_h264` | - | Auto passthrough/encode for HLS |
| **Segments** | `hls_time` | **1 sec** | Segment duration |
| **Segments** | `hls_list_size` | **0** | Playlist size (0=infinite) |
| **Flags** | `hls_flags` | **append+omit+pdt** | Live streaming flags |
| **Frame Output** | `hls_frame_output` | NULL | Current frame extraction |
| **Frame Output** | `hls_frame_interval` | "1" | Frame output interval (frames or seconds) |
| **Frame Output** | `hls_frame_meta_interval` | "1" | Meta output interval (frames or seconds) |
| **Frame Output** | `hls_frame_buffer_output` | NULL | Circular frame buffer |
| **Frame Output** | `hls_frame_buffer_interval` | "1" | Buffer output interval (frames or seconds) |
| **Frame Output** | `hls_frame_meta_output` | NULL | Frame metadata logging |
| **Frame Output** | `hls_fps_window` | "30" | FPS calculation window (frames or seconds) |
| **Frame Output** | `hls_keyframes_only` | 0 | Super-economical mode (keyframes only) |
| **Monitoring** | `hls_pts_discontinuity_exit` | **1** | Exit on stream issues |
| **Monitoring** | `hls_pts_discontinuity_threshold_neg` | **0.1** | Backward jump threshold |
| **Monitoring** | `hls_pts_discontinuity_threshold_pos` | **1.0** | Forward jump threshold |
| **Monitoring** | `hls_drift_startup_window` | "30" | Startup window (frames or seconds) |
| **Monitoring** | `hls_drift_window` | "1,30,300,900" | Averaging windows (frames or seconds) |
| **Monitoring** | `hls_drift_min_threshold` | NULL | Min drift thresholds per window |
| **Monitoring** | `hls_drift_max_threshold` | NULL | Max drift thresholds per window |
| **Logging** | `FFMPEG_EVENTS_LOG` (env) | - | Events log file path |
| **Network** | `timeout` | -1 | Socket I/O timeout (microseconds) |
| **Network** | `ignore_io_errors` | 0 | Ignore I/O errors |
| **Network** | `http_persistent` | 0 | Persistent HTTP connections |
