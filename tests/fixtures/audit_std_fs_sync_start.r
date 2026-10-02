module audit.std_fs_sync_start;

void start_close_directory(std.fs::directory a0, o<std.time::instant> a1) throws std.async::start_error { task<void throws std.fs::fs_error> operation = std.fs::close_directory(move a0, a1); std.async::cancel(move operation); }

void start_close_file(std.fs::file a0, o<std.time::instant> a1) throws std.async::start_error { task<void throws std.fs::fs_error> operation = std.fs::close_file(move a0, a1); std.async::cancel(move operation); }

void start_create_directory(const std.fs::path* a0, bool a1, o<std.time::instant> a2) throws std.async::start_error { task<void throws std.fs::fs_error> operation = std.fs::create_directory(a0, a1, a2); std.async::cancel(move operation); }

void start_flush(const std.fs::file* a0, o<std.time::instant> a1) throws std.async::start_error { task<void throws std.fs::fs_error> operation = std.fs::flush(a0, a1); std.async::cancel(move operation); }

void start_iterate(const std.fs::directory* a0, o<std.time::instant> a1) throws std.async::start_error { task<std.fs::directory_iter throws std.fs::fs_error> operation = std.fs::iterate(a0, a1); std.async::cancel(move operation); }

void start_lock(const std.fs::file* a0, std.fs::lock_kind a1, u64 a2, u64 a3, o<std.time::instant> a4) throws std.async::start_error { task<void throws std.fs::fs_error> operation = std.fs::lock(a0, a1, a2, a3, a4); std.async::cancel(move operation); }

void start_metadata(const std.fs::path* a0, o<std.time::instant> a1) throws std.async::start_error { task<std.fs::metadata throws std.fs::fs_error> operation = std.fs::metadata(a0, a1); std.async::cancel(move operation); }

void start_metadata_beneath(const std.fs::directory* a0, const std.fs::path* a1, o<std.time::instant> a2) throws std.async::start_error { task<std.fs::metadata throws std.fs::fs_error> operation = std.fs::metadata_beneath(a0, a1, a2); std.async::cancel(move operation); }

void start_next(std.fs::directory_iter a0, o<std.time::instant> a1) throws std.async::start_error { task<std.fs::directory_next_result> operation = std.fs::next(move a0, a1); std.async::cancel(move operation); }

void start_open_directory_beneath(const std.fs::directory* a0, const std.fs::path* a1, o<std.time::instant> a2) throws std.async::start_error { task<std.fs::directory throws std.fs::fs_error> operation = std.fs::open_directory_beneath(a0, a1, a2); std.async::cancel(move operation); }

void start_open_file_beneath(const std.fs::directory* a0, const std.fs::path* a1, std.fs::open_file_options a2, o<std.time::instant> a3) throws std.async::start_error { task<std.fs::file throws std.fs::fs_error> operation = std.fs::open_file_beneath(a0, a1, a2, a3); std.async::cancel(move operation); }

void start_read(const std.fs::file* a0, bytes a1, o<std.time::instant> a2) throws std.async::start_error { task<std.io::read_result> operation = std.fs::read(a0, move a1, a2); std.async::cancel(move operation); }

void start_read_at(const std.fs::file* a0, u64 a1, bytes a2, o<std.time::instant> a3) throws std.async::start_error { task<std.io::read_result> operation = std.fs::read_at(a0, a1, move a2, a3); std.async::cancel(move operation); }

void start_read_file_beneath(const std.fs::directory* a0, const std.fs::path* a1, usize a2, o<std.time::instant> a3) throws std.async::start_error { task<bytes throws std.fs::fs_error> operation = std.fs::read_file_beneath(a0, a1, a2, a3); std.async::cancel(move operation); }

void start_remove_directory_beneath(const std.fs::directory* a0, const std.fs::path* a1, o<std.time::instant> a2) throws std.async::start_error { task<void throws std.fs::fs_error> operation = std.fs::remove_directory_beneath(a0, a1, a2); std.async::cancel(move operation); }

void start_remove_file_beneath(const std.fs::directory* a0, const std.fs::path* a1, o<std.time::instant> a2) throws std.async::start_error { task<void throws std.fs::fs_error> operation = std.fs::remove_file_beneath(a0, a1, a2); std.async::cancel(move operation); }

void start_rename_beneath(const std.fs::directory* a0, const std.fs::path* a1, const std.fs::directory* a2, const std.fs::path* a3, o<std.time::instant> a4) throws std.async::start_error { task<void throws std.fs::fs_error> operation = std.fs::rename_beneath(a0, a1, a2, a3, a4); std.async::cancel(move operation); }

void start_seek(const std.fs::file* a0, std.fs::seek_origin a1, i64 a2, o<std.time::instant> a3) throws std.async::start_error { task<u64 throws std.fs::fs_error> operation = std.fs::seek(a0, a1, a2, a3); std.async::cancel(move operation); }

void start_sync(const std.fs::file* a0, std.fs::sync_level a1, o<std.time::instant> a2) throws std.async::start_error { task<void throws std.fs::fs_error> operation = std.fs::sync(a0, a1, a2); std.async::cancel(move operation); }

void start_try_lock(const std.fs::file* a0, std.fs::lock_kind a1, u64 a2, u64 a3, o<std.time::instant> a4) throws std.async::start_error { task<bool throws std.fs::fs_error> operation = std.fs::try_lock(a0, a1, a2, a3, a4); std.async::cancel(move operation); }

void start_unlock(const std.fs::file* a0, u64 a1, u64 a2, o<std.time::instant> a3) throws std.async::start_error { task<void throws std.fs::fs_error> operation = std.fs::unlock(a0, a1, a2, a3); std.async::cancel(move operation); }

void start_write(const std.fs::file* a0, bytes a1, o<std.time::instant> a2) throws std.async::start_error { task<std.io::write_result> operation = std.fs::write(a0, move a1, a2); std.async::cancel(move operation); }

void start_write_all(const std.fs::file* a0, bytes a1, o<std.time::instant> a2) throws std.async::start_error { task<std.io::write_all_result> operation = std.fs::write_all(a0, move a1, a2); std.async::cancel(move operation); }

void start_write_all_at(const std.fs::file* a0, u64 a1, bytes a2, o<std.time::instant> a3) throws std.async::start_error { task<std.io::write_all_result> operation = std.fs::write_all_at(a0, a1, move a2, a3); std.async::cancel(move operation); }

void start_write_file_atomic_no_replace(const std.fs::path* a0, bytes a1, o<std.time::instant> a2) throws std.async::start_error { task<std.fs::write_file_result> operation = std.fs::write_file_atomic_no_replace(a0, move a1, a2); std.async::cancel(move operation); }

i32 main() { return 0; }
