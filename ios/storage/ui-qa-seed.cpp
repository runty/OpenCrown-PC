#include "save-transactions.h"
#include "storage-core.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <string>
namespace fs=std::filesystem;
using namespace dcp::storage;
static void check(bool x,const char *m){if(!x)throw std::runtime_error(m);}
static void put(const fs::path &p,const std::string &v){fs::create_directories(p.parent_path());std::ofstream f(p);f<<v;check(bool(f),"write failed");}
static std::string get(const fs::path &p){std::ifstream f(p);check(bool(f),"read failed");return {std::istreambuf_iterator<char>(f),{}};}
int main(int n,char **a){try{
  check(n==3,"usage: ui-qa-seed seed|verify QA_DOCUMENTS");fs::path root=a[2],saves=root/"ProbeSaves",backups=root/"SaveCheckpoints";
  check(get(root/".opencrown-ui-qa-only")=="synthetic restore QA\n","missing synthetic QA marker");
  if(std::string(a[1])=="seed" || std::string(a[1])=="seed-corrupt"){
    check(!fs::exists(saves)&&!fs::exists(backups),"QA roots must be empty");
    put(saves/"SAVEDATA00/SAVE0.DAT","chosen-progress");
    auto chosen=backups/checkpoint_saves(saves,backups);
    put(saves/"SAVEDATA00/SAVE0.DAT","before-interruption");
    SaveTransactions session(saves,backups);check(session.begin(),"begin failed");
    put(saves/"SAVEDATA00/SAVE0.DAT","interrupted-bytes");
    fs::last_write_time(chosen,fs::file_time_type::clock::now()+std::chrono::hours(1));
    if(std::string(a[1])=="seed-corrupt"){
      auto journal=root/".ProbeSaves.write-session";auto record=get(journal);
      std::istringstream stream(record);std::string version,before;
      std::getline(stream,version);std::getline(stream,before);
      check(version=="DCP-SAVE-SESSION-1"&&before.size()==32,"unexpected journal");
      put(root/".qa-journal-before",record);put(root/".qa-chosen",chosen.filename().string());
      put(backups/before/"SAVEDATA00/SAVE0.DAT",std::string(std::string("before-interruption").size(),'X'));
    }
    std::cout<<"seeded checkpoint="<<chosen.filename()<<"\n";
  }else if(std::string(a[1])=="verify-corrupt"){
    check(get(saves/"SAVEDATA00/SAVE0.DAT")=="interrupted-bytes","interrupted bytes changed");
    check(get(root/".ProbeSaves.write-session")==get(root/".qa-journal-before"),"journal changed");
    validate_checkpoint(backups/get(root/".qa-chosen"));
    check(!fs::exists(root/".ProbeSaves.previous"),"unexpected previous save");
    for(const auto &entry:fs::directory_iterator(backups))
      check(entry.path().filename().string().find("interrupted-")!=0,"unexpected recovery archive");
    std::cout<<"PASS: failed restore retained interrupted data, journal and chosen checkpoint\n";
  }else if(std::string(a[1])=="verify"){
    check(get(saves/"SAVEDATA00/SAVE0.DAT")=="chosen-progress","wrong restored bytes");
    check(get(root/".ProbeSaves.previous/SAVEDATA00/SAVE0.DAT")=="before-interruption","prewrite copy lost");
    unsigned count=0;for(const auto &entry:fs::directory_iterator(backups))
      if(entry.path().filename().string().find("interrupted-")==0){++count;check(get(entry.path()/"SAVEDATA00/SAVE0.DAT")=="interrupted-bytes","interrupted copy lost");}
    check(count==1,"expected one interrupted copy");
    SaveTransactions next(saves,backups);check(!next.active()&&next.recover(),"next launch recovery failed");
    check(get(saves/"SAVEDATA00/SAVE0.DAT")=="chosen-progress","next launch changed restored data");
    std::cout<<"PASS: chosen save, prewrite copy, interrupted archive and simulated next launch\n";
  }else throw std::runtime_error("unknown mode");
  return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
