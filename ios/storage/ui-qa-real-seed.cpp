// Test-only: seeds an explicitly marked, empty QA container with copies of two
// real save trees. It never writes the source trees.
#include "save-transactions.h"
#include "storage-core.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
static void check(bool x,const char *m){if(!x)throw std::runtime_error(m);}
static std::string get(const fs::path &p){std::ifstream f(p);check(bool(f),"read failed");return {std::istreambuf_iterator<char>(f),{}};}
static void put(const fs::path &p,const std::string &s){std::ofstream f(p);f<<s;check(bool(f),"write failed");}
int main(int n,char **a){try{
  check(n==4,"usage: ui-qa-real-seed QA_DOCUMENTS CHOSEN_SOURCE PREWRITE_SOURCE");
  fs::path root=a[1],saves=root/"ProbeSaves",backups=root/"SaveCheckpoints";
  check(get(root/".opencrown-ui-qa-only")=="real-save restore QA\n","missing QA marker");
  check(!fs::exists(saves)&&!fs::exists(backups),"QA save roots must be empty");
  const auto chosen=backups/dcp::storage::checkpoint_saves(a[2],backups);
  fs::copy(a[3],saves,fs::copy_options::recursive);
  dcp::storage::SaveTransactions t(saves,backups);check(t.begin(),"begin failed");
  auto slot=saves/"SAVEDATA00/SAVE0.DAT";std::fstream file(slot,std::ios::in|std::ios::out|std::ios::binary);
  check(bool(file),"cannot open QA slot");file.seekg(-1,std::ios::end);char byte=0;file.read(&byte,1);check(bool(file),"cannot read QA slot");
  byte^=0x5a;file.seekp(-1,std::ios::end);file.write(&byte,1);file.flush();check(bool(file),"cannot modify QA slot");file.close();
  fs::last_write_time(chosen,fs::file_time_type::clock::now()+std::chrono::hours(1));
  put(root/".qa-chosen",chosen.filename().string());
  std::cout<<"seeded chosen="<<chosen.filename()<<" with interrupted copy\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
