#pragma once
#include <cfenv>
#include <exception>
#include <stdexcept>

// Experimental only: isolates a guest floating-point environment across a
// native boundary. It neither maps MXCSR flags nor implements guest traps.
class ExperimentalNativeBoundary {
public:
  ExperimentalNativeBoundary() {
    if (std::fegetenv(&guest_) != 0)
      throw std::runtime_error("cannot save guest floating environment");
    std::fenv_t ignored;
    if (std::feholdexcept(&ignored) != 0)
      throw std::runtime_error("cannot hold native floating exceptions");
  }
  ExperimentalNativeBoundary(const ExperimentalNativeBoundary &) = delete;
  ExperimentalNativeBoundary &
  operator=(const ExperimentalNativeBoundary &) = delete;
  ~ExperimentalNativeBoundary() noexcept {
    if (std::fesetenv(&guest_) != 0)
      std::terminate();
  }

  class GuestCallback {
  public:
    explicit GuestCallback(ExperimentalNativeBoundary &boundary)
        : boundary_(boundary) {
      if (std::fegetenv(&native_) != 0)
        throw std::runtime_error("cannot save native floating environment");
      if (std::fesetenv(&boundary_.guest_) != 0)
        throw std::runtime_error(
            "cannot restore callback floating environment");
    }
    GuestCallback(const GuestCallback &) = delete;
    GuestCallback &operator=(const GuestCallback &) = delete;
    ~GuestCallback() noexcept {
      if (std::fegetenv(&boundary_.guest_) != 0 || std::fesetenv(&native_) != 0)
        std::terminate();
    }

  private:
    ExperimentalNativeBoundary &boundary_;
    std::fenv_t native_;
  };

private:
  std::fenv_t guest_;
};
