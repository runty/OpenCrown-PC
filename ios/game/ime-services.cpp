#include "ime-services.h"
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace {
std::mutex lock;
DcpImeHooks hooks;
DcpImeRequest request;
uint64_t next_id = 0, buffer = 0;
int status = 0, end_status = 0;
bool completed = false, accepted = false;
std::u16string completion;
constexpr uint32_t invalid = 0x80020016, busy = 0x80bc0001,
                   invalid_state = 0x80bc0004;
void *pointer(uint64_t address, size_t bytes, bool writing) {
  if (!address || !hooks.pointer)
    throw std::runtime_error("IME null guest buffer");
  auto *p = hooks.pointer(hooks.context, address, bytes, writing);
  if (!p)
    throw std::runtime_error("IME invalid guest buffer");
  return p;
}
void validate(uint64_t address, size_t size, bool writing) {
  if (!address ||
      (hooks.validate_memory
           ? !hooks.validate_memory(hooks.context, address, size, writing)
           : !pointer(address, size, writing)))
    throw std::runtime_error("IME invalid guest range");
}
void read_memory(uint64_t address, void *output, size_t size) {
  if (hooks.read_memory) {
    if (!address || !hooks.read_memory(hooks.context, address, output, size))
      throw std::runtime_error("IME invalid guest read");
  } else
    std::memcpy(output, pointer(address, size, false), size);
}
void write_memory(uint64_t address, const void *input, size_t size) {
  if (hooks.write_memory) {
    if (!address || !hooks.write_memory(hooks.context, address, input, size))
      throw std::runtime_error("IME invalid guest write");
  } else
    std::memcpy(pointer(address, size, true), input, size);
}
template <class T> T read(uint64_t address) {
  T value;
  read_memory(address, &value, sizeof(T));
  return value;
}
std::u16string text(uint64_t address, size_t limit) {
  std::u16string result;
  if (!address)
    return result;
  for (size_t i = 0; i < limit; ++i) {
    if (address > UINT64_MAX - i * 2)
      throw std::runtime_error("IME guest string overflow");
    auto c = read<char16_t>(address + i * 2);
    if (!c)
      break;
    result.push_back(c);
  }
  return result;
}
// Replace malformed UTF-16 and truncate without splitting a surrogate pair.
std::u16string sanitize(const std::u16string &input, size_t maximum) {
  std::u16string output;
  for (size_t i = 0; i < input.size() && output.size() < maximum; ++i) {
    char16_t c = input[i];
    if (!c)
      break;
    if (c >= 0xd800 && c <= 0xdbff) {
      if (i + 1 < input.size() && input[i + 1] >= 0xdc00 &&
          input[i + 1] <= 0xdfff) {
        if (output.size() + 2 > maximum)
          break;
        output.push_back(c);
        output.push_back(input[++i]);
      } else
        output.push_back(0xfffd);
    } else
      output.push_back(c >= 0xdc00 && c <= 0xdfff ? 0xfffd : c);
  }
  return output;
}
void apply_completion() {
  if (status != 1 || !completed)
    return;
  if (accepted) {
    auto value = sanitize(completion, request.max_units);
    value.push_back(0);
    // Validate the entire write before publishing FINISHED. A failed access
    // leaves the request pending rather than reporting a successful name edit.
    write_memory(buffer, value.data(), value.size() * 2);
  }
  end_status = accepted ? 0 : 1;
  status = 2;
  completion.clear();
}
} // namespace

void dcp_ime_init(DcpImeHooks value) {
  const unsigned copies = unsigned(value.validate_memory != nullptr) +
                          unsigned(value.read_memory != nullptr) +
                          unsigned(value.write_memory != nullptr);
  if (copies && copies != 3)
    throw std::runtime_error("IME memory callbacks must be supplied together");
  std::lock_guard<std::mutex> guard(lock);
  hooks = value;
  request = {};
  buffer = 0;
  status = end_status = 0;
  completed = accepted = false;
  completion.clear();
}
bool dcp_ime_pending(DcpImeRequest *output) {
  std::lock_guard<std::mutex> guard(lock);
  if (status != 1 || completed)
    return false;
  if (output)
    *output = request;
  return true;
}
bool dcp_ime_finish(uint64_t id, const std::u16string *value) {
  std::lock_guard<std::mutex> guard(lock);
  if (status != 1 || completed || request.id != id)
    return false;
  // Bound host input as well as guest writes. Nothing borrowed survives here.
  completion = value ? sanitize(*value, request.max_units) : std::u16string{};
  accepted = value != nullptr;
  completed = true;
  return true;
}
bool dcp_ime_service(const char *name, const uint64_t a[6], uint64_t *result) {
  if (!name || std::strncmp(name, "sceImeDialog", 12))
    return false;
  std::lock_guard<std::mutex> guard(lock);
  uint32_t value = 0;
  if (!std::strcmp(name, "sceImeDialogInit")) {
    if (status)
      value = busy;
    else if (!a[0] || a[0] > UINT64_MAX - 0x50)
      value = invalid;
    else {
      // Validate all fixed fields and the complete output capacity first.
      validate(a[0], 0x50, false);
      uint32_t maximum = read<uint32_t>(a[0] + 0x24);
      uint64_t destination = read<uint64_t>(a[0] + 0x28);
      if (!maximum || maximum > 2048 || !destination)
        value = invalid;
      else {
        validate(destination, (size_t(maximum) + 1) * 2, true);
        DcpImeRequest next;
        next.max_units = maximum;
        next.text = sanitize(text(destination, maximum), maximum);
        next.title = sanitize(text(read<uint64_t>(a[0] + 0x48), 256), 256);
        next.id = ++next_id;
        request = std::move(next);
        buffer = destination;
        completed = accepted = false;
        completion.clear();
        end_status = 0;
        status = hooks.host_present ? 1 : 2;
      }
    }
  } else if (!std::strcmp(name, "sceImeDialogGetStatus")) {
    apply_completion();
    value = status;
  } else if (!std::strcmp(name, "sceImeDialogGetResult")) {
    apply_completion();
    if (status != 2)
      value = invalid_state;
    else if (a[0])
      write_memory(a[0], &end_status, 4);
  } else if (!std::strcmp(name, "sceImeDialogAbort")) {
    if (status == 1) {
      completed = false;
      completion.clear();
      status = 2;
      end_status = 2;
    }
  } else if (!std::strcmp(name, "sceImeDialogTerm")) {
    request = {};
    buffer = 0;
    completed = accepted = false;
    completion.clear();
    status = 0;
  } else if (!std::strcmp(name, "sceImeDialogGetPanelSizeExtended")) {
    // Validate both outputs before writing either one.
    if (a[2])
      validate(a[2], 4, true);
    if (a[3])
      validate(a[3], 4, true);
    const uint32_t w = 793, h = 408;
    if (a[2])
      write_memory(a[2], &w, 4);
    if (a[3])
      write_memory(a[3], &h, 4);
  } else
    return false;
  if (result)
    *result = value;
  return true;
}
