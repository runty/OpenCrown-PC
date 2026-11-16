#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace dcp::storage {
struct FileSpec {
  std::string path;
  uint64_t size;
  std::string sha256; // Empty only for archives, which get size/TOC validation.
  bool cpk = false;
  bool optional = false; // Optional size=0 metadata is bounded to 16 MiB.
};
struct Progress {
  uint64_t copied, total;
  std::string file;
};
using ProgressCallback = std::function<void(const Progress &)>;
std::vector<FileSpec> supported_game_files();
// The caller owns exclusive access to destination and must stop the guest
// first. Copies into a sibling staging directory, validates, then atomically
// exchanges the existing directory on Darwin. Cancel/failure leaves current
// Game intact.
void import_game(const std::filesystem::path &source,
                 const std::filesystem::path &destination,
                 const std::atomic<bool> &cancel,
                 ProgressCallback progress = {});
// Startup recovery never promotes an unvalidated pending import. Retains the
// previous installed directory after an interrupted successful exchange.
void recover_import(const std::filesystem::path &destination);
void validate_game(const std::filesystem::path &directory);

// Call only with ALL guest writers stopped. Checkpoints contain complete local
// save trees and a SHA-256 inventory. They never access iCloud or live Mac
// saves. Empty saves are permitted; caller decides when a checkpoint is useful.
std::string checkpoint_saves(const std::filesystem::path &save_root,
                             const std::filesystem::path &checkpoint_root);
void recover_checkpoints(const std::filesystem::path &checkpoint_root);
void validate_checkpoint(const std::filesystem::path &checkpoint);
// Refuses to replace a save with an unresolved write-session journal. The UI
// should use restore_user_checkpoint to recover before applying the selection.
void restore_checkpoint(const std::filesystem::path &checkpoint,
                        const std::filesystem::path &save_root);
void recover_save_restore(const std::filesystem::path &save_root);

// Manual Files handoff. Source is a folder containing SAVEDATA00/SAVE0.DAT,
// either a raw Mac save root or an exported checkpoint. A supplied inventory
// must validate and match the staged bytes. No cloud folder is modified.
// All guest writers must be stopped and these roots exclusively owned.
std::string stage_save_transfer(const std::filesystem::path &source,
                               const std::filesystem::path &staging_root);
// Stages/validates the incoming tree before backing up and replacing local
// progress. Returns the pre-import checkpoint ID (empty for a first import).
std::string import_save_folder(const std::filesystem::path &source,
                               const std::filesystem::path &save_root,
                               const std::filesystem::path &checkpoint_root);

// Generic manifest-based engine is exposed for small file-free regression
// fixtures. Production import_game always uses supported_game_files().
namespace detail {
void restore_checkpoint_generation(const std::filesystem::path &checkpoint,
                                   const std::filesystem::path &save_root,
                                   const std::string &generation);
void import_manifest(const std::filesystem::path &source,
                     const std::filesystem::path &destination,
                     const std::vector<FileSpec> &manifest,
                     const std::atomic<bool> &cancel,
                     ProgressCallback progress = {});
} // namespace detail
} // namespace dcp::storage
