#!/usr/bin/env python3
"""Write a private, slow counter build of the lazy-TLS runtime for QA only."""
import argparse,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():p.error('output must be on a mounted external disk')
out.mkdir(parents=True,exist_ok=False);source=Path(__file__).resolve().parents[1]/'runtime.cpp';s=source.read_text().replace('#include "runtime.h"','#include "runtime.h"\n#include <cstdio>',1)
m='''template <bool lifted>
[[gnu::always_inline]] static inline GuestScalarThread &selectScalarThread'''
n='''static std::atomic<uint64_t> dcpBoundChecks{0}, dcpUnboundChecks{0};
struct DcpTlsAuditReport { ~DcpTlsAuditReport() {
  std::fprintf(stderr,"DCP_TLS_AUDIT bound=%llu unbound=%llu\\n",
    (unsigned long long)dcpBoundChecks.load(), (unsigned long long)dcpUnboundChecks.load());
}};
static DcpTlsAuditReport dcpTlsAuditReport;
'''+m
assert s.count(m)==1;s=s.replace(m,n,1)
m='''    if (memory.scalarThread)
      return *memory.scalarThread;
    return unboundScalarThread();'''
n='''    if (memory.scalarThread) {
      dcpBoundChecks.fetch_add(1, std::memory_order_relaxed);
      return *memory.scalarThread;
    }
    dcpUnboundChecks.fetch_add(1, std::memory_order_relaxed);
    return unboundScalarThread();'''
assert s.count(m)==1;s=s.replace(m,n,1);target=out/'runtime-audit.cpp';target.write_text(s)
(out/'manifest.json').write_text(json.dumps({'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'instrumented_sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'scope':'Slow private QA counter; not for performance or distribution'},indent=2)+'\n')
