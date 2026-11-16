// Build only with the isolated shared-reader runtime supplied on the include path.
#include "runtime.cpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
Memory *dispatch(State &,uint64_t,Memory *m){return m;}
static bool wait_for(const std::atomic<bool> &value,int ms=2000){auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);while(!value.load()&&std::chrono::steady_clock::now()<until)std::this_thread::yield();return value.load();}
int main(){
 uint64_t word=0;std::atomic<bool> a{false},b{false},writer{false},releaseReaders{false},releaseWriter{false},lateReader{false};
 std::thread first([&]{AccessLock lock(&word,sizeof(word),0,true);a=true;while(!releaseReaders)std::this_thread::yield();});
 bool firstEntered=wait_for(a);
 std::thread second([&]{AccessLock lock(&word,sizeof(word),0,true);b=true;while(!releaseReaders)std::this_thread::yield();});
 bool shared=wait_for(b);
 std::thread third([&]{AccessLock lock(&word,sizeof(word),0,false);writer=true;while(!releaseWriter)std::this_thread::yield();});
 std::this_thread::sleep_for(std::chrono::milliseconds(20));bool writerBlocked=!writer.load();
 releaseReaders=true;first.join();second.join();bool writerEntered=wait_for(writer);
 std::thread fourth([&]{AccessLock lock(&word,sizeof(word),0,true);lateReader=true;});
 std::this_thread::sleep_for(std::chrono::milliseconds(20));bool readerBlocked=!lateReader.load();
 releaseWriter=true;third.join();fourth.join();
 bool ok=firstEntered&&shared&&writerBlocked&&writerEntered&&readerBlocked&&lateReader;
 std::printf("shared readers overlap=%d writer excluded=%d later reader excluded=%d\n",shared,writerBlocked,readerBlocked);
 return ok?0:1;
}
