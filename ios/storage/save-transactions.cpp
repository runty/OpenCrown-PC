#include "save-transactions.h"
#include "storage-core.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace dcp::storage {
namespace fs = std::filesystem;
namespace {
constexpr const char *magic = "DCP-SAVE-SESSION-1";
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
struct Fd {
  int value;
  explicit Fd(int n) : value(n) {}
  ~Fd() {
    if (value >= 0)
      ::close(value);
  }
};
bool present(const fs::path &path) {
  std::error_code ec;
  auto st = fs::symlink_status(path, ec);
  return !ec && st.type() != fs::file_type::not_found;
}
bool id_valid(const std::string &id) {
  return id.size() == 32 &&
         id.find_first_not_of("0123456789abcdef") == std::string::npos;
}
std::string unique_id() {
  unsigned char bytes[16];
  arc4random_buf(bytes, sizeof bytes);
  std::string result;
  for (auto b : bytes) {
    result += "0123456789abcdef"[b >> 4];
    result += "0123456789abcdef"[b & 15];
  }
  return result;
}
void sync_parent(const fs::path &path) {
  Fd fd(::open(path.parent_path().c_str(),
               O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  check(fd.value >= 0, "Could not open save transaction directory");
  if (::fsync(fd.value) && errno != EINVAL && errno != ENOTSUP)
    throw std::runtime_error(
        "Could not synchronize save transaction directory");
}
std::string read(const fs::path &path) {
  Fd fd(::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
  struct stat st{};
  check(fd.value >= 0 && !fstat(fd.value, &st) && S_ISREG(st.st_mode) &&
            st.st_size >= 0 && st.st_size <= 256,
        "Invalid save transaction journal");
  std::string data(size_t(st.st_size), '\0');
  size_t at = 0;
  while (at < data.size()) {
    auto n = ::read(fd.value, data.data() + at, data.size() - at);
    if (n < 0 && errno == EINTR)
      continue;
    check(n > 0, "Could not read save transaction journal");
    at += size_t(n);
  }
  return data;
}
struct Record {
  std::string before, recovery;
};
Record read_record(const fs::path &path) {
  std::istringstream stream(read(path));
  std::string version, extra;
  Record record;
  check(bool(std::getline(stream, version)) && version == magic &&
            bool(std::getline(stream, record.before)) &&
            id_valid(record.before) &&
            bool(std::getline(stream, record.recovery)) &&
            (record.recovery == "-" || id_valid(record.recovery)) &&
            !std::getline(stream, extra),
        "Invalid save transaction journal");
  return record;
}
void write_record(const fs::path &path, const Record &record) {
  const auto pending = fs::path(path.string() + ".pending");
  std::error_code ec;
  fs::remove(pending, ec);
  check(!ec, "Could not clean save transaction staging metadata");
  Fd fd(::open(pending.c_str(),
               O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
  check(fd.value >= 0, "Could not create save transaction journal");
  const std::string text =
      std::string(magic) + '\n' + record.before + '\n' + record.recovery + '\n';
  size_t at = 0;
  while (at < text.size()) {
    auto n = ::write(fd.value, text.data() + at, text.size() - at);
    if (n < 0 && errno == EINTR)
      continue;
    check(n > 0, "Could not write save transaction journal");
    at += size_t(n);
  }
  check(!::fsync(fd.value) && !::rename(pending.c_str(), path.c_str()),
        "Could not publish save transaction journal");
  sync_parent(path);
}
void clear_record(const fs::path &path) {
  check(!::unlink(path.c_str()), "Could not clear completed save transaction");
  sync_parent(path);
}
void cleanup_pending(const fs::path &journal) {
  std::error_code ec;
  fs::remove(fs::path(journal.string() + ".pending"), ec);
  check(!ec, "Could not clean save transaction staging metadata");
}
void prune_completed(const fs::path &root, const fs::path &journal,
                     const std::string &latest) {
  struct Candidate {
    fs::path path;
    std::string id;
    fs::file_time_type time;
  };
  std::vector<Candidate> candidates;
  std::set<std::string> protected_ids{latest};
  if (present(journal))
    protected_ids.insert(read_record(journal).before);
  for (const auto &entry : fs::directory_iterator(root)) {
    const auto name = entry.path().filename().string();
    // Never follow a symlink, interpret an unknown name, or prune an invalid
    // checkpoint. Interrupted repair candidates have their own explicit policy.
    if (!id_valid(name) || !fs::is_directory(entry.symlink_status()))
      continue;
    try {
      validate_checkpoint(entry.path());
    } catch (const std::exception &) {
      continue;
    }
    candidates.push_back({entry.path(), name, entry.last_write_time()});
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate &a, const Candidate &b) {
              return a.time != b.time ? a.time > b.time : a.id > b.id;
            });
  std::set<std::string> keep = protected_ids;
  for (const auto &entry : candidates) {
    if (keep.size() >= 10)
      break;
    keep.insert(entry.id);
  }
  bool changed = false;
  for (const auto &entry : candidates) {
    if (keep.count(entry.id))
      continue;
    // Check again at deletion time. A replaced/symlinked path is never pruned.
    if (!fs::is_directory(fs::symlink_status(entry.path)))
      continue;
    validate_checkpoint(entry.path);
    fs::remove_all(entry.path);
    changed = true;
  }
  if (changed)
    sync_parent(root / "checkpoint-inventory");
}
std::string exception_error() {
  try {
    throw;
  } catch (const std::exception &error) {
    return error.what();
  } catch (...) {
    return "Unknown save transaction failure";
  }
}
} // namespace
SaveTransactions::SaveTransactions(fs::path saves, fs::path checkpoints)
    : saves_(std::move(saves)), checkpoints_(std::move(checkpoints)),
      journal_(saves_.parent_path() /
               ("." + saves_.filename().string() + ".write-session")) {}
bool SaveTransactions::begin() noexcept {
  std::lock_guard<std::mutex> guard(lock_);
  error_.clear();
  try {
    check(
        !present(journal_),
        "A save write transaction is already active; recover before mounting");
    fs::create_directories(saves_);
    cleanup_pending(journal_);
    fs::create_directories(checkpoints_);
    struct stat live{}, backups{};
    check(!::stat(saves_.c_str(), &live) &&
              !::stat(checkpoints_.c_str(), &backups) &&
              live.st_dev == backups.st_dev,
          "Save session checkpoints must stay on the same local filesystem");
    const auto before = checkpoint_saves(saves_, checkpoints_);
    write_record(journal_, {before, "-"});
    return true;
  } catch (...) {
    error_ = exception_error();
    return false;
  }
}
bool SaveTransactions::commit() noexcept {
  std::lock_guard<std::mutex> guard(lock_);
  error_.clear();
  try {
    const auto record = read_record(journal_);
    check(record.recovery == "-", "Save recovery is pending");
    validate_checkpoint(checkpoints_ / record.before);
    // The new tree must be durably checkpointed before acknowledging the write.
    const auto latest = checkpoint_saves(saves_, checkpoints_);
    clear_record(journal_);
    retention_warning_.clear();
    try {
      prune_completed(checkpoints_, journal_, latest);
    } catch (...) {
      retention_warning_ = exception_error();
    }
    return true;
  } catch (...) {
    error_ = exception_error();
    return false;
  }
}
bool SaveTransactions::recover() noexcept {
  std::lock_guard<std::mutex> guard(lock_);
  error_.clear();
  recovered_.clear();
  try {
    recover_save_restore(saves_);
    recover_checkpoints(checkpoints_);
    cleanup_pending(journal_);
    if (!present(journal_))
      return true;
    auto record = read_record(journal_);
    validate_checkpoint(checkpoints_ / record.before);
    if (record.recovery == "-") {
      record.recovery = unique_id();
      write_record(journal_, record);
    }
    const auto generation = saves_ / ".opencrown-generation";
    bool restored = present(generation) && read(generation) == record.recovery;
    if (!restored)
      detail::restore_checkpoint_generation(checkpoints_ / record.before,
                                            saves_, record.recovery);
    const auto previous =
        saves_.parent_path() / ("." + saves_.filename().string() + ".previous");
    const auto interrupted = checkpoints_ / ("interrupted-" + record.recovery);
    if (present(previous)) {
      check(!present(interrupted),
            "An interrupted-save archive already exists");
      fs::rename(previous, interrupted);
      sync_parent(previous);
      sync_parent(interrupted);
    }
    check(present(interrupted), "The interrupted save copy was not retained");
    recovered_ = interrupted.string();
    clear_record(journal_);
    return true;
  } catch (...) {
    error_ = exception_error();
    return false;
  }
}
std::string SaveTransactions::error() const {
  std::lock_guard<std::mutex> guard(lock_);
  return error_;
}
std::string SaveTransactions::last_recovery_path() const {
  std::lock_guard<std::mutex> guard(lock_);
  return recovered_;
}
std::string SaveTransactions::retention_warning() const {
  std::lock_guard<std::mutex> guard(lock_);
  return retention_warning_;
}
bool SaveTransactions::active() const {
  std::lock_guard<std::mutex> guard(lock_);
  return present(journal_);
}
} // namespace dcp::storage
