#pragma once
#include <filesystem>
#include <mutex>
#include <string>

namespace dcp::storage {
// All guest writers must be quiescent at each boundary. The object serializes
// its own operations; all instances and direct storage API calls for these
// paths still require exclusive access. Methods catch errors for typed HLE
// callbacks.
class SaveTransactions {
public:
  SaveTransactions(std::filesystem::path save_root,
                   std::filesystem::path checkpoint_root);
  bool begin() noexcept;
  bool commit() noexcept;
  bool recover() noexcept;
  std::string error() const;
  std::string last_recovery_path() const;
  // Retention is best effort after a durable commit; failure never converts an
  // already committed save into an uncommitted session.
  std::string retention_warning() const;
  bool active() const;

private:
  std::filesystem::path saves_, checkpoints_, journal_;
  mutable std::mutex lock_;
  std::string error_, recovered_, retention_warning_;
};
} // namespace dcp::storage
