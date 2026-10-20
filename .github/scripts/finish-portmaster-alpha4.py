"""One-use release coordinator: build exact source, validate, publish existing draft."""
import hashlib,io,json,os,struct,tarfile,time,urllib.request,urllib.error,urllib.parse,zipfile
from pathlib import Path,PurePosixPath
REPO=os.environ['GITHUB_REPOSITORY'];TOKEN=os.environ['GH_TOKEN'];SOURCE=os.environ['BUILD_SHA'];STREAM=os.environ['RELEASE_STREAM']
CONFIG={
 'desktop':('v0.1.5','build-desktop.yml','Vurus v0.1.5',False,['vurus-linux.tar.gz','vurus-windows-amd64.zip','vurus-darwin-arm64.zip','vurus-darwin-x86_64.zip']),
 'portmaster':('portmaster-v0.1.0-alpha.4','build-portmaster.yml','Vurus PortMaster Alpha Preview alpha.4',True,['vurus-portmaster.zip'])}
TAG,WORKFLOW,TITLE,PRE,PACKAGES=CONFIG[STREAM]
API='https://api.github.com/repos/'+REPO+'/'
class SafeRedirect(urllib.request.HTTPRedirectHandler):
 def redirect_request(self,request,fp,code,message,headers,url):
  redirected=super().redirect_request(request,fp,code,message,headers,url)
  if redirected is not None and urllib.parse.urlsplit(request.full_url).netloc!=urllib.parse.urlsplit(url).netloc:
   redirected.remove_header('Authorization')
  return redirected
opener=urllib.request.build_opener(SafeRedirect())
def api(path,method='GET',data=None,binary=False):
 request=urllib.request.Request(API+path,data=None if data is None else json.dumps(data).encode(),method=method,headers={'Authorization':'Bearer '+TOKEN,'Accept':'application/octet-stream' if binary and path.startswith('releases/assets/') else 'application/vnd.github+json','X-GitHub-Api-Version':'2022-11-28','User-Agent':'vurus-release-validator','Content-Type':'application/json'})
 for attempt in range(5):
  try:
   with opener.open(request,timeout=120) as response:
    body=response.read();return body if binary else (json.loads(body) if body else None)
  except urllib.error.HTTPError as error:
   if method!='GET' or error.code not in (429,500,502,503,504) or attempt==4:raise
   time.sleep(10*(attempt+1))
def maybe(path):
 try:return api(path)
 except urllib.error.HTTPError as error:
  if error.code==404:return None
  raise
def check_tag():
 ref=api('git/ref/tags/'+TAG);assert ref['object']['type']=='commit' and ref['object']['sha']==SOURCE,ref
existing=maybe('git/ref/tags/'+TAG)
if existing is None:api('git/refs','POST',{'ref':'refs/tags/'+TAG,'sha':SOURCE})
check_tag()
# The GITHUB_TOKEN-created tag does not cause a push build. A single intentional
# workflow_dispatch at that tag invokes the unchanged normal build/release stream.
def runs():
 return [r for r in api('actions/workflows/'+WORKFLOW+'/runs?event=workflow_dispatch&head_sha='+SOURCE+'&per_page=100')['workflow_runs'] if r['head_sha']==SOURCE and r['head_branch']==TAG]
found=runs();assert len(found)<=1,'Duplicate matching builds; inspect before retrying'
assert len(found)==1 and found[0]['id']==39514613513, 'Recovery requires exact existing build'
deadline=time.time()+110*60;run=None
while time.time()<deadline:
 found=runs();assert len(found)<=1,'Duplicate matching builds'
 if found:
  run=api('actions/runs/'+str(found[0]['id']));print('BUILD',run['html_url'],run['status'],run['conclusion'],flush=True)
  assert run['head_sha']==SOURCE and run['event']=='workflow_dispatch' and run['head_branch']==TAG and run['path']=='.github/workflows/'+WORKFLOW
  if run['status']=='completed':break
 time.sleep(20)
assert run and run['status']=='completed'
expected_count=2 if PRE else 5
for attempt in range(30):
 jobs=api('actions/runs/'+str(run['id'])+'/jobs?per_page=100')['jobs']
 if len(jobs)==expected_count and all(j['status']=='completed' for j in jobs):break
 time.sleep(10)
assert PRE and run['id']==39514613513 and len(jobs)==2
build=next(j for j in jobs if j['name']=='build');publication=next(j for j in jobs if j['name']=='release')
assert build['conclusion']=='success' and publication['conclusion']=='failure'
failed_log=api('actions/jobs/'+str(publication['id'])+'/logs',binary=True).decode('utf-8-sig')
assert 'sha256sum: SHA256SUMS.txt: No such file or directory' in failed_log
print('REUSE_VERIFIED_BUILD',run['html_url'],'normal draft step failed on known nested checksum path',flush=True)
if not PRE:
 for label in ['linux-x64','windows-x64','macos-arm64','macos-x64']:
  assert any(label in j['name'] and j['name'].startswith('build (') for j in jobs),label
