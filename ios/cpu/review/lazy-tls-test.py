#!/usr/bin/env python3
"""Instrument only the isolated TLS fallback; never use this runtime in a game."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--backend-root',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();cpu=Path(__file__).resolve().parents[1]
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():p.error('output must be on mounted external storage')
out.mkdir(parents=True,exist_ok=False)
source=(cpu/'runtime.cpp').read_text();old='[[gnu::noinline]] static GuestScalarThread &unboundScalarThread() {\n  return scalarThread;';new='extern "C" void dcp_test_tls_fallback();\n[[gnu::noinline]] static GuestScalarThread &unboundScalarThread() {\n  dcp_test_tls_fallback();\n  return scalarThread;';assert source.count(old)==1
instrumented=out/'runtime-instrumented.cpp';instrumented.write_text(source.replace(old,new,1))
fixture=Path(__file__).with_name('lazy-tls.cpp');report={'candidate_sha256':hashlib.sha256((cpu/'runtime.cpp').read_bytes()).hexdigest(),'runs':{}}
for mode,flags in [('native',[]),('asan-ubsan',['-fsanitize=address,undefined']),('tsan',['-fsanitize=thread'])]:
 binary=out/mode
 cmd=['xcrun','clang++','-std=c++17','-O2','-g','-pthread','-Wall','-Wextra','-Werror',*flags,'-I'+str(cpu),'-I'+str(a.backend_root/'remill/include'),str(instrumented),str(fixture),'-o',str(binary)]
 with (out/(mode+'-build.log')).open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
 env=dict(os.environ,ASAN_OPTIONS='abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1',TSAN_OPTIONS='halt_on_error=1')
 r=subprocess.run([str(binary)],capture_output=True,text=True,env=env,timeout=120);(out/(mode+'.txt')).write_text(r.stdout+r.stderr);report['runs'][mode]={'exit':r.returncode,'output':r.stdout};(out/'results.json').write_text(json.dumps(report,indent=2)+'\n');r.check_returncode();print(mode,r.stdout.strip(),flush=True)
