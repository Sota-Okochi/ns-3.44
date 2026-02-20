#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  tools/video_to_udp_trace.sh -i INPUT_VIDEO -o OUTPUT_DAT [options]

Options:
  -i, --input PATH            Input video file (required)
  -o, --output PATH           Output .dat file (required)
  -d, --duration-sec N        Trim to first N seconds (default: no trim)
  -s, --stream-index N        Video stream index (default: 0)
  -h, --help                  Show this help

Output format (.dat):
  <index> <frame_type(I|P|B)> <timestamp_ms> <size_bytes>

Notes:
  - Requires ffprobe (from FFmpeg).
  - Timestamp is normalized to start from 0 ms.
EOF
}

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Error: '$1' is required but not found in PATH." >&2
    exit 1
  fi
}

input=""
output=""
duration_sec=""
stream_index="0"

while [[ $# -gt 0 ]]; do
  case "$1" in
    -i|--input)
      input="${2:-}"
      shift 2
      ;;
    -o|--output)
      output="${2:-}"
      shift 2
      ;;
    -d|--duration-sec)
      duration_sec="${2:-}"
      shift 2
      ;;
    -s|--stream-index)
      stream_index="${2:-}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ -z "${input}" || -z "${output}" ]]; then
  usage >&2
  exit 1
fi

if [[ ! -f "${input}" ]]; then
  echo "Error: input file not found: ${input}" >&2
  exit 1
fi

if [[ -n "${duration_sec}" && ! "${duration_sec}" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
  echo "Error: --duration-sec must be a number." >&2
  exit 1
fi

if [[ ! "${stream_index}" =~ ^[0-9]+$ ]]; then
  echo "Error: --stream-index must be a non-negative integer." >&2
  exit 1
fi

require_cmd ffprobe

max_ms="-1"
if [[ -n "${duration_sec}" ]]; then
  max_ms=$(awk -v sec="${duration_sec}" 'BEGIN { printf("%.0f", sec * 1000.0) }')
fi

tmp_output="$(mktemp)"
trap 'rm -f "${tmp_output}"' EXIT

# ffprobe output example with compact format and nk=1:
# 0.000000|I|240000
ffprobe -v error \
  -select_streams "v:${stream_index}" \
  -show_frames \
  -show_entries frame=best_effort_timestamp_time,pict_type,pkt_size \
  -of compact=p=0:nk=1 \
  "${input}" \
| awk -F'|' -v max_ms="${max_ms}" '
function to_ms(ts) {
  return int((ts * 1000.0) + 0.5);
}
{
  t = $1;
  typ = $2;
  sz = $3;

  if (typ != "I" && typ != "P" && typ != "B") {
    next;
  }
  if (t == "N/A" || sz == "N/A") {
    next;
  }

  ms = to_ms(t + 0.0);
  if (ms < 0) {
    next;
  }
  if (max_ms >= 0 && ms > max_ms) {
    next;
  }

  if (!have_base) {
    base_ms = ms;
    have_base = 1;
  }
  ms -= base_ms;
  if (ms < 0) {
    ms = 0;
  }
  # Keep monotonic timestamps if input jitter exists.
  if (n > 0 && ms < prev_ms) {
    ms = prev_ms;
  }

  n++;
  prev_ms = ms;
  sum_bytes += (sz + 0);
  last_ms = ms;

  printf("%d %s %d %d\n", n, typ, ms, sz + 0);
}
END {
  if (n == 0) {
    print "Error: no valid video frames were parsed." > "/dev/stderr";
    exit 2;
  }
  dur_s = (last_ms > 0) ? (last_ms / 1000.0) : 0.0;
  avg_bps = (dur_s > 0.0) ? ((sum_bytes * 8.0) / dur_s) : 0.0;
  printf("Generated frames: %d\n", n) > "/dev/stderr";
  printf("Duration: %.3f s\n", dur_s) > "/dev/stderr";
  printf("Total bytes: %.0f\n", sum_bytes) > "/dev/stderr";
  printf("Average bitrate: %.0f bps (%.6f Mbps)\n", avg_bps, avg_bps / 1e6) > "/dev/stderr";
}
' > "${tmp_output}"

mv "${tmp_output}" "${output}"
echo "Wrote: ${output}" >&2