else:assert sorted(j['name'] for j in jobs)==['build','release']
for job in jobs:
 if job['name'].startswith('build'):
  required=['Build, test and package ARM64 GLES','Verify package and record checksum'] if PRE else ['Test','Standalone CLI smoke test']
  for name in required:assert any(s['name']==name and s['conclusion']=='success' for s in job['steps']),name
artifacts=api('actions/runs/'+str(run['id'])+'/artifacts?per_page=100')['artifacts'];payloads={};artifact_receipts=[]
for artifact in artifacts:
 if (PRE and artifact['name']!='vurus-portmaster-aarch64-alpha-preview') or (not PRE and not artifact['name'].startswith('vurus-')):continue
 assert not artifact['expired'];raw=api('actions/artifacts/'+str(artifact['id'])+'/zip',binary=True)
 with zipfile.ZipFile(io.BytesIO(raw)) as z:
  assert z.testzip() is None
  for name in z.namelist():
   key=PurePosixPath(name).name
   if key in PACKAGES or (PRE and key in ['SHA256SUMS.txt','source-revision.txt']):
    assert key not in payloads,key;payloads[key]=z.read(name)
 artifact_receipts.append({'id':artifact['id'],'name':artifact['name'],'digest':artifact.get('digest'),'download_sha256':hashlib.sha256(raw).hexdigest()})
assert all(name in payloads and payloads[name] for name in PACKAGES)
if PRE:assert payloads['source-revision.txt'].decode().strip()==SOURCE

def validate_package(name,raw):
 if name.endswith('.zip'):
  with zipfile.ZipFile(io.BytesIO(raw)) as z:
   assert z.testzip() is None;entries={i.filename:z.read(i) for i in z.infolist() if not i.is_dir()}
 else:
  with tarfile.open(fileobj=io.BytesIO(raw),mode='r:gz') as t:
   assert all(i.isdir() or i.isfile() for i in t.getmembers()),'special archive entry'
   entries={i.name:t.extractfile(i).read() for i in t.getmembers() if i.isfile()}
 for n in entries:
  p=PurePosixPath(n);assert not p.is_absolute() and '..' not in p.parts,n
  assert not any(part.lower() in {'work','.git','.agents','.codex','saves','data'} for part in p.parts),n
  assert p.suffix.lower() not in {'.iso','.bin','.rsc','.gaf','.vzs','.cfg','.ppm'},n
  assert p.name.lower() not in {'vz.sco','options.cfg','slot1.vzs'},n
 names=list(entries);lower=[n.lower() for n in names]
 assert any(PurePosixPath(n).name.lower()=='license.md' for n in names),'engine license missing'
 for needed in ['zlib','smaa','mame','notice','rust-licenses']:
  assert any(needed in n for n in lower),(name,'missing notice',needed)
 # The Indeo bridge's own license is embedded in its Rust license list.
 assert any('oxideav' in n for n in lower),(name,'Indeo attribution missing')
 if PRE:
  assert 'Vurus.sh' in entries and 'vurus/vurus.aarch64' in entries
  binary=entries['vurus/vurus.aarch64'];assert binary[:4]==b'\x7fELF' and binary[4:6]==b'\x02\x01' and struct.unpack_from('<H',binary,18)[0]==183
  for suffix in ['libc++.txt','libc++abi.txt','GCC-runtime.txt']:assert any(n.endswith(suffix) for n in names)
 else:
  suffix='vurus.exe' if 'windows' in name else 'vurus';matches=[v for n,v in entries.items() if PurePosixPath(n).name==suffix];assert len(matches)==1
  binary=matches[0]
  if 'linux' in name:assert binary[:4]==b'\x7fELF' and binary[4:6]==b'\x02\x01' and struct.unpack_from('<H',binary,18)[0]==62
  elif 'windows' in name:
   assert binary[:2]==b'MZ';offset=struct.unpack_from('<I',binary,0x3c)[0];assert binary[offset:offset+4]==b'PE\0\0' and struct.unpack_from('<H',binary,offset+4)[0]==0x8664
  else:assert binary[:4]==b'\xcf\xfa\xed\xfe' and struct.unpack_from('<I',binary,4)[0]==(0x100000c if 'arm64' in name else 0x1000007)
 print('PACKAGE_VERIFIED',name,len(entries),'files',hashlib.sha256(raw).hexdigest(),flush=True)
 return names
