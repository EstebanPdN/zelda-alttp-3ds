#!/usr/bin/env python3
"""MSU-1 track validator (zero dependencies, Python standard library only).

Scans an MSU-1 track directory and checks every file against the loading
requirements of this project (engine/src/audio.c):

  * 8-byte header: 4-byte tag + repeat_position (uint32 little-endian, samples)
  * PCM tracks: the tag must be b"MSU1"; the payload is 44100 Hz / 16-bit
    signed / interleaved stereo (4 bytes per sample)
  * Opus tracks (--ext opuz): this project's loader requires the tag b"OPUZ";
    each packet is a 2-byte length (low 15 bits, at most 1275 bytes) + payload

Checks:
  1. Extension, file size (at least 8 bytes), header tag
  2. PCM duration (size - 8) / 4 / 44100; warn when shorter than 5 seconds or
     longer than 30 minutes (track 0 is the menu/title track and is allowed to
     be short, so no lower bound applies to it)
  3. PCM payload length is a multiple of 4; repeat_position lies inside the
     track
  4. Numbering continuity (track 0 is the optional menu/title track; gaps only
     produce a warning and do not affect the exit code)
  5. Summary: total, passed, failed, total duration, missing numbers

Usage:
    python3 tools/check_msu_tracks.py
    python3 tools/check_msu_tracks.py /path/to/msu
    python3 tools/check_msu_tracks.py msu --quiet
    python3 tools/check_msu_tracks.py msu --ext opuz --json

Options:
    --ext pcm|opuz  extension to validate (default: pcm)
    --quiet         print only errors and the summary
    --json          emit JSON (for CI and scripts)

Exit codes:
    0  all tracks are valid (warnings do not affect the exit code)
    1  at least one error, or the directory is missing, or it contains no
       tracks to validate

Notes:
    * The game builds file names as "<number>.pcm" (or .opuz), so the name
      must be a plain number. Non-canonical names such as "007.pcm",
      non-numeric names, and duplicate numbers all produce warnings.
    * A PCM repeat_position is a sample offset, not a byte offset; the loader
      wraps around when it exceeds the total sample count, so we warn here.
    * The Opus duration depends on the decoded output and cannot be computed
      exactly without pulling in a decoder library, so the report shows
      "unknown" and the duration sanity check is skipped.
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import sys
import unicodedata
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple

SAMPLE_RATE = 44100
BYTES_PER_SAMPLE = 4          # 16-bit interleaved stereo: L(2) + R(2)
HEADER_SIZE = 8
MIN_DURATION = 5.0            # seconds; shorter than this is suspicious
MAX_DURATION = 30 * 60.0      # seconds; longer than this is suspicious
MAX_OPUS_PACKET = 1275        # per-packet Opus limit; the loader checks it too

# Header tag per extension. PCM is the MSU-1 standard; the Opus tag comes from
# the opuz branch in this project's audio.c (read little-endian as 'ZUPO',
# i.e. the byte sequence OPUZ).
EXPECTED_TAGS: Dict[str, bytes] = {
    "pcm": b"MSU1",
    "opuz": b"OPUZ",
}


class ScanError(Exception):
    """Directory-level error (missing, unreadable, or nothing to validate)."""


@dataclass
class TrackResult:
    """Validation result for a single track."""

    path: Path
    name: str
    number: Optional[int] = None
    size: int = 0
    duration: Optional[float] = None
    repeat_position: Optional[int] = None
    total_samples: Optional[int] = None
    packet_count: Optional[int] = None
    errors: List[str] = field(default_factory=list)
    warnings: List[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        """No errors means the file is valid (warnings do not count)."""
        return not self.errors


@dataclass
class ScanResult:
    """Scan result for a whole directory."""

    directory: Path
    ext: str
    tracks: List[TrackResult]
    missing: List[int]
    duplicates: Dict[int, List[str]]

    @property
    def failed(self) -> int:
        return sum(1 for track in self.tracks if not track.ok)

    @property
    def passed(self) -> int:
        return len(self.tracks) - self.failed

    @property
    def warning_count(self) -> int:
        return sum(len(track.warnings) for track in self.tracks)


def display_width(text: str) -> int:
    """Terminal display width; CJK full-width characters take 2 columns."""
    return sum(2 if unicodedata.east_asian_width(ch) in ("W", "F") else 1
               for ch in text)


def pad(text: str, width: int, align: str = "left") -> str:
    """Pad with spaces by display width (rather than character count)."""
    fill = " " * max(0, width - display_width(text))
    return fill + text if align == "right" else text + fill


def human_size(size: int) -> str:
    """Format a byte count as a human-readable size."""
    if size >= 1024 * 1024:
        return f"{size / (1024 * 1024):.1f} MB"
    if size >= 1024:
        return f"{size / 1024:.1f} KB"
    return f"{size} B"


def format_duration(seconds: Optional[float], tenths: bool = True) -> str:
    """Format as M:SS.s (or H:MM:SS.s); None renders as "—"."""
    if seconds is None:
        return "—"
    seconds = max(0.0, seconds)
    if tenths:
        total_tenths = int(round(seconds * 10))
        hours, rem = divmod(total_tenths, 36000)
        minutes, rem = divmod(rem, 600)
        secs, tenth = divmod(rem, 10)
        if hours:
            return f"{hours}:{minutes:02d}:{secs:02d}.{tenth}"
        return f"{minutes}:{secs:02d}.{tenth}"
    total = int(round(seconds))
    hours, rem = divmod(total, 3600)
    minutes, secs = divmod(rem, 60)
    if hours:
        return f"{hours}:{minutes:02d}:{secs:02d}"
    return f"{minutes}:{secs:02d}"


def format_number_list(numbers: Sequence[int], limit: int = 16) -> str:
    """List numbers comma-separated; truncate long lists to avoid flooding."""
    if not numbers:
        return "none"
    shown = ", ".join(str(n) for n in numbers[:limit])
    if len(numbers) > limit:
        shown += f" … ({len(numbers)} total)"
    return shown


# Prefixes the 3DS build probes for, in order; the number follows one of them.
TRACK_PREFIXES = ("alttp_msu-", "msu-", "")


def parse_track_number(name: str) -> Optional[int]:
    """Parse the track number from a file name.

    Stock MSU packs (Snes9x/bsnes naming) use "alttp_msu-1.pcm"; plain packs use
    "1.pcm".  The 3DS build probes both, so accept either here and require the
    remainder to be ASCII digits.
    """
    stem = Path(name).stem
    for prefix in TRACK_PREFIXES:
        if prefix and not stem.startswith(prefix):
            continue
        rest = stem[len(prefix):]
        if rest.isascii() and rest.isdigit():
            return int(rest)
    return None


def check_file(path: Path, ext: str) -> TrackResult:
    """Validate the contents of one track file (numbering is handled later)."""
    result = TrackResult(path=path, name=path.name)
    result.number = parse_track_number(path.name)

    try:
        result.size = path.stat().st_size
    except OSError as exc:
        result.errors.append(
            f"Cannot read file information: {exc.strerror or exc}"
        )
        return result

    if result.size < HEADER_SIZE:
        result.errors.append(
            f"File is only {result.size} bytes, shorter than the 8-byte header"
        )
        return result

    try:
        with path.open("rb") as handle:
            header = handle.read(HEADER_SIZE)
    except OSError as exc:
        result.errors.append(f"Cannot open file: {exc.strerror or exc}")
        return result

    if len(header) < HEADER_SIZE:
        # Only reachable if the file is truncated between stat and read
        result.errors.append(
            f"File is only {len(header)} bytes, shorter than the 8-byte header"
        )
        return result

    tag = header[:4]
    result.repeat_position = struct.unpack("<I", header[4:8])[0]

    expected = EXPECTED_TAGS[ext]
    if tag != expected:
        if ext == "pcm":
            result.errors.append(
                f"Header tag is not MSU1 (found {tag!r}); "
                "this is not an MSU-1 PCM file"
            )
        else:
            result.errors.append(
                f"Header tag is not OPUZ (found {tag!r}); this is not an "
                "MSU-1 Opus track for this project"
            )
        return result

    if ext == "pcm":
        check_pcm(result)
    else:
        check_opuz(result)
    return result


def check_pcm(result: TrackResult) -> None:
    """PCM-specific checks: duration, data alignment, loop point."""
    data_size = result.size - HEADER_SIZE
    result.total_samples = data_size // BYTES_PER_SAMPLE
    result.duration = data_size / BYTES_PER_SAMPLE / SAMPLE_RATE

    if data_size % BYTES_PER_SAMPLE:
        result.warnings.append(
            f"PCM payload is {data_size} bytes, not a multiple of 4; "
            f"the trailing {data_size % BYTES_PER_SAMPLE} bytes are ignored"
        )

    # Track 0 is the menu/title track and is short by design, so the 5-second
    # lower bound does not apply to it.
    if result.duration <= MIN_DURATION and result.number != 0:
        result.warnings.append(
            f"Duration is only {result.duration:.1f}s, under 5 seconds; "
            "the file may be truncated or a sound effect"
        )
    elif result.duration >= MAX_DURATION:
        result.warnings.append(
            f"Duration {format_duration(result.duration)} exceeds 30 minutes; "
            "check whether tracks were concatenated"
        )

    if result.repeat_position is not None and result.total_samples is not None:
        if result.repeat_position > result.total_samples:
            result.warnings.append(
                f"repeat_position={result.repeat_position} is beyond the "
                f"total sample count {result.total_samples}; the loop point "
                "is invalid (the loader wraps around)"
            )
        elif (result.repeat_position == result.total_samples
              and result.total_samples > 0):
            result.warnings.append(
                f"repeat_position={result.repeat_position} points at the "
                "end of the track; looping ends immediately"
            )


def check_opuz(result: TrackResult) -> None:
    """Opus-specific checks: container packets only; duration stays unknown."""
    offset = HEADER_SIZE
    packets = 0
    try:
        with result.path.open("rb") as handle:
            handle.seek(HEADER_SIZE)
            while offset < result.size:
                raw = handle.read(2)
                if len(raw) < 2:
                    result.warnings.append(
                        f"Truncated {len(raw)}-byte packet length field "
                        "at end of file"
                    )
                    break
                length = struct.unpack("<H", raw)[0] & 0x7FFF
                if length > MAX_OPUS_PACKET:
                    result.errors.append(
                        f"Packet {packets + 1} length {length} exceeds the "
                        f"{MAX_OPUS_PACKET}-byte limit"
                    )
                    break
                if offset + 2 + length > result.size:
                    result.errors.append(
                        f"Packet {packets + 1} runs past the end of the file "
                        f"(offset {offset}, length {length}, "
                        f"file size {result.size} bytes)"
                    )
                    break
                handle.seek(length, os.SEEK_CUR)
                offset += 2 + length
                packets += 1
    except OSError as exc:
        result.errors.append(
            f"Failed to read Opus packets: {exc.strerror or exc}"
        )
        return

    result.packet_count = packets
    if packets == 0:
        result.errors.append("File has a header but no Opus packets")
    # The Opus duration depends on decoder output and has nothing to do with
    # (size-8)/4/44100; without libopus there is no basis for an estimate, so
    # duration stays None.


def check_numbering(tracks: Sequence[TrackResult], ext: str) -> Dict[int, List[str]]:
    """Fill in numbers, check naming and duplicates; return duplicates."""
    by_number: Dict[int, List[TrackResult]] = {}
    for track in tracks:
        number = parse_track_number(track.name)
        track.number = number
        if number is None:
            track.warnings.append(
                f"File name \"{track.name}\" is not a numeric track number; "
                f"the game looks for <number>.{ext} and will not load it"
            )
            continue
        by_number.setdefault(number, []).append(track)
        # Accepted: "<n>.<ext>" or "alttp_msu-<n>.<ext>" -- the prefixes the 3DS
        # build probes.  Padding ("007.pcm") or a stray prefix parses to the right
        # number but will not be found at runtime.
        canonical = {f"{number}.{ext}", f"alttp_msu-{number}.{ext}"}
        if track.name not in canonical:
            track.warnings.append(
                f"File name \"{track.name}\" is not a canonical name; the "
                f"game looks for {number}.{ext} or alttp_msu-{number}.{ext}"
            )

    duplicates: Dict[int, List[str]] = {
        number: [track.name for track in group]
        for number, group in by_number.items()
        if len(group) > 1
    }
    for number, names in duplicates.items():
        for track in by_number[number]:
            others = ", ".join(name for name in names if name != track.name)
            track.warnings.append(
                f"Number {number} is duplicated (also {others}); the game "
                "loads only one of them"
            )
    return duplicates


def scan_dir(directory: Path, ext: str) -> ScanResult:
    """Scan a directory; return per-file results, gaps, and duplicates."""
    if not directory.exists():
        raise ScanError(f"Directory does not exist: {directory}")
    if not directory.is_dir():
        raise ScanError(f"Not a directory: {directory}")

    suffix = "." + ext.lower()
    broken_links: List[Path] = []
    try:
        entries: List[Tuple[Optional[int], Path]] = []
        for entry in os.scandir(directory):
            path = Path(entry.path)
            if path.suffix.lower() != suffix:
                continue
            # is_file() silently skips dangling symlinks. Track packs often use
            # symlinks into external storage, and a broken link must be an
            # error here; otherwise it only shows up as a "missing number".
            if entry.is_symlink() and not path.exists():
                broken_links.append(path)
                continue
            if not entry.is_file():
                continue
            entries.append((parse_track_number(path.name), path))
    except OSError as exc:
        raise ScanError(
            f"Cannot read directory {directory}: {exc.strerror or exc}"
        ) from exc

    if not entries and not broken_links:
        raise ScanError(f"No *{suffix} files found in {directory}")

    # Sort by number first (menu track 0 first), non-numeric names last.
    entries.sort(key=lambda item: (
        item[0] is None,
        item[0] if item[0] is not None else 0,
        item[1].name.lower(),
    ))

    tracks = [check_file(path, ext) for _, path in entries]
    for path in broken_links:
        broken = TrackResult(path=path, name=path.name)
        broken.number = parse_track_number(path.name)
        broken.errors.append("Symbolic link target does not exist")
        tracks.append(broken)

    # Number parsing, canonical file name check, duplicate number check.
    duplicates = check_numbering(tracks, ext)

    # Continuity: track 0 is the optional menu/title track and never counts as
    # missing; check from 1 up to the highest number.
    missing: List[int] = []
    present = {track.number for track in tracks if track.number is not None}
    if present:
        missing = [n for n in range(1, max(present) + 1) if n not in present]

    return ScanResult(
        directory=directory,
        ext=ext,
        tracks=tracks,
        missing=missing,
        duplicates=duplicates,
    )


def total_duration(tracks: Sequence[TrackResult]) -> Tuple[float, int]:
    """Return (sum of known durations, valid files with unknown duration)."""
    known = 0.0
    unknown = 0
    for track in tracks:
        if track.duration is not None:
            known += track.duration
        elif track.ok:
            unknown += 1
    return known, unknown


def scan_to_json(scan: ScanResult) -> Dict[str, Any]:
    """Build a stable JSON structure, so CI can assert on it."""
    known, unknown = total_duration(scan.tracks)
    has_duration = any(track.duration is not None for track in scan.tracks)
    return {
        "directory": str(scan.directory),
        "ext": scan.ext,
        "expected_tag": EXPECTED_TAGS[scan.ext].decode("ascii"),
        "files": [
            {
                "number": track.number,
                "name": track.name,
                "path": str(track.path),
                "size": track.size,
                "duration_seconds": (
                    round(track.duration, 3)
                    if track.duration is not None else None
                ),
                "duration": (
                    format_duration(track.duration)
                    if track.duration is not None else None
                ),
                "repeat_position": track.repeat_position,
                "total_samples": track.total_samples,
                "packet_count": track.packet_count,
                "ok": track.ok,
                "errors": track.errors,
                "warnings": track.warnings,
            }
            for track in scan.tracks
        ],
        "summary": {
            "total": len(scan.tracks),
            "passed": scan.passed,
            "failed": scan.failed,
            "total_duration_seconds": round(known, 3) if has_duration else None,
            "total_duration": (
                format_duration(known, tenths=False) if has_duration else None
            ),
            "duration_unknown_files": unknown,
            "missing_numbers": scan.missing,
            "duplicate_numbers": sorted(scan.duplicates),
            "warning_count": scan.warning_count,
            "ok": scan.failed == 0,
        },
    }


def report(scan: ScanResult, quiet: bool = False, as_json: bool = False) -> int:
    """Print the report and return the exit code (0 all valid, 1 any error)."""
    if as_json:
        print(json.dumps(scan_to_json(scan), ensure_ascii=False, indent=2))
        return 0 if scan.failed == 0 else 1

    directory = str(scan.directory).rstrip(os.sep) + os.sep
    if scan.ext == "pcm":
        detail = "PCM · 44100 Hz · 16-bit · interleaved stereo"
    else:
        detail = "Opus · container packet check (duration unknown)"
    print(f"MSU-1 track check: {directory}  ({detail})")

    # Table: number, file, size, duration
    if not quiet:
        numbers = [len(str(t.number)) for t in scan.tracks
                   if t.number is not None]
        number_width = max([4] + numbers)
        name_width = max(4, max(display_width(t.name) for t in scan.tracks))
        size_width = max(6, max(display_width(human_size(t.size))
                                for t in scan.tracks))
        duration_width = max(6, max(display_width(format_duration(t.duration))
                                    for t in scan.tracks))
        print("  {}   {}   {}   {}".format(
            pad("No.", number_width, "right"),
            pad("File", name_width),
            pad("Size", size_width, "right"),
            pad("Duration", duration_width, "right"),
        ))
        for track in scan.tracks:
            number = str(track.number) if track.number is not None else "—"
            print("  {}   {}   {}   {}".format(
                pad(number, number_width, "right"),
                pad(track.name, name_width),
                pad(human_size(track.size), size_width, "right"),
                pad(format_duration(track.duration), duration_width, "right"),
            ))

    # Errors are always printed; with --quiet the per-file warnings are dropped
    # and only errors and the summary remain.
    issue_lines: List[str] = []
    for track in scan.tracks:
        for message in track.errors:
            issue_lines.append(f"  ❌ {track.name}: {message}")
    if not quiet:
        for track in scan.tracks:
            for message in track.warnings:
                issue_lines.append(f"  ⚠ {track.name}: {message}")
    if issue_lines:
        print()
        for line in issue_lines:
            print(line)

    known, unknown = total_duration(scan.tracks)
    has_duration = any(track.duration is not None for track in scan.tracks)
    if has_duration:
        duration_text = format_duration(known, tenths=False)
        if unknown:
            duration_text += f" (+{unknown} with unknown duration)"
    else:
        duration_text = "unknown"

    parts = []
    if scan.failed:
        parts.append(f"passed {scan.passed}/{len(scan.tracks)} "
                     f"(failed {scan.failed})")
    else:
        parts.append(f"passed {scan.passed}/{len(scan.tracks)}")
    parts.append(f"total duration {duration_text}")
    if scan.missing:
        parts.append(f"missing numbers ({len(scan.missing)}): "
                     f"{format_number_list(scan.missing)}")
    else:
        parts.append("no missing numbers")
    if scan.warning_count and not quiet:
        parts.append(f"warnings: {scan.warning_count}")

    if issue_lines or not quiet:
        print()
    print("  " + ", ".join(parts))
    if scan.missing:
        if any(track.number == 0 for track in scan.tracks):
            hint = "Missing numbers fall back to the original music in-game."
        else:
            hint = ("Track 0 is the optional menu/title track; missing "
                    "numbers fall back to the original music in-game.")
        print(f"  Hint: {hint}")

    return 0 if scan.failed == 0 else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="check_msu_tracks.py",
        description=("Validate an MSU-1 track directory (PCM/Opus) against "
                     "this project's loading requirements."),
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Examples:\n"
            "  python3 tools/check_msu_tracks.py\n"
            "  python3 tools/check_msu_tracks.py msu --quiet\n"
            "  python3 tools/check_msu_tracks.py /path/to/msu --ext opuz --json"
        ),
    )
    parser.add_argument(
        "directory", nargs="?", default="msu", metavar="DIRECTORY",
        help="track directory (default: msu)",
    )
    parser.add_argument(
        "--ext", choices=("pcm", "opuz"), default="pcm", metavar="pcm|opuz",
        help="extension to validate (default: pcm)",
    )
    parser.add_argument(
        "--quiet", action="store_true",
        help="print only errors and the summary",
    )
    parser.add_argument(
        "--json", action="store_true",
        help="emit JSON (for CI and scripts)",
    )
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    directory = Path(args.directory)
    try:
        scan = scan_dir(directory, args.ext)
    except ScanError as exc:
        if args.json:
            print(json.dumps({
                "directory": str(directory),
                "ext": args.ext,
                "error": str(exc),
                "ok": False,
            }, ensure_ascii=False, indent=2))
        else:
            print(f"❌ {exc}", file=sys.stderr)
        return 1
    return report(scan, quiet=args.quiet, as_json=args.json)


if __name__ == "__main__":
    sys.exit(main())
