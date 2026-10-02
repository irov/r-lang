#!/usr/bin/env python3
"""Exercise the R zip and unzip examples with deterministic generated trees."""

from __future__ import annotations

import argparse
import hashlib
import os
import random
import shlex
import subprocess
import sys
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path


DEFAULT_SEEDS = (
    0x5A495001,
    0x5A495002,
    0x00C0FFEE,
    0x00DEC0DE,
    0x0BAD5EED,
)
SAFETY_SEED = 0x53414645
PROCESS_TIMEOUT_SECONDS = 60
RANDOM_ALPHABET = "abcdefghijklmnopqrstuvwxyz0123456789"
SUPPORTED_METHODS = {zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED}


class HarnessFailure(RuntimeError):
    """A deterministic test failure associated with an optional replay seed."""

    def __init__(self, message: str, seed: int | None = None) -> None:
        super().__init__(message)
        self.seed = seed


@dataclass(frozen=True)
class Corpus:
    directories: tuple[str, ...]
    files: dict[str, bytes]

    def entry_arguments(self) -> list[str]:
        directory_arguments = [f"{name}/" for name in self.directories]
        return directory_arguments + sorted(self.files)


@dataclass(frozen=True)
class TreeSnapshot:
    directories: tuple[str, ...]
    files: dict[str, bytes]


def parse_seed(value: str) -> int:
    try:
        seed = int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError(f"invalid integer seed: {value}") from error
    if seed < 0 or seed > 0xFFFFFFFFFFFFFFFF:
        raise argparse.ArgumentTypeError("seed must be in the u64 range")
    return seed


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zip-executable", type=Path, required=True)
    parser.add_argument("--unzip-executable", type=Path, required=True)
    parser.add_argument(
        "--seed",
        action="append",
        type=parse_seed,
        help="run one replay seed; may be supplied more than once",
    )
    return parser.parse_args()


def require(condition: bool, message: str, seed: int) -> None:
    if not condition:
        raise HarnessFailure(message, seed)


def random_component(random_source: random.Random, prefix: str, index: int) -> str:
    suffix = "".join(random_source.choice(RANDOM_ALPHABET) for _ in range(8))
    return f"{prefix}_{index:02d}_{suffix}"


def random_bytes(random_source: random.Random, size: int) -> bytes:
    return bytes(random_source.getrandbits(8) for _ in range(size))


def build_corpus(source_root: Path, seed: int) -> Corpus:
    random_source = random.Random(seed)
    directories = {
        "empty",
        "nested",
        "nested/deeper",
        "nested/deeper/empty_leaf",
        "unicode",
    }
    data_directories = ["", "nested", "nested/deeper", "unicode"]

    for index in range(3):
        trunk = random_component(random_source, "tree", index)
        branch = random_component(random_source, "branch", index)
        leaf = random_component(random_source, "leaf", index)
        directories.add(trunk)
        directories.add(f"{trunk}/{branch}")
        directories.add(f"{trunk}/{branch}/{leaf}")
        directories.add(f"{trunk}/empty_leaf")
        data_directories.extend((trunk, f"{trunk}/{branch}", f"{trunk}/{branch}/{leaf}"))

    files = {
        "empty.bin": b"",
        "nested/one-byte.bin": bytes((seed & 0xFF,)),
        "nested/deeper/repeated.dat": (b"R-language-zip\x00" * 257) + b"tail",
        "unicode/данные-猫.bin": "безопасный UTF-8 путь\n".encode("utf-8"),
    }
    sizes = (2, 31, 255, 1024, 4097, 16384, random_source.randrange(512, 32769))
    for index, size in enumerate(sizes):
        directory = random_source.choice(data_directories)
        name = random_component(random_source, "random", index) + ".bin"
        relative_name = f"{directory}/{name}" if directory else name
        files[relative_name] = random_bytes(random_source, size)

    ordered_directories = tuple(sorted(directories, key=lambda name: (name.count("/"), name)))
    for directory in ordered_directories:
        (source_root / directory).mkdir(parents=True, exist_ok=True)
    for relative_name, contents in files.items():
        path = source_root / relative_name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(contents)

    return Corpus(directories=ordered_directories, files=files)


def command_diagnostics(completed: subprocess.CompletedProcess[bytes]) -> str:
    stdout = completed.stdout.decode("utf-8", errors="replace").strip()
    stderr = completed.stderr.decode("utf-8", errors="replace").strip()
    parts = [f"exit={completed.returncode}"]
    if stdout:
        parts.append(f"stdout={stdout[-4096:]!r}")
    if stderr:
        parts.append(f"stderr={stderr[-4096:]!r}")
    return ", ".join(parts)


