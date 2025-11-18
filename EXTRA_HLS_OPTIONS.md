# HLS Frame Output and PDT Drift Correction

This document describes the new HLS muxer options for frame output and program date time (PDT) drift correction.

## Frame Output Options

### `-hls_frame_output <path>`

Specifies the path where the current video frame will be written as a BGR raw data file (`frame.raw`).

**Format:**
- **Line 1:** Metadata in `key=value` format: `name=segment_xxx.ts,offset=0.123,width=1920,height=1080,fps=30.000,bitrate=5000000,pts=123456,is_keyframe=1,program_date_time=2024-01-01T12:00:00.000+0000`
- **Subsequent lines:** Binary BGR24 data (width × height × 3 bytes), directly mappable to a Python `cv2` `ndarray`

**Metadata fields:**
- `name` - Name of the HLS segment this frame belongs to
- `offset` - Time offset in seconds from the beginning of the segment
- `width` - Frame width in pixels
- `height` - Frame height in pixels
- `fps` - Frame rate (frames per second)
- `bitrate` - Estimated bitrate in bits per second
- `pts` - Presentation timestamp
- `is_keyframe` - 1 if frame is a keyframe, 0 otherwise
- `program_date_time` - Program date time in ISO 8601 format (only if `-hls_flags program_date_time` is enabled)

**Example:**
```bash
ffmpeg -i input.mp4 -f hls -hls_frame_output /path/to/frame.raw playlist.m3u8
```

### `-hls_frame_interval <N>`

Specifies that every Nth frame should be written to `frame.raw`. Default is 1 (write every frame).

**Example:**
```bash
ffmpeg -i input.mp4 -f hls -hls_frame_output /path/to/frame.raw -hls_frame_interval 30 playlist.m3u8
```

## PDT Drift Correction Options

### `-hls_pdt_drift_threshold <seconds>`

Maximum allowed program date time (PDT) drift in seconds before automatic correction is applied. Default is 1.0 seconds. Set to 0 to disable drift correction (original FFmpeg behavior).

**How it works:**
- PDT is calculated as `initial_prog_date_time + total_duration` of all completed segments
- Due to rounding errors in segment durations (calculated from PTS), PDT can drift from real time over long streams
- When drift exceeds the threshold, PDT is automatically synchronized with real time
- This prevents long-term error accumulation while maintaining short-term accuracy

**Range:** 0.0 to 3600.0 seconds

**Examples:**
```bash
# Default: correct drift when it exceeds 1.0 second
ffmpeg -i input.mp4 -f hls -hls_flags program_date_time playlist.m3u8

# Correct drift when it exceeds 0.5 seconds
ffmpeg -i input.mp4 -f hls -hls_flags program_date_time -hls_pdt_drift_threshold 0.5 playlist.m3u8

# Disable drift correction (original behavior)
ffmpeg -i input.mp4 -f hls -hls_flags program_date_time -hls_pdt_drift_threshold 0 playlist.m3u8
```

### `-hls_pdt_use_realtime <0|1>`

Use real time for each segment's program date time instead of accumulated durations. Default is 0 (disabled).

**When enabled:**
- Each segment's PDT is set to the current real time when the segment starts
- This completely eliminates drift accumulation
- Useful for long-running streams where accurate PDT is critical
- Note: This makes PDT independent for each segment, which may not match the original HLS specification behavior

**When disabled (default):**
- PDT is calculated as `initial_prog_date_time + total_duration`
- Matches original FFmpeg behavior
- Drift correction (if enabled via `-hls_pdt_drift_threshold`) will still apply

**Example:**
```bash
# Use real time for each segment (independent PDT)
ffmpeg -i input.mp4 -f hls -hls_flags program_date_time -hls_pdt_use_realtime 1 playlist.m3u8
```

## When to Use Each Option

### Use `-hls_pdt_use_realtime 1` (Real Time) When:

✅ **Long-running streams (hours/days)** - Prevents drift accumulation over time  
✅ **CV analytics requiring accurate timestamps** - Each frame's PDT matches real time  
✅ **Camera surveillance systems** - Need precise time correlation between frames and events  
✅ **Compliance with real-world time** - When PDT must match wall-clock time exactly  
✅ **Frame-by-frame analysis** - When analyzing frames with timestamps, real time is more reliable  

**Example use case:** 24-hour camera recording where you need to correlate CV detections with exact timestamps:
```bash
ffmpeg -i rtsp://camera/stream \
    -c:v libx264 -preset ultrafast -c:a aac \
    -f hls -hls_time 2 -hls_list_size 0 \
    -hls_flags program_date_time+append_list \
    -hls_frame_output /path/to/frame.raw \
    -hls_frame_interval 30 \
    -hls_pdt_use_realtime 1 \
    playlist.m3u8
```

