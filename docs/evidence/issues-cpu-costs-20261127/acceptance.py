from pathlib import Path
import subprocess,json,re,time
r=Path('/private/tmp/aiverse-issue223');b=Path('/Volumes/KINGSTON/AIVERSE-builds/codex-issue223-minimap-atlas-quick');out=r/'docs/evidence/issues-cpu-costs-20261127';out.mkdir(exist_ok=True)
cases=['atlas_pack_integration_and_runtime_lookup','audio_repeated_selection','precise_windup'];runs=[]
for case in cases:
 for iteration in range(3):
  command=['/usr/bin/time','-l',str(b/'aiverse_tests'),case];start=time.monotonic();x=subprocess.run(command,cwd=r,text=True,capture_output=True,timeout=60);wall=time.monotonic()-start;log=x.stdout+x.stderr;name=case+'-'+str(iteration)+'.log.txt';(out/name).write_text(log);assert x.returncode==0 and '0 failed' in x.stdout
  rss=int(re.search(r'(\d+)\s+maximum resident set size',log)[1]);times=re.search(r'([\d.]+) real\s+([\d.]+) user\s+([\d.]+) sys',log);cpu=float(times[2])+float(times[3]);assert cpu<1 and rss<536870912
  runs.append({'case':case,'iteration':iteration,'exit_code':x.returncode,'log':name,'whole_process_cpu_seconds':cpu,'wall_seconds_including_launch':wall,'peak_rss_bytes':rss});print(case,iteration,'cpu',cpu,'rss',rss,flush=True)
(out/'result.json').write_text(json.dumps({'sha':subprocess.check_output(['git','rev-parse','HEAD'],cwd=r,text=True).strip(),'binary_source':'85414a9ca-era aggregate build; relevant tests and production owners unchanged since build','budgets':{'cpu_seconds_per_complete_case':1,'peak_process_rss_bytes':536870912},'scope':'Whole native regression process including fixture creation, load, operations and teardown.3repeats each. These are coarse bounded-workload costs, not latency per callback or frame budgets.','runs':runs},indent=2)+'\n')
