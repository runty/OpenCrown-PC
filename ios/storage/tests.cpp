#include "save-transactions.h"
#include "storage-core.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace dcp::storage;
namespace {
unsigned passed = 0;
void expect(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
  ++passed;
}
template <class F> void rejects(F callback, const char *message) {
  bool rejected = false;
  try {
    callback();
  } catch (const std::exception &) {
    rejected = true;
  }
  expect(rejected, message);
}
void write(const fs::path &path, const std::string &data) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out << data;
  if (!out)
    throw std::runtime_error("fixture write failed");
}
std::string read(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}
std::set<std::string> checkpoint_ids(const fs::path &root) {
  std::set<std::string> result;
  for (const auto &entry : fs::directory_iterator(root)) {
    const auto name = entry.path().filename().string();
    if (name.size() != 32 ||
        name.find_first_not_of("0123456789abcdef") != std::string::npos ||
        !fs::is_directory(entry.symlink_status()))
      continue;
    try {
      validate_checkpoint(entry.path());
      result.insert(name);
    } catch (const std::exception &) {
    }
  }
  return result;
}
void be16(std::string &s, uint16_t v) {
  s += char(v >> 8);
  s += char(v);
}
void be32(std::string &s, uint32_t v) {
  for (int i = 3; i >= 0; --i)
    s += char(v >> (i * 8));
}
void be64(std::string &s, uint64_t v) {
  for (int i = 7; i >= 0; --i)
    s += char(v >> (i * 8));
}
std::string table(const std::string &name, const std::string &first,
                  const std::string &second, uint64_t a, uint64_t b) {
  std::string strings = name + '\0' + first + '\0' + second + '\0';
  std::string body;
  be16(body, 0);
  be16(body, 50);
  be32(body, 50);
  be32(body, 50 + uint32_t(strings.size()));
  be32(body, 0);
  be16(body, 2);
  be16(body, 0);
  be32(body, 1);
  body += char(0x36);
  be32(body, uint32_t(name.size() + 1));
  be64(body, a);
  body += char(0x36);
  be32(body, uint32_t(name.size() + first.size() + 2));
  be64(body, b);
  body += strings;
  std::string out = "@UTF";
  be32(out, uint32_t(body.size()));
  out += body;
  return out;
}
void packet(std::string &file, size_t offset, const char *magic,
            const std::string &utf) {
  if (file.size() < offset + 16 + utf.size())
    file.resize(offset + 16 + utf.size());
  file.replace(offset, 4, magic, 4);
  uint32_t n = uint32_t(utf.size());
  for (unsigned i = 0; i < 4; ++i)
    file[offset + 8 + i] = char(n >> (i * 8));
  file.replace(offset + 16, utf.size(), utf);
}
std::string cpk(bool overflow) {
  std::string file(1024, '\0');
  packet(file, 0, "CPK ",
         table("CpkHeader", "TocOffset", "ContentOffset", 256, 512));
  packet(file, 256, "TOC ",
         table("CpkTocInfo", "FileOffset", "FileSize",
               overflow ? UINT64_MAX : 512, 8));
  return file;
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("Usage: storage-tests EXTERNAL_TEST_ROOT");
    fs::path base = fs::path(argv[1]) / ("run-" + std::to_string(getpid()));
    fs::create_directories(base);
    const auto source = base / "source", game = base / "Documents/Game";
    write(source / "file.bin", "abc");
    std::vector<FileSpec> manifest{
        {"FILE.BIN", 3,
         "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"}};
    std::atomic<bool> cancel{false};
    uint64_t progress = 0;
    detail::import_manifest(source, game, manifest, cancel,
                            [&](const Progress &v) { progress = v.copied; });
    expect(read(game / "FILE.BIN") == "abc" && progress == 3,
           "initial import or casing failed");
    write(source / "file.bin", "def");
    rejects([&] { detail::import_manifest(source, game, manifest, cancel); },
            "bad hash accepted");
    expect(read(game / "FILE.BIN") == "abc",
           "failed import changed current game");
    cancel = true;
    rejects([&] { detail::import_manifest(source, game, manifest, cancel); },
            "cancelled import accepted");
    expect(read(game / "FILE.BIN") == "abc", "cancel changed current game");
    cancel = false;
    write(source / "file.bin", "abc");
    detail::import_manifest(source, game, manifest, cancel);
    expect(read(base / "Documents/.Game.previous/FILE.BIN") == "abc",
           "previous install not retained");
    fs::remove(source / "file.bin");
    fs::create_symlink(game / "FILE.BIN", source / "file.bin");
    rejects([&] { detail::import_manifest(source, game, manifest, cancel); },
            "source symlink accepted");
    fs::remove(source / "file.bin");
    write(source / "file.bin", std::string(2 * 1024 * 1024, 'x'));
    std::vector<FileSpec> large{{"file.bin", 2 * 1024 * 1024, ""}};
    rejects(
        [&] {
          detail::import_manifest(source, game, large, cancel,
                                  [&](const Progress &) { cancel = true; });
        },
        "mid-copy cancel accepted");
    expect(read(game / "FILE.BIN") == "abc" &&
               !fs::exists(base / "Documents/.Game.staging"),
           "cancelled stage not cleaned");
    cancel = false;
    const std::string id(32, 'a');
    write(base / "Documents/.Game.staging/partial", "partial");
    write(base / "Documents/.Game.transaction", id);
    recover_import(game);
    expect(read(game / "FILE.BIN") == "abc" &&
               !fs::exists(base / "Documents/.Game.staging"),
           "pending transaction recovery failed");
    write(game / ".opencrown-generation", id);
    write(base / "Documents/.Game.staging/previous", "old");
    write(base / "Documents/.Game.transaction", id);
    recover_import(game);
    expect(read(base / "Documents/.Game.previous/previous") == "old",
           "post-exchange recovery lost previous install");
    write(source / "archive.cpk", cpk(false));
    std::vector<FileSpec> archives{{"archive.cpk", 1024, "", true}};
    detail::import_manifest(source, base / "Documents/ArchiveGame", archives,
                            cancel);
    expect(fs::file_size(base / "Documents/ArchiveGame/archive.cpk") == 1024,
           "valid synthetic CPK rejected");
    write(source / "archive.cpk", cpk(true));
    rejects(
        [&] {
          detail::import_manifest(source, base / "Documents/ArchiveGame",
                                  archives, cancel);
        },
        "overflowing CPK range accepted");
    write(source / "archive.cpk", std::string(1024, 'x'));
    rejects(
        [&] {
          detail::import_manifest(source, base / "Documents/ArchiveGame",
                                  archives, cancel);
        },
        "invalid CPK magic accepted");
    write(source / "file.bin", "abc");
    manifest.push_back({"optional.bin", 0, "", false, true});
    detail::import_manifest(source, game, manifest, cancel);
    expect(!fs::exists(game / "optional.bin"), "absent optional file failed");
    write(source / "optional.bin", "metadata");
    detail::import_manifest(source, game, manifest, cancel);
    expect(read(game / "optional.bin") == "metadata",
           "optional file not copied");
    fs::remove(source / "optional.bin");
    fs::create_symlink(source / "file.bin", source / "optional.bin");
    rejects([&] { detail::import_manifest(source, game, manifest, cancel); },
            "optional symlink accepted");
    const auto saves = base / "Documents/ProbeSaves",
               checkpoints = base / "Documents/SaveCheckpoints";
    write(saves / "SAVEDATA00/data.bin", "first complete save");
    write(saves / "SAVEDATA00/sce_sys/param.bin", "metadata");
    fs::create_directories(saves / "empty");
    const auto checkpoint = checkpoints / checkpoint_saves(saves, checkpoints);
    validate_checkpoint(checkpoint);
    expect(read(checkpoint / "SAVEDATA00/data.bin") == "first complete save" &&
               fs::is_directory(checkpoint / "empty"),
           "checkpoint copy incomplete");
    write(saves / "SAVEDATA00/data.bin", "new progress");
    restore_checkpoint(checkpoint, saves);
    expect(read(saves / "SAVEDATA00/data.bin") == "first complete save" &&
               fs::is_directory(saves / "empty"),
           "save restore failed");
    expect(read(base / "Documents/.ProbeSaves.previous/SAVEDATA00/data.bin") ==
               "new progress",
           "restore backup lost");
    write(checkpoint / "SAVEDATA00/data.bin", "tampered");
    rejects([&] { restore_checkpoint(checkpoint, saves); },
            "tampered checkpoint restored");
    expect(read(saves / "SAVEDATA00/data.bin") == "first complete save",
           "bad checkpoint changed live saves");
    const auto good = checkpoints / checkpoint_saves(saves, checkpoints);
    write(good / ".opencrown-checkpoint", "DCP-SNAPSHOT-1\nabc");
    rejects([&] { validate_checkpoint(good); },
            "truncated checkpoint inventory accepted");
    fs::create_symlink(game / "FILE.BIN", saves / "unexpected-link");
    rejects([&] { checkpoint_saves(saves, checkpoints); },
            "save symlink accepted");
    fs::remove(saves / "unexpected-link");
    fs::create_directories(base / "empty-saves");
    auto empty =
        checkpoints / checkpoint_saves(base / "empty-saves", checkpoints);
    validate_checkpoint(empty);
    expect(fs::exists(empty / ".opencrown-checkpoint"),
           "empty save checkpoint failed");
    rejects([&] { checkpoint_saves(saves, saves / "nested-checkpoints"); },
            "nested checkpoint root accepted");
    rejects([&] { restore_checkpoint(empty, checkpoints); },
            "restore destination containing checkpoint accepted");
    const auto pending =
        checkpoints / ("." + std::string(32, 'b') + ".pending");
    write(pending / "partial", "partial");
    write(checkpoints / "keep-this-file", "user data");
    recover_checkpoints(checkpoints);
    expect(!fs::exists(pending) &&
               read(checkpoints / "keep-this-file") == "user data",
           "pending checkpoint recovery deleted unrelated file");
    const auto nested = base / "nested-source";
    fs::create_directories(nested);
    fs::create_directory_symlink(source, nested / "dir");
    std::vector<FileSpec> nestedManifest{{"dir/file.bin", 3, ""}};
    rejects(
        [&] { detail::import_manifest(nested, game, nestedManifest, cancel); },
        "intermediate directory symlink accepted");
    std::vector<FileSpec> traversal{{"../source/file.bin", 3, ""}};
    rejects([&] { detail::import_manifest(source, game, traversal, cancel); },
            "traversal path accepted");
    fs::remove(source / "optional.bin");
    expect(!::mkfifo((source / "optional.bin").c_str(), 0600),
           "FIFO fixture creation failed");
    rejects([&] { detail::import_manifest(source, game, manifest, cancel); },
            "optional special file accepted");
    fs::remove(source / "optional.bin");
    write(source / "optional.bin", "");
    expect(!::truncate((source / "optional.bin").c_str(), 17 * 1024 * 1024),
           "oversized fixture creation failed");
    rejects([&] { detail::import_manifest(source, game, manifest, cancel); },
            "oversized optional metadata accepted");
    const auto sessionSaves = base / "Session/Saves",
               sessionCheckpoints = base / "Session/Checkpoints";
    write(sessionSaves / "SAVEDATA00/data.bin", "complete-before-write");
    SaveTransactions sessions(sessionSaves, sessionCheckpoints);
    expect(sessions.recover(), "initial save session recovery failed");
    expect(sessions.begin() && sessions.active(), "save write begin failed");
    rejects(
        [&] {
          if (!sessions.begin())
            throw std::runtime_error(sessions.error());
        },
        "overlapping save write begin accepted");
    write(sessionSaves / "SAVEDATA00/data.bin", "interrupted-half-write");
    expect(sessions.recover() && !sessions.active(),
           "interrupted write recovery failed");
    expect(read(sessionSaves / "SAVEDATA00/data.bin") ==
               "complete-before-write",
           "interrupted write did not roll back");
    expect(read(fs::path(sessions.last_recovery_path()) /
                "SAVEDATA00/data.bin") == "interrupted-half-write",
           "interrupted save copy not retained");
    expect(sessions.begin(), "second save write begin failed");
    write(sessionSaves / "SAVEDATA00/data.bin", "complete-new-progress");
    expect(sessions.commit() && !sessions.active(), "save write commit failed");
    expect(sessions.recover() && read(sessionSaves / "SAVEDATA00/data.bin") ==
                                     "complete-new-progress",
           "recovery rolled back a committed save");
    expect(!sessions.commit() && !sessions.error().empty(),
           "commit without active session accepted");
    expect(sessions.begin(), "corruption test begin failed");
    auto journalPath = base / "Session/.Saves.write-session";
    std::istringstream journal(read(journalPath));
    std::string version, before, phase;
    std::getline(journal, version);
    std::getline(journal, before);
    std::getline(journal, phase);
    write(sessionSaves / "SAVEDATA00/data.bin", "current-must-stay");
    write(sessionCheckpoints / before / "SAVEDATA00/data.bin",
          "damaged-checkpoint");
    expect(!sessions.recover() && sessions.active(),
           "corrupt rollback checkpoint accepted");
    expect(read(sessionSaves / "SAVEDATA00/data.bin") == "current-must-stay",
           "corrupt rollback changed current save");
    // Restore the checkpoint bytes to exercise restart after recovery's atomic
    // directory exchange, before moving its interrupted copy or clearing
    // journal.
    write(sessionCheckpoints / before / "SAVEDATA00/data.bin",
          "complete-new-progress");
    const std::string recoveryId(32, 'c');
    write(journalPath,
          "DCP-SAVE-SESSION-1\n" + before + "\n" + recoveryId + "\n");
    detail::restore_checkpoint_generation(sessionCheckpoints / before,
                                          sessionSaves, recoveryId);
    expect(sessions.recover(), "post-exchange save recovery restart failed");
    expect(read(sessionCheckpoints / ("interrupted-" + recoveryId) /
                "SAVEDATA00/data.bin") == "current-must-stay",
           "recovery restart lost interrupted copy");
    expect(read(sessionSaves / "SAVEDATA00/data.bin") ==
               "complete-new-progress",
           "recovery restart changed complete save");
    // A second restart after the interrupted tree has already been archived is
    // idempotent and must not exchange the save again.
    write(journalPath,
          "DCP-SAVE-SESSION-1\n" + before + "\n" + recoveryId + "\n");
    expect(sessions.recover(), "post-archive save recovery restart failed");
    expect(read(sessionCheckpoints / ("interrupted-" + recoveryId) /
                "SAVEDATA00/data.bin") == "current-must-stay",
           "idempotent recovery changed interrupted archive");
    const auto boundedSaves = base / "Retention/Saves",
               boundedCheckpoints = base / "Retention/Checkpoints";
    write(boundedSaves / "SAVEDATA00/data.bin", "initial-retained-progress");
    fs::create_directories(boundedCheckpoints);
    write(boundedCheckpoints / "keep-this-name", "user data");
    write(boundedCheckpoints / ("interrupted-" + std::string(32, 'e')) /
              "partial",
          "repair candidate");
    write(boundedCheckpoints / std::string(32, 'a') / ".opencrown-checkpoint",
          "not a valid checkpoint");
    fs::create_directory_symlink(source,
                                 boundedCheckpoints / std::string(32, 'b'));
    write(boundedCheckpoints / std::string(32, 'c'), "ordinary unrelated file");
    expect(!::mkfifo((boundedCheckpoints / std::string(32, 'd')).c_str(), 0600),
           "retention FIFO fixture failed");
    SaveTransactions bounded(boundedSaves, boundedCheckpoints);
    std::vector<std::string> creationOrder;
    auto appendNew = [&] {
      auto ids = checkpoint_ids(boundedCheckpoints);
      unsigned added = 0;
      for (const auto &id : ids)
        if (std::find(creationOrder.begin(), creationOrder.end(), id) ==
            creationOrder.end()) {
          creationOrder.push_back(id);
          ++added;
        }
      expect(added == 1, "session did not publish exactly one new checkpoint");
    };
    for (unsigned i = 0; i < 15; ++i) {
      expect(bounded.begin(), "retention write begin failed");
      appendNew();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      write(boundedSaves / "SAVEDATA00/data.bin",
            "retained-progress-" + std::to_string(i));
      expect(bounded.commit() && bounded.retention_warning().empty(),
             "retention write commit failed");
      appendNew();
      const auto ids = checkpoint_ids(boundedCheckpoints);
      std::set<std::string> expected;
      const size_t first =
          creationOrder.size() > 10 ? creationOrder.size() - 10 : 0;
      for (size_t j = first; j < creationOrder.size(); ++j)
        expected.insert(creationOrder[j]);
      expect(ids == expected,
             "retention did not preserve exactly the latest ten checkpoints");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    expect(checkpoint_ids(boundedCheckpoints).size() == 10,
           "completed checkpoint count is not bounded");
    expect(bounded.begin(), "protected rollback begin failed");
    const auto activeJournal = base / "Retention/.Saves.write-session";
    std::istringstream activeRecord(read(activeJournal));
    std::string activeVersion, protectedId;
    std::getline(activeRecord, activeVersion);
    std::getline(activeRecord, protectedId);
    expect(checkpoint_ids(boundedCheckpoints).size() == 11 &&
               fs::exists(boundedCheckpoints / protectedId),
           "begin pruned its rollback checkpoint");
    fs::last_write_time(boundedCheckpoints / protectedId,
                        fs::file_time_type::clock::now() -
                            std::chrono::hours(24 * 365));
    write(boundedSaves / "SAVEDATA00/data.bin", "unfinished-after-retention");
    expect(!bounded.begin() && fs::exists(boundedCheckpoints / protectedId),
           "active rollback checkpoint was pruned");
    expect(bounded.recover(), "retained rollback checkpoint could not recover");
    expect(read(boundedSaves / "SAVEDATA00/data.bin") == "retained-progress-14",
           "retention lost rollback progress");
    expect(read(fs::path(bounded.last_recovery_path()) /
                "SAVEDATA00/data.bin") == "unfinished-after-retention",
           "retention recovery lost interrupted copy");
    expect(
        read(boundedCheckpoints / "keep-this-name") == "user data" &&
            read(boundedCheckpoints / ("interrupted-" + std::string(32, 'e')) /
                 "partial") == "repair candidate" &&
            fs::is_symlink(fs::symlink_status(boundedCheckpoints /
                                              std::string(32, 'b'))) &&
            fs::is_regular_file(fs::symlink_status(boundedCheckpoints /
                                                   std::string(32, 'c'))) &&
            fs::is_fifo(fs::symlink_status(boundedCheckpoints /
                                           std::string(32, 'd'))) &&
            read(boundedCheckpoints / std::string(32, 'a') /
                 ".opencrown-checkpoint") == "not a valid checkpoint",
        "retention touched interrupted, invalid, special or unknown entries");
    // The low-level checkpoint API deliberately has no retention policy.
    for (unsigned i = 0; i < 12; ++i)
      checkpoint_saves(boundedSaves, base / "UnprunedCheckpoints");
    expect(checkpoint_ids(base / "UnprunedCheckpoints").size() == 12,
           "generic checkpoint API unexpectedly pruned snapshots");
    std::cout << "{\"passed\":" << passed << ",\"failed\":0}\n";
    fs::remove_all(base);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