package_entries={name:validate_package(name,payloads[name]) for name in PACKAGES};hashes={name:hashlib.sha256(payloads[name]).hexdigest() for name in PACKAGES}
# Locate the existing normal workflow draft. Never overwrite an old public asset.
checks=payloads['SHA256SUMS.txt'].decode()
checkmap={line.split(maxsplit=1)[1].strip().lstrip('*'):line.split()[0] for line in checks.splitlines() if line.strip()}
assert checkmap==hashes
releases=api('releases?per_page=100');matches=[r for r in releases if r['tag_name']==TAG];assert len(matches)<=1
if matches:release=matches[0];assert release['draft'],'Already published; inspect rather than overwrite'
else:release=api('releases','POST',{'tag_name':TAG,'target_commitish':SOURCE,'name':TITLE,'body':'Validated ARM64 build; final release notes pending.','draft':True,'prerelease':True,'make_latest':'false'})
check_tag()
existing_assets={a['name']:a for a in release['assets']};upload_payloads={name:payloads[name] for name in PACKAGES+['SHA256SUMS.txt']}
assert set(existing_assets)<=set(upload_payloads)
for name,raw in upload_payloads.items():
 digest='sha256:'+hashlib.sha256(raw).hexdigest()
 if name in existing_assets:
  assert existing_assets[name]['digest']==digest and existing_assets[name]['size']==len(raw)
  continue
 url='https://uploads.github.com/repos/'+REPO+'/releases/'+str(release['id'])+'/assets?name='+name
 request=urllib.request.Request(url,data=raw,method='POST',headers={'Authorization':'Bearer '+TOKEN,'Accept':'application/vnd.github+json','Content-Type':'application/zip' if name.endswith('.zip') else 'text/plain','User-Agent':'vurus-release-validator'})
 with opener.open(request,timeout=120) as response:asset=json.loads(response.read())
 assert asset['digest']==digest and asset['state']=='uploaded'
release=api('releases/'+str(release['id']))
assets={a['name']:a for a in release['assets']};assert set(assets)==set(PACKAGES+['SHA256SUMS.txt']),list(assets)
for name,digest in hashes.items():
 assert assets[name]['state']=='uploaded' and assets[name]['size']==len(payloads[name])
 assert assets[name]['digest']=='sha256:'+digest,(name,assets[name]['digest'],digest)
checks=api('releases/assets/'+str(assets['SHA256SUMS.txt']['id']),binary=True).decode();checkmap={line.split(maxsplit=1)[1].strip().lstrip('*'):line.split()[0] for line in checks.splitlines() if line.strip()};assert checkmap==hashes
check_tag()
notes='''This update improves crowded-scene CPU work, animation and audio maintenance while preserving the accepted gameplay and memory improvements.

- Cache resolved simulation/render properties and weapon sound bindings instead of repeating authored-property and clip lookups.
- Preserve ordered reference-owner queries and avoid empty legacy route work; trim redundant animation synchronization and sorting.
- Load the save browser catalogue in the background so opening menus after saving or death stays responsive; load only the selected preview.
- Repair native enemy movement/command handling, route headings and generator creator/UID and bounds publication.
- Refresh the README with measured H700 encounter FPS, nearby enemy counts, startup memory samples and a simulation/render timing breakdown.

On H700 at 640×480, four current development encounter samples measured reciprocal median frame rates of about 49.8, 60.0, 42.0 and 33.8 FPS. They are location-specific samples, not whole-level or device guarantees; music was off, and small regressions and frame-time spikes remain documented. See the [performance report](https://github.com/REPOSITORY/blob/SOURCE/docs/performance_update_2026-10-21.md) for settings, full percentiles, baseline comparison and separate startup RSS provenance.

Save schema v35 and its existing compression envelope are unchanged. Keep imported data, saves, settings and scores when upgrading. The existing cross-platform RNG-state save limitation remains; use saves with their originating platform/build family. Original game resources are required and are not included. Engine and third-party notices accompany the packages. Verify downloads with SHA256SUMS.txt.
'''.replace('REPOSITORY',REPO).replace('SOURCE',SOURCE)
if PRE:notes+='\nARM64 / OpenGL ES 3.0 PortMaster Alpha Preview. Extract Vurus.sh and the vurus directory into the firmware Ports folder, preserving existing user files. Use the normal Ports launcher. This remains a prerelease and does not replace the latest desktop stable release.\n'
else:notes+='\nDesktop downloads: Linux x64 / Steam Deck, Windows x64, macOS Apple Silicon and Intel. macOS requires version 15+, ships unsigned terminal executables and uses FXAA instead of SMAA. CI validation does not replace physical testing on every platform.\n'
notes+='\nBuilt and tested by [GitHub CI]('+run['html_url']+'), source commit [`'+SOURCE[:7]+'`](https://github.com/'+REPO+'/commit/'+SOURCE+'). Release packages are the exact validated CI artifacts.\n'
assert release['draft'],'Release already published; inspect instead of modifying it'
updated=api('releases/'+str(release['id']),'PATCH',{'name':TITLE,'body':notes,'target_commitish':SOURCE,'draft':False,'prerelease':PRE,'make_latest':'false' if PRE else 'true'})
assert updated['tag_name']==TAG and not updated['draft'] and updated['prerelease']==PRE
check_tag();final=api('releases/'+str(release['id']));assert {a['name']:a['digest'] for a in final['assets']}=={n:a['digest'] for n,a in assets.items()}
receipt={'source':SOURCE,'tag':TAG,'run':run,'jobs':jobs,'artifacts':artifact_receipts,'package_entries':package_entries,'hashes':hashes,'release':final}
Path('release-receipt-'+STREAM+'.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('PUBLISHED',final['html_url'],flush=True)
with open(os.environ['GITHUB_STEP_SUMMARY'],'a') as f:f.write('## '+TITLE+'\n\n'+final['html_url']+'\n\nSource: `'+SOURCE+'`\n\nCI: '+run['html_url']+'\n')