### Use `-hls_pdt_drift_threshold <seconds>` (Drift Correction) When:

✅ **Medium-length streams (minutes to hours)** - Need balance between accuracy and HLS spec compliance  
✅ **HLS specification compliance** - Want to maintain continuous PDT progression as per spec  
✅ **Gradual correction preferred** - Want to correct drift gradually rather than resetting each segment  
✅ **Short-term accuracy important** - Need precise relative timing between segments  

**Recommended thresholds:**
- **0.5 seconds** - For streams up to 1-2 hours (aggressive correction)
- **1.0 seconds** - Default, good for streams up to 4-6 hours
- **2.0-5.0 seconds** - For very long streams where occasional correction is acceptable

**Example use case:** 6-hour recording where you want HLS-compliant PDT with automatic correction:
```bash
ffmpeg -i rtsp://camera/stream \
    -c:v libx264 -preset ultrafast -c:a aac \
    -f hls -hls_time 2 -hls_list_size 0 \
    -hls_flags program_date_time+append_list \
    -hls_frame_output /path/to/frame.raw \
    -hls_frame_interval 30 \
    -hls_pdt_drift_threshold 1.0 \
    playlist.m3u8
```

### Use Neither (Original Behavior) When:

✅ **Short streams (< 1 hour)** - Drift is negligible  
✅ **HLS specification strict compliance** - Need exact original FFmpeg behavior  
✅ **Testing/debugging** - Want to see original behavior without modifications  

**Example:**
```bash
ffmpeg -i input.mp4 -f hls -hls_flags program_date_time -hls_pdt_drift_threshold 0 playlist.m3u8
```

## Recommendation for 24-Hour Camera Playlists with CV Analytics

For your use case (24-hour playlists, CV analytics), **use `-hls_pdt_use_realtime 1`**:

**Why:**
1. **No drift accumulation** - Over 24 hours, even small errors accumulate significantly
2. **Accurate timestamps** - Each frame's PDT matches real time, essential for CV analytics
3. **Event correlation** - When CV detects an event, you can correlate it with exact wall-clock time
4. **Frame accuracy** - Frame timestamps in `frame.raw` will always be accurate relative to real time

**Recommended command:**
```bash
ffmpeg -i rtsp://camera/stream \
    -c:v libx264 -preset ultrafast -c:a aac \
    -f hls \
    -hls_time 2 \
    -hls_list_size 0 \
    -hls_flags program_date_time+append_list \
    -hls_frame_output /path/to/frame.raw \
    -hls_frame_interval 30 \
    -hls_pdt_use_realtime 1 \
    playlist.m3u8
```

**Trade-off:** PDT in playlist may not be perfectly continuous (each segment uses current time), but frame timestamps in `frame.raw` will be accurate, which is more important for CV analytics.

## Complete Example

```bash
# HLS with frame output, PDT drift correction, and append_list mode
ffmpeg -i rtsp://example.com/stream \
    -c:v libx264 -preset ultrafast -c:a aac \
    -f hls \
    -hls_time 2 \
    -hls_list_size 0 \
    -hls_flags program_date_time+append_list \
    -hls_frame_output /path/to/frame.raw \
    -hls_frame_interval 30 \
    -hls_pdt_drift_threshold 1.0 \
    playlist.m3u8
```

## Reading frame.raw in Python

```python
import cv2
import numpy as np

def read_frame_raw(filename):
    with open(filename, 'rb') as f:
        # Read metadata line
        metadata_line = f.readline().decode('utf-8').strip()
        
        # Parse metadata
        metadata = {}
        for item in metadata_line.split(','):
            key, value = item.split('=', 1)
            metadata[key] = value
        
        width = int(metadata['width'])
        height = int(metadata['height'])
        
        # Read BGR data
        bgr_data = f.read(width * height * 3)
        
        # Convert to numpy array
        frame = np.frombuffer(bgr_data, dtype=np.uint8)
        frame = frame.reshape((height, width, 3))
        
        return frame, metadata

# Usage
frame, metadata = read_frame_raw('frame.raw')
print(f"Segment: {metadata['name']}, Offset: {metadata['offset']}s")
print(f"PDT: {metadata.get('program_date_time', 'N/A')}")
cv2.imshow('Frame', frame)
cv2.waitKey(0)
```

## Notes

- Frame output requires video decoding (cannot use `-c copy` for video)
- The decoder uses the **output** codec (e.g., H.264), not the input codec (e.g., HEVC)
- Frame output is non-blocking and will not slow down HLS segment writing
- PDT drift correction works with `append_list` mode for seamless restarts
- When using `append_list`, PDT continuity is maintained from the parsed playlist

