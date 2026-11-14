// Export the existing loader's parsed module metadata and unwrapped bytes
// privately.
#include "loader/elf.h"
#include "loader/nid.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::runtime_error("Usage: module-tool INPUT OUTPUT_ELF");
    std::ifstream input(argv[1], std::ios::binary);
    if (!input)
      throw std::runtime_error("cannot open input");
    auto data =
        dcp::loader::unwrap_self({std::istreambuf_iterator<char>(input), {}});
    auto info = dcp::loader::parse_elf(data);
    std::ofstream output(argv[2], std::ios::binary);
    output.write(reinterpret_cast<const char *>(data.data()), data.size());
    if (!output)
      throw std::runtime_error("cannot write ELF");
    std::map<std::string, std::string> names;
    for (auto name : {"memcpy", "memmove", "memcmp", "memset", "strlen",
                      "strcmp", "strcpy", "malloc", "free", "_init_env"})
      names[dcp::loader::nid_from_name(name)] = name;
    std::cout << "{\"entry\":" << info.entry
              << ",\"proc_param\":" << info.proc_param
              << ",\"tls_vaddr\":" << info.tls.vaddr
              << ",\"tls_filesz\":" << info.tls.filesz
              << ",\"tls_memsz\":" << info.tls.memsz
              << ",\"image_size\":" << info.image_size << ",\"segments\":[";
    bool first = true;
    for (auto segment : info.load) {
      if (!first)
        std::cout << ',';
      first = false;
      std::cout << "{\"offset\":" << segment.offset
                << ",\"vaddr\":" << segment.vaddr
                << ",\"filesz\":" << segment.filesz
                << ",\"memsz\":" << segment.memsz
                << ",\"flags\":" << segment.flags << '}';
    }
    std::cout << "],\"symbols\":[";
    first = true;
    for (const auto &symbol : info.syms) {
      if (!first)
        std::cout << ',';
      first = false;
      auto nid = symbol.name.substr(0, symbol.name.find('#'));
      std::cout << "{\"nid\":\"" << symbol.name << "\",\"name\":\""
                << names[nid] << "\",\"value\":" << symbol.value
                << ",\"defined\":" << symbol.shndx << '}';
    }
    std::cout << "],\"relocations\":[";
    first = true;
    for (const auto &reloc : info.relocs) {
      if (!first)
        std::cout << ',';
      first = false;
      std::cout << '[' << reloc.offset << ',' << reloc.type << ',' << reloc.sym
                << ',' << reloc.addend << ']';
    }
    std::cout << "]}\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
