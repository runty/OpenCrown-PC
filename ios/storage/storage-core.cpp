#include "storage-core.h"
#include "formats/utf_table.h"
#include <CommonCrypto/CommonDigest.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

namespace dcp::storage {
namespace fs = std::filesystem;
namespace {
constexpr uint64_t reserve_bytes = 64ull * 1024 * 1024;
constexpr uint64_t max_save_bytes = 2ull * 1024 * 1024 * 1024;
constexpr uint64_t max_save_files = 8192;
struct Fd {
  int value;
  explicit Fd(int n = -1) : value(n) {}
  ~Fd() {
    if (value >= 0)
      ::close(value);
  }
  Fd(const Fd &) = delete;
  Fd(Fd &&v) noexcept : value(v.value) { v.value = -1; }
};
[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error(message);
}
void check(bool condition, const std::string &message) {
  if (!condition)
    fail(message);
}
void cancelled(const std::atomic<bool> &flag) {
  if (flag.load())
    fail("Import cancelled");
}
bool present(const fs::path &p) {
  std::error_code ec;
  const auto status = fs::symlink_status(p, ec);
  return !ec && status.type() != fs::file_type::not_found;
}
void directory(const fs::path &p) {
  std::error_code ec;
  fs::create_directories(p, ec);
  check(!ec && fs::is_directory(fs::symlink_status(p)),
        "Expected an ordinary directory: " + p.filename().string());
}
void erase_tree(const fs::path &p) {
  std::error_code ec;
  fs::remove_all(p, ec);
  check(!ec, "Could not clean transaction directory");
}
void sync_directory(const fs::path &p) {
  Fd fd(::open(p.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  check(fd.value >= 0, "Could not open transaction parent");
  if (::fsync(fd.value) && errno != EINVAL && errno != ENOTSUP)
    fail("Could not synchronize transaction directory");
}
std::string unique_id() {
  unsigned char bytes[16];
  arc4random_buf(bytes, sizeof bytes);
  std::ostringstream text;
  for (auto b : bytes)
    text << std::hex << std::setw(2) << std::setfill('0') << unsigned(b);
  return text.str();
}
std::string read_small(const fs::path &p, size_t limit = 4 * 1024 * 1024) {
  Fd fd(::open(p.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
  struct stat st{};
  check(fd.value >= 0 && !fstat(fd.value, &st) && S_ISREG(st.st_mode) &&
            st.st_size >= 0 && uint64_t(st.st_size) <= limit,
        "Missing or invalid transaction metadata");
  std::string out(size_t(st.st_size), '\0');
  size_t offset = 0;
  while (offset < out.size()) {
    const auto n = ::read(fd.value, out.data() + offset, out.size() - offset);
    if (n < 0 && errno == EINTR)
      continue;
    check(n > 0, "Could not read transaction metadata");
    offset += size_t(n);
  }
  return out;
}
void write_small(const fs::path &p, const std::string &text) {
  Fd fd(::open(p.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
               0600));
  check(fd.value >= 0, "Could not create transaction metadata");
  size_t offset = 0;
  while (offset < text.size()) {
    auto n = ::write(fd.value, text.data() + offset, text.size() - offset);
    if (n < 0 && errno == EINTR)
      continue;
    check(n > 0, "Could not write transaction metadata");
    offset += size_t(n);
  }
  check(!::fsync(fd.value), "Could not synchronize transaction metadata");
}
std::vector<std::string> components(const std::string &relative) {
  check(!relative.empty() && relative[0] != '/', "Invalid relative filename");
  std::vector<std::string> result;
  size_t start = 0;
  while (start < relative.size()) {
    size_t end = relative.find('/', start);
    std::string c =
        relative.substr(start, end == std::string::npos ? end : end - start);
    check(!c.empty() && c != "." && c != ".." &&
              c.find('\0') == std::string::npos,
          "Invalid relative filename");
    result.push_back(c);
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return result;
}
std::string case_component(int parent, const std::string &wanted) {
  struct stat st{};
  if (!::fstatat(parent, wanted.c_str(), &st, AT_SYMLINK_NOFOLLOW))
    return wanted;
  Fd duplicate(::openat(parent, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  check(duplicate.value >= 0, "Could not enumerate source directory");
  DIR *dir = ::fdopendir(duplicate.value);
  check(dir, "Could not enumerate source directory");
  duplicate.value = -1;
  std::string match;
  while (auto *item = ::readdir(dir)) {
    if (!::strcasecmp(item->d_name, wanted.c_str())) {
      if (!match.empty()) {
        ::closedir(dir);
        fail("Ambiguous filename casing");
      }
      match = item->d_name;
    }
  }
  ::closedir(dir);
  return match.empty() ? wanted : match;
}
Fd source_file(const fs::path &root, const std::string &relative,
               bool case_fallback = false, bool missing_allowed = false) {
  Fd parent(
      ::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  check(parent.value >= 0, "Could not open source directory");
  auto names = components(relative);
  for (size_t i = 0; i < names.size(); ++i) {
    auto name =
        case_fallback ? case_component(parent.value, names[i]) : names[i];
    int flags = O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC;
    if (i + 1 < names.size())
      flags |= O_DIRECTORY;
    int next = ::openat(parent.value, name.c_str(), flags);
    if (next < 0 && errno == ENOENT && missing_allowed)
      return Fd{};
    check(next >= 0, "Could not open required file: " + relative);
    ::close(parent.value);
    parent.value = next;
  }
  struct stat st{};
  check(!fstat(parent.value, &st) && S_ISREG(st.st_mode),
        "Source is not an ordinary file: " + relative);
  return parent;
}
uint64_t size_of(int fd) {
  struct stat st{};
  check(!::fstat(fd, &st) && st.st_size >= 0, "Could not determine file size");
  return uint64_t(st.st_size);
}
std::string digest(int fd) {
  check(::lseek(fd, 0, SEEK_SET) == 0, "Could not rewind file");
  CC_SHA256_CTX context;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  CC_SHA256_Init(&context);
  std::vector<uint8_t> buffer(1024 * 1024);
  for (;;) {
    const auto n = ::read(fd, buffer.data(), buffer.size());
    if (n < 0 && errno == EINTR)
      continue;
    check(n >= 0, "Could not hash file");
    if (!n)
      break;
    CC_SHA256_Update(&context, buffer.data(), CC_LONG(n));
  }
  uint8_t bytes[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256_Final(bytes, &context);
#pragma clang diagnostic pop
  std::ostringstream out;
  for (auto b : bytes)
    out << std::hex << std::setw(2) << std::setfill('0') << unsigned(b);
  return out.str();
}
std::vector<uint8_t> packet(int fd, uint64_t size, uint64_t offset,
                            const char *magic) {
  uint8_t h[16];
  check(offset <= size && size - offset >= sizeof h &&
            offset <= uint64_t(INT64_MAX),
        "Incomplete CPK packet");
  check(::pread(fd, h, sizeof h, off_t(offset)) == sizeof h &&
            !std::memcmp(h, magic, 4),
        "Invalid CPK packet");
  const uint32_t n = uint32_t(h[8]) | uint32_t(h[9]) << 8 |
                     uint32_t(h[10]) << 16 | uint32_t(h[11]) << 24;
  check(n <= 64 * 1024 * 1024 && n <= size - offset - 16,
        "Incomplete or oversized CPK table");
  std::vector<uint8_t> data(n);
  check(::pread(fd, data.data(), n, off_t(offset + 16)) == n,
        "Could not read CPK table");
  return data;
}
void validate_cpk(int fd) {
  const auto size = size_of(fd);
  auto bytes = packet(fd, size, 0, "CPK ");
  formats::UtfTable header(bytes);
  const auto toc = header.u64(0, "TocOffset"),
             content = header.u64(0, "ContentOffset");
  check(header.name() == "CpkHeader" && toc, "Invalid CPK header table");
  bytes = packet(fd, size, toc, "TOC ");
  formats::UtfTable table(bytes);
  check(table.find_column("FileOffset") && table.find_column("FileSize") &&
            table.rows() > 0,
        "CPK has no readable file inventory");
  const uint64_t base = content ? std::min(toc, content) : toc;
  for (uint32_t row = 0; row < table.rows(); ++row) {
    const auto offset = table.u64(row, "FileOffset"),
               length = table.u64(row, "FileSize");
    check(base <= size && offset <= size - base &&
              length <= size - base - offset,
          "CPK inventory extends beyond the archive");
  }
}
void validate_manifest(const fs::path &root,
                       const std::vector<FileSpec> &manifest) {
  for (auto &item : manifest) {
    auto file = source_file(root, item.path, false, item.optional);
    if (file.value < 0)
      continue;
    check(item.optional && item.size == 0
              ? size_of(file.value) <= 16 * 1024 * 1024
              : size_of(file.value) == item.size,
          "Unsupported or incomplete file: " + item.path);
    if (!item.sha256.empty())
      check(digest(file.value) == item.sha256,
            "File checksum mismatch: " + item.path);
    if (item.cpk)
      validate_cpk(file.value);
  }
}
void available_space(const fs::path &root, uint64_t bytes) {
  struct statfs status{};
  check(!::statfs(root.c_str(), &status),
        "Could not determine available storage");
  const uint64_t available = uint64_t(status.f_bavail) * status.f_bsize;
  check(bytes <= UINT64_MAX - reserve_bytes &&
            available >= bytes + reserve_bytes,
        "Not enough local storage for a complete staged copy");
}
void copy_file(int source, const fs::path &target,
               const std::atomic<bool> &cancel, uint64_t &copied,
               uint64_t total, const std::string &name,
               const ProgressCallback &progress) {
  directory(target.parent_path());
  Fd output(::open(target.c_str(),
                   O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
  check(output.value >= 0 && !::lseek(source, 0, SEEK_SET),
        "Could not create staged file");
  std::vector<uint8_t> buffer(1024 * 1024);
  for (;;) {
    cancelled(cancel);
    auto n = ::read(source, buffer.data(), buffer.size());
    if (n < 0 && errno == EINTR)
      continue;
    check(n >= 0, "Could not read source data");
    if (!n)
      break;
    size_t offset = 0;
    while (offset < size_t(n)) {
      auto wrote =
          ::write(output.value, buffer.data() + offset, size_t(n) - offset);
      if (wrote < 0 && errno == EINTR)
        continue;
      check(wrote > 0, "Could not write staged data");
      offset += size_t(wrote);
    }
    check(copied <= UINT64_MAX - uint64_t(n), "Copy size overflow");
    copied += uint64_t(n);
    if (progress)
      progress({copied, total, name});
  }
  check(!::fsync(output.value), "Could not synchronize staged data");
  sync_directory(target.parent_path());
}
struct Transaction {
  fs::path current, stage, previous, journal;
  explicit Transaction(fs::path path) : current(std::move(path)) {
    check(!current.filename().empty() && current.filename() != "." &&
              current.filename() != "..",
          "Invalid destination");
    const auto stem = "." + current.filename().string();
    stage = current.parent_path() / (stem + ".staging");
    previous = current.parent_path() / (stem + ".previous");
    journal = current.parent_path() / (stem + ".transaction");
  }
};
constexpr const char *generation_file = ".opencrown-generation";
void recover(Transaction &t) {
  directory(t.current.parent_path());
  if (!present(t.journal)) {
    if (present(t.stage))
      erase_tree(t.stage);
    return;
  }
  const auto id = read_small(t.journal, 128);
  check(id.size() == 32 &&
            id.find_first_not_of("0123456789abcdef") == std::string::npos,
        "Invalid storage transaction journal");
  bool promoted = false;
  if (present(t.current / generation_file))
    promoted = read_small(t.current / generation_file, 128) == id;
  if (promoted && present(t.stage)) {
    erase_tree(t.previous);
    fs::rename(t.stage, t.previous);
  } else if (present(t.stage))
    erase_tree(t.stage);
  erase_tree(t.journal);
  sync_directory(t.current.parent_path());
}
void promote(Transaction &t, const std::string &id) {
  write_small(t.stage / generation_file, id);
  sync_directory(t.stage);
  write_small(t.journal, id);
  sync_directory(t.current.parent_path());
  if (present(t.current)) {
    check(fs::is_directory(fs::symlink_status(t.current)),
          "Destination is not an ordinary directory");
    check(!::renameatx_np(AT_FDCWD, t.stage.c_str(), AT_FDCWD,
                          t.current.c_str(), RENAME_SWAP),
          "Could not atomically promote validated data");
  } else {
    check(!::rename(t.stage.c_str(), t.current.c_str()),
          "Could not promote validated data");
  }
  sync_directory(t.current.parent_path());
  recover(t);
}
std::vector<std::string> inventory(const fs::path &root) {
  check(fs::is_directory(fs::symlink_status(root)),
        "Save root is not an ordinary directory");
  std::vector<std::string> files;
  uint64_t total = 0;
  for (const auto &entry : fs::recursive_directory_iterator(root)) {
    const auto status = entry.symlink_status();
    check(fs::is_directory(status) || fs::is_regular_file(status),
          "Save tree contains a symlink or special file");
    if (!fs::is_regular_file(status))
      continue;
    const auto relative =
        entry.path().lexically_relative(root).generic_string();
    if (relative == generation_file || relative == ".opencrown-checkpoint")
      continue;
    auto file = source_file(root, relative);
    const auto size = size_of(file.value);
    check(size <= max_save_bytes - total && files.size() < max_save_files,
          "Save checkpoint exceeds safety bounds");
    total += size;
    files.push_back(relative);
  }
  std::sort(files.begin(), files.end());
  return files;
}
std::string hex_path(const std::string &name) {
  std::ostringstream out;
  for (unsigned char c : name)
    out << std::hex << std::setw(2) << std::setfill('0') << unsigned(c);
  return out.str();
}
std::string unhex_path(const std::string &text) {
  check(!text.empty() && !(text.size() % 2) &&
            text.find_first_not_of("0123456789abcdef") == std::string::npos,
        "Invalid checkpoint path encoding");
  std::string out;
  for (size_t i = 0; i < text.size(); i += 2)
    out += char(std::stoul(text.substr(i, 2), nullptr, 16));
  components(out);
  return out;
}
std::vector<FileSpec> checkpoint_manifest(const fs::path &root) {
  std::istringstream stream(read_small(root / ".opencrown-checkpoint"));
  std::string magic;
  std::getline(stream, magic);
  check(magic == "DCP-SNAPSHOT-1", "Invalid save checkpoint version");
  std::vector<FileSpec> result;
  std::string line;
  uint64_t total = 0;
  while (std::getline(stream, line)) {
    std::istringstream row(line);
    std::string hash, encoded, extra;
    uint64_t size;
    check(bool(row >> hash >> size >> encoded) && !(row >> extra),
          "Truncated save checkpoint inventory");
    check(hash.size() == 64 &&
              hash.find_first_not_of("0123456789abcdef") == std::string::npos &&
              size <= max_save_bytes - total && result.size() < max_save_files,
          "Invalid save checkpoint inventory");
    total += size;
    auto name = unhex_path(encoded);
    check(name != generation_file && name != ".opencrown-checkpoint" &&
              (result.empty() || result.back().path < name),
          "Duplicate or unordered save checkpoint path");
    result.push_back({name, size, hash, false});
  }
  check(stream.eof(), "Could not read save checkpoint inventory");
  check(inventory(root).size() == result.size(),
        "Save checkpoint contains unlisted files");
  return result;
}
bool contains(const fs::path &ancestor, const fs::path &path) {
  auto a = fs::weakly_canonical(ancestor), p = fs::weakly_canonical(path);
  auto ai = a.begin(), pi = p.begin();
  for (; ai != a.end(); ++ai, ++pi)
    if (pi == p.end() || *ai != *pi)
      return false;
  return true;
}
void disjoint(const fs::path &a, const fs::path &b) {
  check(!contains(a, b) && !contains(b, a),
        "Save and checkpoint directories must be separate");
}
uint64_t tree_bytes(const fs::path &root,
                    const std::vector<std::string> &files) {
  uint64_t total = 0;
  for (const auto &name : files) {
    auto file = source_file(root, name);
    auto size = size_of(file.value);
    check(size <= max_save_bytes - total,
          "Save checkpoint exceeds safety bounds");
    total += size;
  }
  return total;
}
void copy_save_tree(const fs::path &source, const fs::path &target,
                    const std::vector<std::string> &files) {
  uint64_t copied = 0;
  const std::atomic<bool> cancel{false};
  for (const auto &entry : fs::recursive_directory_iterator(source)) {
    auto status = entry.symlink_status();
    check(fs::is_directory(status) || fs::is_regular_file(status),
          "Save tree contains a symlink or special file");
    if (fs::is_directory(status))
      directory(target / entry.path().lexically_relative(source));
  }
  for (const auto &name : files) {
    auto input = source_file(source, name);
    copy_file(input.value, target / name, cancel, copied, 0, name, {});
  }
}
} // namespace

std::vector<FileSpec> supported_game_files() {
  return {{"eboot.bin", 22283899,
           "f9bff150c4c28507096e7e143a07492063ed89bf05ac6cb995a55a0946e3508a"},
          {"sce_module/libc.prx", 1069730,
           "1e952f9486c966e8e3058c01902c8d9c7382acfd3030362db19dfd74681f925c"},
          {"DRACRO.CPK", 5264082928, "", true},
          {"DRACRO_US.CPK", 871756328, "", true},
          {"sce_discmap.plt", 0, "", false, true}};
}
void recover_import(const fs::path &destination) {
  Transaction t(destination);
  recover(t);
}
void validate_game(const fs::path &directory) {
  validate_manifest(directory, supported_game_files());
}
void import_game(const fs::path &source, const fs::path &destination,
                 const std::atomic<bool> &cancel, ProgressCallback progress) {
  detail::import_manifest(source, destination, supported_game_files(), cancel,
                          std::move(progress));
}
void detail::import_manifest(const fs::path &source,
                             const fs::path &destination,
                             const std::vector<FileSpec> &manifest,
                             const std::atomic<bool> &cancel,
                             ProgressCallback progress) {
  Transaction t(destination);
  recover(t);
  uint64_t total = 0;
  for (const auto &item : manifest) {
    components(item.path);
    auto input = source_file(source, item.path, true, item.optional);
    if (input.value < 0)
      continue;
    const auto size = size_of(input.value);
    check((item.optional && item.size == 0 ? size <= 16 * 1024 * 1024
                                           : size == item.size) &&
              size <= UINT64_MAX - total,
          "Unsupported or incomplete file: " + item.path);
    total += size;
  }
  available_space(t.current.parent_path(), total);
  cancelled(cancel);
  directory(t.stage);
  try {
    uint64_t copied = 0;
    for (const auto &item : manifest) {
      auto input = source_file(source, item.path, true, item.optional);
      if (input.value < 0)
        continue;
      copy_file(input.value, t.stage / item.path, cancel, copied, total,
                item.path, progress);
    }
    cancelled(cancel);
    validate_manifest(t.stage, manifest);
    cancelled(cancel);
    promote(t, unique_id());
  } catch (...) {
    if (present(t.journal))
      recover(t);
    else
      erase_tree(t.stage);
    throw;
  }
}
std::string checkpoint_saves(const fs::path &save_root,
                             const fs::path &checkpoint_root) {
  disjoint(save_root, checkpoint_root);
  directory(checkpoint_root);
  recover_checkpoints(checkpoint_root);
  const auto files = inventory(save_root);
  available_space(checkpoint_root, tree_bytes(save_root, files));
  const std::string id = unique_id();
  const auto stage = checkpoint_root / ("." + id + ".pending");
  directory(stage);
  try {
    copy_save_tree(save_root, stage, files);
    std::ostringstream manifest;
    manifest << "DCP-SNAPSHOT-1\n";
    for (const auto &name : files) {
      auto file = source_file(stage, name);
      manifest << digest(file.value) << ' ' << size_of(file.value) << ' '
               << hex_path(name) << '\n';
    }
    write_small(stage / ".opencrown-checkpoint", manifest.str());
    validate_checkpoint(stage);
    sync_directory(stage);
    fs::rename(stage, checkpoint_root / id);
    sync_directory(checkpoint_root);
    return id;
  } catch (...) {
    erase_tree(stage);
    throw;
  }
}
void recover_checkpoints(const fs::path &checkpoint_root) {
  if (!present(checkpoint_root))
    return;
  check(fs::is_directory(fs::symlink_status(checkpoint_root)),
        "Checkpoint root is not an ordinary directory");
  for (const auto &entry : fs::directory_iterator(checkpoint_root)) {
    auto name = entry.path().filename().string();
    if (name.size() == 41 && name[0] == '.' && name.substr(33) == ".pending" &&
        name.substr(1, 32).find_first_not_of("0123456789abcdef") ==
            std::string::npos)
      erase_tree(entry.path());
  }
}
void validate_checkpoint(const fs::path &checkpoint) {
  validate_manifest(checkpoint, checkpoint_manifest(checkpoint));
}
void restore_checkpoint(const fs::path &checkpoint, const fs::path &save_root) {
  detail::restore_checkpoint_generation(checkpoint, save_root, unique_id());
}
void detail::restore_checkpoint_generation(const fs::path &checkpoint,
                                           const fs::path &save_root,
                                           const std::string &generation) {
  check(generation.size() == 32 && generation.find_first_not_of(
                                       "0123456789abcdef") == std::string::npos,
        "Invalid restore generation identifier");
  disjoint(checkpoint, save_root);
  validate_checkpoint(checkpoint);
  const auto files = inventory(checkpoint);
  directory(save_root.parent_path());
  available_space(save_root.parent_path(), tree_bytes(checkpoint, files));
  Transaction t(save_root);
  recover(t);
  directory(t.stage);
  try {
    copy_save_tree(checkpoint, t.stage, files);
    validate_manifest(t.stage, checkpoint_manifest(checkpoint));
    promote(t, generation);
  } catch (...) {
    if (present(t.journal))
      recover(t);
    else
      erase_tree(t.stage);
    throw;
  }
}
void recover_save_restore(const fs::path &save_root) {
  recover_import(save_root);
}
std::string stage_save_transfer(const fs::path &source,
                                const fs::path &staging_root) {
  check(!present(source.parent_path() /
                 ("." + source.filename().string() + ".write-session")),
        "Recover the interrupted save before exporting it");
  // Reject an empty/wrong folder before creating a destination. This is a
  // filesystem/content-inventory check, not a parser of the game's save format.
  auto slot = source_file(source, "SAVEDATA00/SAVE0.DAT");
  check(size_of(slot.value) > 0,
        "Choose a save folder containing SAVEDATA00/SAVE0.DAT");
  const bool supplied = present(source / ".opencrown-checkpoint");
  std::string manifest;
  if (supplied) {
    validate_checkpoint(source);
    manifest = read_small(source / ".opencrown-checkpoint");
  }
  auto id = checkpoint_saves(source, staging_root);
  try {
    // Compare parsed inventories, allowing harmless whitespace differences in
    // imported metadata. A source changed during copying must not be rehashed
    // into an apparently valid replacement of its advertised snapshot.
    if (supplied) {
      auto expected = checkpoint_manifest(source);
      auto copied = checkpoint_manifest(staging_root / id);
      check(read_small(source / ".opencrown-checkpoint") == manifest,
            "Save inventory changed during transfer");
      check(expected.size() == copied.size(), "Save changed during transfer");
      for (size_t i = 0; i < expected.size(); ++i)
        check(expected[i].path == copied[i].path &&
                  expected[i].size == copied[i].size &&
                  expected[i].sha256 == copied[i].sha256,
              "Save changed during transfer");
    }
    return id;
  } catch (...) {
    erase_tree(staging_root / id);
    throw;
  }
}
std::string import_save_folder(const fs::path &source,
                               const fs::path &save_root,
                               const fs::path &checkpoint_root) {
  disjoint(source, save_root);
  disjoint(save_root, checkpoint_root);
  check(!present(save_root.parent_path() /
                 ("." + save_root.filename().string() + ".write-session")),
        "Recover the interrupted local save before importing");
  const auto incoming =
      checkpoint_root / stage_save_transfer(source, checkpoint_root);
  // The incoming copy remains as a validated recovery point even if later
  // backup or promotion fails. Never prune backups here.
  std::string backup;
  recover_save_restore(save_root);
  if (present(save_root))
    backup = checkpoint_saves(save_root, checkpoint_root);
  restore_checkpoint(incoming, save_root);
  return backup;
}
} // namespace dcp::storage
