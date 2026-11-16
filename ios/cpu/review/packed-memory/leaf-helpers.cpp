// Experimental helpers linked only into the fixed-leaf benchmark.
#define DCP_READ256_BENCH 1
#define DCP_EXTERNAL_DISPATCH 1
#include "read256-study.cpp"
#ifdef DCP_LEAF_AUDIT
static std::atomic<uint64_t> fastReads{0}, slowReads{0};
struct AuditReport { ~AuditReport() { std::fprintf(stderr,"fast_reads=%llu slow_reads=%llu\n", (unsigned long long)fastReads.load(), (unsigned long long)slowReads.load()); } };
static AuditReport auditReport;
#endif
extern "C" void dcp_read_memory_f32_vector(Memory *m, uint64_t address,
                                           uint32_t lanes, void *output) {
  if (lanes != 4 && lanes != 8) throw std::invalid_argument("bad lane count");
  uint32_t values[8]{};
  if (lanes == 8) {
#ifdef DCP_LEAF_AUDIT
    if (try_read256(*m,address,values)) ++fastReads;
    else { ++slowReads; original(*m,address,values); }
#else
    candidate(*m, address, values);
#endif
  }
  else for (unsigned i=0; i<lanes; ++i) {
    auto value=__remill_read_memory_f32(m,address+4*i);
    std::memcpy(values+i,&value,4);
  }
  std::memcpy(output, values, lanes*4);
}
extern "C" Memory *dcp_write_memory_f32_vector(Memory *m, uint64_t address,
                                                uint32_t lanes, const void *input) {
  if (lanes != 4 && lanes != 8) throw std::invalid_argument("bad lane count");
  for (unsigned i=0; i<lanes; ++i) {
    float value; std::memcpy(&value,static_cast<const uint8_t *>(input)+4*i,4);
    m=__remill_write_memory_f32(m,address+4*i,value);
  }
  return m;
}
