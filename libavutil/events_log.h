/*
 * FFmpeg Events Logging
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVUTIL_EVENTS_LOG_H
#define AVUTIL_EVENTS_LOG_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

/**
 * Log an event to FFMPEG_EVENTS_LOG file (if set via environment variable).
 * Format: JSON lines with ISO8601 timestamp.
 *
 * @param event Event name (e.g., "SEGMENT_START")
 * @param fmt printf-style format for additional JSON fields (without leading comma)
 *            Pass NULL if no additional fields needed.
 */
static inline void ff_log_event(const char *event, const char *fmt, ...)
{
    const char *log_path = getenv("FFMPEG_EVENTS_LOG");
    struct timeval tv;
    struct tm tm_info;
    char ts_buf[64];
    FILE *fp;

    if (!log_path || !log_path[0])
        return;

    fp = fopen(log_path, "a");
    if (!fp)
        return;

    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm_info);
    snprintf(ts_buf, sizeof(ts_buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03d",
             tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
             tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec,
             (int)(tv.tv_usec / 1000));

    fprintf(fp, "{\"ts\":\"%s\",\"event\":\"%s\"", ts_buf, event);

    if (fmt && fmt[0]) {
        va_list args;
        va_start(args, fmt);
        fprintf(fp, ",");
        vfprintf(fp, fmt, args);
        va_end(args);
    }

    fprintf(fp, "}\n");
    fclose(fp);
}

#endif /* AVUTIL_EVENTS_LOG_H */

