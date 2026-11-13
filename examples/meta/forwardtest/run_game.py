#!/usr/bin/env python3
import argparse,os,pathlib,subprocess
p=argparse.ArgumentParser();p.add_argument('--drop-reply',action='store_true');p.add_argument('--packed',action='store_true');a=p.parse_args()
r=pathlib.Path(__file__).resolve().parent;t=r/'Builds/.temp';e=dict(os.environ)
e.update(META_PLAYER_TOKEN_FILE=str(t/'private/player-token'),META_OUTBOX_FILE=str(t/'private/player-outbox.json'),META_RESULT_FILE=str(t/'evidence/client-view.json'),AIVERSE_AUDIO='off',AIVERSE_SPLASH='off',AIVERSE_EXIT_AFTER_FRAMES='240',AIVERSE_SCREENSHOT=str(t/'evidence/game.ppm'))
if a.drop_reply:e['META_DROP_REPLY']='1'
else:e.pop('META_DROP_REPLY',None)
if a.packed:e['AIVERSE_CONTENT']=str(t/'packed-content')
else:e.pop('AIVERSE_CONTENT',None)
name='client-lost-reply.log' if a.drop_reply else ('client-packed.log' if a.packed else 'client-restart.log')
with (t/'evidence'/name).open('w') as log:
 subprocess.run([str(t/'client/engine/AiverseGame.app/Contents/MacOS/AiverseGame'),'--map','0','--spectate'],env=e,stdout=log,stderr=log,check=True)
output=(t/'evidence'/name).read_text()
expected='META_PENDING_RETRY' if a.drop_reply else 'META_ACCEPTANCE_PASS'
assert expected in output, output
print(output)