def invoke(
    command: list[str], working_directory: Path, seed: int
) -> subprocess.CompletedProcess[bytes]:
    try:
        return subprocess.run(
            command,
            cwd=working_directory,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=PROCESS_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired as error:
        raise HarnessFailure(
            f"command timed out after {PROCESS_TIMEOUT_SECONDS}s: {shlex.join(command)}",
            seed,
        ) from error
    except OSError as error:
        raise HarnessFailure(f"cannot run {shlex.join(command)}: {error}", seed) from error


def expect_success(command: list[str], working_directory: Path, seed: int, label: str) -> None:
    completed = invoke(command, working_directory, seed)
    require(
        completed.returncode == 0,
        f"{label} failed: {command_diagnostics(completed)}",
        seed,
    )


def expect_failure(command: list[str], working_directory: Path, seed: int, label: str) -> None:
    completed = invoke(command, working_directory, seed)
    require(
        completed.returncode != 0,
        f"{label} unexpectedly succeeded: {command_diagnostics(completed)}",
        seed,
    )


def snapshot_tree(root: Path, seed: int) -> TreeSnapshot:
    directories: list[str] = []
    files: dict[str, bytes] = {}
    for current, directory_names, file_names in os.walk(root):
        directory_names.sort()
        file_names.sort()
        current_path = Path(current)
        for directory_name in directory_names:
            path = current_path / directory_name
            require(not path.is_symlink(), f"unexpected directory symlink: {path}", seed)
            directories.append(path.relative_to(root).as_posix())
        for file_name in file_names:
            path = current_path / file_name
            require(path.is_file() and not path.is_symlink(), f"unexpected non-file: {path}", seed)
            files[path.relative_to(root).as_posix()] = path.read_bytes()
    return TreeSnapshot(directories=tuple(sorted(directories)), files=files)


def describe_name_difference(expected: set[str], actual: set[str]) -> str:
    missing = sorted(expected - actual)
    extra = sorted(actual - expected)
    return f"missing={missing}, extra={extra}"


def compare_trees(source_root: Path, extracted_root: Path, seed: int) -> None:
    source = snapshot_tree(source_root, seed)
    extracted = snapshot_tree(extracted_root, seed)
    source_directories = set(source.directories)
    extracted_directories = set(extracted.directories)
    require(
        source_directories == extracted_directories,
        "directory tree mismatch: "
        + describe_name_difference(source_directories, extracted_directories),
        seed,
    )

    source_names = set(source.files)
    extracted_names = set(extracted.files)
    require(
        source_names == extracted_names,
        "file tree mismatch: " + describe_name_difference(source_names, extracted_names),
        seed,
    )
    for name in sorted(source_names):
        expected = source.files[name]
        actual = extracted.files[name]
        if expected == actual:
            continue
        expected_digest = hashlib.sha256(expected).hexdigest()
        actual_digest = hashlib.sha256(actual).hexdigest()
        raise HarnessFailure(
            f"content mismatch for {name}: expected sha256={expected_digest}, "
            f"actual sha256={actual_digest}",
            seed,
        )


def validate_with_python_zipfile(archive_path: Path, corpus: Corpus, seed: int) -> None:
    expected_names = corpus.entry_arguments()
    try:
        with zipfile.ZipFile(archive_path, "r") as archive:
            infos = archive.infolist()
            actual_names = [info.filename for info in infos]
            require(
                actual_names == expected_names,
                f"central-directory order/names mismatch: expected={expected_names}, "
                f"actual={actual_names}",
                seed,
            )
            require(
                len(actual_names) == len(set(actual_names)),
                "archive contains duplicate central-directory names",
                seed,
            )
            damaged_name = archive.testzip()
            require(
                damaged_name is None,
                f"Python zipfile reports bad CRC for {damaged_name}",
                seed,
            )

            for info in infos:
                require(
                    info.compress_type in SUPPORTED_METHODS,
                    f"unsupported method {info.compress_type} for {info.filename}",
                    seed,
                )
                require((info.flag_bits & 1) == 0, f"encrypted entry {info.filename}", seed)
                if info.filename.endswith("/"):
                    require(
                        info.is_dir(),
                        f"directory entry not marked as directory: {info.filename}",
                        seed,
                    )
                    require(archive.read(info) == b"", f"directory has data: {info.filename}", seed)
                else:
                    expected_contents = corpus.files[info.filename]
                    require(
                        archive.read(info) == expected_contents,
                        f"Python zipfile content mismatch for {info.filename}",
                        seed,
                    )
    except (
        EOFError,
        OSError,
        KeyError,
        RuntimeError,
        zipfile.BadZipFile,
        zipfile.LargeZipFile,
    ) as error:
        raise HarnessFailure(
            f"Python zipfile rejected {archive_path.name}: {error}", seed
        ) from error


def run_corpus(zip_executable: Path, unzip_executable: Path, seed: int) -> None:
    with tempfile.TemporaryDirectory(prefix=f"r-zip-{seed:016x}-") as temporary_directory:
        root = Path(temporary_directory)
        source_root = root / "source"
        first_output = root / "archive-a"
        second_output = root / "archive-b"
        extracted_root = root / "extracted"
        for directory in (source_root, first_output, second_output, extracted_root):
            directory.mkdir()

        corpus = build_corpus(source_root, seed)
        archive_name = f"roundtrip-{seed:016x}.zip"
        entries = corpus.entry_arguments()
        first_command = [str(zip_executable), str(first_output), archive_name, *entries]
        second_command = [str(zip_executable), str(second_output), archive_name, *entries]
        expect_success(first_command, source_root, seed, "first zip invocation")
        expect_success(second_command, source_root, seed, "second zip invocation")

        first_archive = first_output / archive_name
        second_archive = second_output / archive_name
        require(first_archive.is_file(), "first zip invocation did not publish an archive", seed)
        require(second_archive.is_file(), "second zip invocation did not publish an archive", seed)
        require(
            first_archive.read_bytes() == second_archive.read_bytes(),
            "identical input trees did not produce byte-identical archives",
            seed,
        )
        validate_with_python_zipfile(first_archive, corpus, seed)

        unzip_command = [str(unzip_executable), str(first_archive), str(extracted_root)]
        expect_success(unzip_command, root, seed, "R unzip invocation")
        compare_trees(source_root, extracted_root, seed)


def require_empty_directory(directory: Path, seed: int, label: str) -> None:
    names = sorted(path.name for path in directory.iterdir())
    require(not names, f"{label} left output artifacts: {names}", seed)


def run_safety_contracts(zip_executable: Path, unzip_executable: Path) -> None:
    seed = SAFETY_SEED
    with tempfile.TemporaryDirectory(prefix="r-zip-safety-") as temporary_directory:
        root = Path(temporary_directory)
        empty_source = root / "empty-source"
        empty_output = root / "empty-output"
        empty_extracted = root / "empty-extracted"
        source_root = root / "source"
        output_root = root / "output"
        for directory in (
            empty_source,
            empty_output,
            empty_extracted,
            source_root,
            output_root,
        ):
            directory.mkdir()

        empty_archive_name = "empty.zip"
        empty_zip_command = [
            str(zip_executable),
            str(empty_output),
            empty_archive_name,
        ]
        expect_success(empty_zip_command, empty_source, seed, "empty zip invocation")
        empty_archive = empty_output / empty_archive_name
        require(empty_archive.is_file(), "empty zip invocation did not publish an archive", seed)
        validate_with_python_zipfile(empty_archive, Corpus((), {}), seed)
        empty_unzip_command = [
            str(unzip_executable),
            str(empty_archive),
            str(empty_extracted),
        ]
        expect_success(empty_unzip_command, root, seed, "empty R unzip invocation")
        compare_trees(empty_source, empty_extracted, seed)

        (source_root / "nested").mkdir()
        (source_root / "input.bin").write_bytes(b"safety-contract\x00")
        (source_root / "nested/value.bin").write_bytes(b"nested")
        (source_root / "back\\slash.bin").write_bytes(b"backslash")
        (source_root / "colon:name.bin").write_bytes(b"colon")
        (source_root / "node").write_bytes(b"file-directory collision")
        outside_file = root / "outside.bin"
        outside_file.write_bytes(b"outside")
        (root / "outside-directory").mkdir()

        archive_name = "no-replace.zip"
        initial_command = [str(zip_executable), str(output_root), archive_name, "input.bin"]
        expect_success(initial_command, source_root, seed, "initial no-replace fixture")
        archive_path = output_root / archive_name
        require(archive_path.is_file(), "initial no-replace archive is missing", seed)
        original_archive = archive_path.read_bytes()
        original_names = sorted(path.name for path in output_root.iterdir())

        expect_failure(initial_command, source_root, seed, "second no-replace invocation")
        require(archive_path.is_file(), "no-replace failure removed the archive", seed)
        require(
            archive_path.read_bytes() == original_archive,
            "no-replace failure changed archive",
            seed,
        )
        require(
            sorted(path.name for path in output_root.iterdir()) == original_names,
            "no-replace failure left temporary output artifacts",
            seed,
        )

        duplicate_output = root / "duplicate-output"
        duplicate_output.mkdir()
        duplicate_command = [
            str(zip_executable),
            str(duplicate_output),
            "duplicate.zip",
            "input.bin",
            "input.bin",
        ]
        expect_failure(duplicate_command, source_root, seed, "duplicate entry invocation")
        require_empty_directory(duplicate_output, seed, "duplicate entry failure")

        conflicting_entry_sets = (
            ("case-fold duplicate", ("Case/", "case/")),
            ("file-directory collision", ("node", "node/")),
            ("file-parent collision", ("node", "node/child/")),
        )
        for index, (label, entries) in enumerate(conflicting_entry_sets):
            collision_output = root / f"collision-output-{index}"
            collision_output.mkdir()
            collision_command = [
                str(zip_executable),
                str(collision_output),
                f"collision-{index}.zip",
                *entries,
            ]
            expect_failure(collision_command, source_root, seed, label)
            require_empty_directory(collision_output, seed, label)

        unsafe_entries = (
            "../outside.bin",
            str(outside_file.resolve()),
            "./input.bin",
            "nested/../input.bin",
            "nested//value.bin",
            "back\\slash.bin",
            "colon:name.bin",
            "../outside-directory/",
        )
        for index, unsafe_entry in enumerate(unsafe_entries):
            unsafe_output = root / f"unsafe-output-{index}"
            unsafe_output.mkdir()
            unsafe_command = [
                str(zip_executable),
                str(unsafe_output),
                f"unsafe-{index}.zip",
                unsafe_entry,
            ]
            expect_failure(
                unsafe_command,
                source_root,
                seed,
                f"unsafe entry {unsafe_entry!r}",
            )
            require_empty_directory(unsafe_output, seed, f"unsafe entry {unsafe_entry!r}")

        unsafe_archive_output = root / "unsafe-archive-output"
        unsafe_archive_output.mkdir()
        escaped_archive = root / "escaped.zip"
        unsafe_archive_command = [
            str(zip_executable),
            str(unsafe_archive_output),
            "../escaped.zip",
            "input.bin",
        ]
        expect_failure(unsafe_archive_command, source_root, seed, "unsafe archive name")
        require(not escaped_archive.exists(), "unsafe archive name escaped output capability", seed)
        require_empty_directory(unsafe_archive_output, seed, "unsafe archive name")

        absolute_archive_output = root / "absolute-archive-output"
        absolute_archive_output.mkdir()
        absolute_archive = root / "absolute.zip"
        absolute_archive_command = [
            str(zip_executable),
            str(absolute_archive_output),
            str(absolute_archive),
            "input.bin",
        ]
        expect_failure(absolute_archive_command, source_root, seed, "absolute archive name")
        require(
            not absolute_archive.exists(),
            "absolute archive name escaped output capability",
            seed,
        )
        require_empty_directory(absolute_archive_output, seed, "absolute archive name")


def validate_executable(path: Path, option_name: str) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_file():
        raise HarnessFailure(f"{option_name} is not a file: {resolved}")
    if not os.access(resolved, os.X_OK):
        raise HarnessFailure(f"{option_name} is not executable: {resolved}")
    return resolved


def replay_command(arguments: argparse.Namespace, seed: int) -> str:
    command = [
        sys.executable,
        str(Path(__file__).resolve()),
        "--zip-executable",
        str(arguments.zip_executable),
        "--unzip-executable",
        str(arguments.unzip_executable),
        "--seed",
        hex(seed),
    ]
    return shlex.join(command)


def main() -> int:
    arguments = parse_arguments()
    try:
        zip_executable = validate_executable(arguments.zip_executable, "--zip-executable")
        unzip_executable = validate_executable(arguments.unzip_executable, "--unzip-executable")
        seeds = tuple(dict.fromkeys(arguments.seed or DEFAULT_SEEDS))
        for seed in seeds:
            run_corpus(zip_executable, unzip_executable, seed)
        run_safety_contracts(zip_executable, unzip_executable)
    except HarnessFailure as error:
        print(f"zip round-trip failed: {error}", file=sys.stderr)
        if error.seed is not None:
            print(f"replay seed: {error.seed:#018x}", file=sys.stderr)
            print(f"replay command: {replay_command(arguments, error.seed)}", file=sys.stderr)
        return 1

    print(
        f"zip round-trip valid: {len(seeds)} deterministic corpora plus safety contracts"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
