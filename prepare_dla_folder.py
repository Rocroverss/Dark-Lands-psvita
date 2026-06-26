#!/usr/bin/env python3
"""Prepare a ready-to-copy ux0:data/dla folder from a Dark Lands APK."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tempfile
from typing import List, Optional, Tuple
import zipfile


EXPECTED_SHA256 = "7ecd7f1c4cd2e9d9066b0515c7bde2af7ce6d0a8b7d79deab652b98e7f3d1b0e"
SO_ENTRY = "lib/armeabi-v7a/libcocos2dcpp.so"
ASSETS_PREFIX = "assets/"


class PrepError(RuntimeError):
    pass


def log(message: str) -> None:
    print(f"[prepare_dla] {message}")


def safe_archive_path(name: str) -> PurePosixPath:
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts:
        raise PrepError(f"unsafe APK entry: {name}")
    return path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def copy_zip_member(apk: zipfile.ZipFile, member: str, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with apk.open(member) as source, destination.open("wb") as target:
        shutil.copyfileobj(source, target)


def copy_file(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    try:
        same_file = destination.exists() and source.resolve() == destination.resolve()
    except OSError:
        same_file = False
    if same_file:
        return
    shutil.copy2(source, destination)


def find_ffmpeg(explicit_path: Optional[str]) -> str:
    if explicit_path:
        found = shutil.which(explicit_path)
        return found or explicit_path

    found = shutil.which("ffmpeg")
    if not found:
        raise PrepError(
            "ffmpeg was not found in PATH. Install ffmpeg or pass --ffmpeg path/to/ffmpeg."
        )
    return found


def convert_mp3_assets(
    apk: zipfile.ZipFile,
    mp3_entries: List[str],
    output_root: Path,
    ffmpeg: str,
    quality: float,
) -> int:
    converted = 0
    with tempfile.TemporaryDirectory(prefix="darklands-audio-") as temporary:
        temp_root = Path(temporary)
        for index, member in enumerate(mp3_entries, 1):
            relative = safe_archive_path(member)
            source = temp_root / f"audio-{index}.mp3"
            destination = output_root.joinpath(*relative.with_suffix(".ogg").parts)
            destination.parent.mkdir(parents=True, exist_ok=True)

            source.write_bytes(apk.read(member))
            subprocess.run(
                [
                    ffmpeg,
                    "-nostdin",
                    "-hide_banner",
                    "-loglevel",
                    "error",
                    "-y",
                    "-i",
                    str(source),
                    "-vn",
                    "-c:a",
                    "libvorbis",
                    "-q:a",
                    str(quality),
                    str(destination),
                ],
                check=True,
            )
            converted += 1
            log(f"audio {index}/{len(mp3_entries)}: {member} -> {destination}")

    return converted


def validate_apk(
    apk_path: Path,
    expected_sha256: str,
    skip_sha256: bool,
    convert_audio: bool,
    ffmpeg_arg: Optional[str],
) -> Tuple[str, List[str], List[str], Optional[str]]:
    if not apk_path.is_file():
        raise PrepError(f"APK does not exist: {apk_path}")

    actual_sha256 = sha256_file(apk_path)
    log(f"SHA-256: {actual_sha256}")
    if not skip_sha256 and actual_sha256.lower() != expected_sha256.lower():
        raise PrepError(
            "APK SHA-256 mismatch.\n"
            f"  expected: {expected_sha256}\n"
            f"  actual:   {actual_sha256}\n"
            "Use the expected Dark Lands 1.5.6 APK, or pass --skip-sha256 if you "
            "intentionally want to prepare a different APK."
        )

    try:
        with zipfile.ZipFile(apk_path) as apk:
            names = apk.namelist()
            if SO_ENTRY not in names:
                raise PrepError(f"APK is missing {SO_ENTRY}")

            asset_entries = [
                name for name in names if name.startswith(ASSETS_PREFIX) and not name.endswith("/")
            ]
            if not asset_entries:
                raise PrepError(f"APK has no files under {ASSETS_PREFIX}")

            mp3_entries = [
                name for name in asset_entries if name.lower().endswith(".mp3")
            ]
    except zipfile.BadZipFile as exc:
        raise PrepError(f"not a valid APK/ZIP file: {apk_path}") from exc

    ffmpeg = None
    if convert_audio and mp3_entries:
        ffmpeg = find_ffmpeg(ffmpeg_arg)

    return actual_sha256, asset_entries, mp3_entries, ffmpeg


def prepare_folder(args: argparse.Namespace) -> int:
    apk_path = args.apk
    output_root = args.output

    _, asset_entries, mp3_entries, ffmpeg = validate_apk(
        apk_path=apk_path,
        expected_sha256=args.expected_sha256,
        skip_sha256=args.skip_sha256,
        convert_audio=not args.no_audio_convert,
        ffmpeg_arg=args.ffmpeg,
    )

    log(f"output folder: {output_root}")
    log(f"assets to extract: {len(asset_entries)}")
    log(f"MP3 files to convert: {len(mp3_entries)}")

    if args.dry_run:
        log("dry run complete; no files were written")
        return 0

    output_root.mkdir(parents=True, exist_ok=True)
    (output_root / "gxp").mkdir(parents=True, exist_ok=True)

    copy_file(apk_path, output_root / "base.apk")

    with zipfile.ZipFile(apk_path) as apk:
        copy_zip_member(apk, SO_ENTRY, output_root / "libcocos2dcpp.so")

        for member in asset_entries:
            relative = safe_archive_path(member)
            copy_zip_member(apk, member, output_root.joinpath(*relative.parts))

        converted = 0
        if not args.no_audio_convert and mp3_entries:
            converted = convert_mp3_assets(
                apk=apk,
                mp3_entries=mp3_entries,
                output_root=output_root,
                ffmpeg=ffmpeg or find_ffmpeg(args.ffmpeg),
                quality=args.quality,
            )

    log("done")
    log(f"wrote: {output_root / 'base.apk'}")
    log(f"wrote: {output_root / 'libcocos2dcpp.so'}")
    log(f"extracted assets: {len(asset_entries)}")
    log(f"converted audio files: {converted}")
    log(f"copy this folder to the Vita as ux0:data/dla: {output_root}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Verify a Dark Lands APK and create a ready-to-copy dla folder with "
            "base.apk, libcocos2dcpp.so, extracted assets, and converted OGG audio."
        )
    )
    parser.add_argument("apk", type=Path, help="path to dark-lands-1-5-6.apk")
    parser.add_argument(
        "output",
        nargs="?",
        type=Path,
        default=Path("dla"),
        help="output folder to create or update (default: dla)",
    )
    parser.add_argument(
        "--expected-sha256",
        default=EXPECTED_SHA256,
        help=f"expected APK SHA-256 (default: {EXPECTED_SHA256})",
    )
    parser.add_argument(
        "--skip-sha256",
        action="store_true",
        help="do not fail when the APK SHA-256 differs from the expected value",
    )
    parser.add_argument(
        "--no-audio-convert",
        action="store_true",
        help="extract assets but do not convert MP3 files to OGG",
    )
    parser.add_argument(
        "--quality",
        type=float,
        default=5.0,
        help="ffmpeg libvorbis quality from -1 to 10 (default: 5)",
    )
    parser.add_argument(
        "--ffmpeg",
        help="path to ffmpeg, or command name if it is not simply 'ffmpeg'",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="verify the APK and print planned work without writing files",
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return prepare_folder(args)
    except subprocess.CalledProcessError as exc:
        parser.error(f"ffmpeg failed while converting audio: {exc}")
    except PrepError as exc:
        parser.error(str(exc))
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
