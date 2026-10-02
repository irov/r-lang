"""Check rejected filesystem async calls without relying on backend diagnostics."""
import pathlib
import subprocess
import sys
import tempfile

compiler = sys.argv[1]
prefix = "module test.fs_extended_negative;\n"
cases = {
    "arity": ("void test(const std.fs::file* file) throws std.async::start_error { task<u64 throws std.fs::fs_error> operation = std.fs::seek(file, std.fs::seek_origin::start); std.async::cancel(move operation); }", "R-DIAG-TYPE"),
    "offset_type": ("void test(const std.fs::file* file) throws std.async::start_error { task<u64 throws std.fs::fs_error> operation = std.fs::seek(file, std.fs::seek_origin::start, true, o::none); std.async::cancel(move operation); }", "R-DIAG-TYPE"),
    "deadline_type": ("void test(const std.fs::path* path) throws std.async::start_error { task<std.fs::metadata throws std.fs::fs_error> operation = std.fs::metadata(path, false); std.async::cancel(move operation); }", "R-DIAG-TYPE"),
    "missing_move": ("void test(const std.fs::file* file, bytes buffer) throws std.async::start_error { task<std.io::write_result> operation = std.fs::write(file, buffer, o::none); std.async::cancel(move operation); }", "R-DIAG-MOVE-001"),
    "unhandled_start": ("void test(const std.fs::directory* directory) { task<std.fs::directory_iter throws std.fs::fs_error> operation = std.fs::iterate(directory, o::none); std.async::cancel(move operation); }", "R-DIAG-EFFECT"),
    "unhandled_completion": ("async void test(std.fs::file file) throws std.async::start_error { u64 result = await std.fs::seek(&file, std.fs::seek_origin::start, 0, o::none); result as void; }", "R-DIAG-EFFECT"),
    "moved_file": ("void test(std.fs::file file) throws std.async::start_error { task<void throws std.fs::fs_error> close = std.fs::close_file(move file, o::none); std.async::cancel(move close); task<std.fs::metadata throws std.fs::fs_error> info = std.fs::file_metadata(&file, o::none); std.async::cancel(move info); }", "R-DIAG-MOVE-002"),
    "result_type": ("void test(const std.fs::file* file) throws std.async::start_error { task<u32 throws std.fs::fs_error> operation = std.fs::seek(file, std.fs::seek_origin::start, 0, o::none); std.async::cancel(move operation); }", "R-DIAG-TYPE"),
    "missing_completion_effect": ("void test(const std.fs::path* path) throws std.async::start_error { task<std.fs::metadata> operation = std.fs::metadata(path, o::none); std.async::cancel(move operation); }", "R-DIAG-TYPE-001"),
}
with tempfile.TemporaryDirectory(prefix="r-fs-semantics-") as scratch:
    for name, (source, diagnostic) in cases.items():
        path = pathlib.Path(scratch) / (name + ".r")
        path.write_text(prefix + source + "\n", encoding="utf-8")
        result = subprocess.run([compiler, "--emit=hir", str(path)], capture_output=True, text=True)
        if result.returncode != 1 or diagnostic not in result.stderr:
            raise SystemExit(f"{name}: expected source diagnostic {diagnostic}; got {result.returncode}\n{result.stdout}{result.stderr}")
print(f"Validated {len(cases)} rejected filesystem calls")
