#!/usr/bin/env python3
"""Self-contained 3D canvas preview from evaluated MotionWeave joint CSV."""
import csv
import json
import math
import struct
import sys
import zlib
from pathlib import Path

source, output = map(Path, sys.argv[1:3])
with source.open(newline="") as stream:
    rows = list(csv.DictReader(stream))
names = ("root", "hips", "spine", "chest", "upperChest", "neck", "head",
         "thighL", "shinL", "footL", "toeL", "thighR", "shinR", "footR", "toeR",
         "clavicleL", "upperArmL", "foreArmL", "handL",
         "clavicleR", "upperArmR", "foreArmR", "handR")
frames = [{name: [float(row[name + axis]) for axis in "XYZ"] for name in names}
          for row in rows]
html = r'''<!doctype html><html lang="ru"><meta charset="utf-8">
<title>MotionWeave · ходьба в 3D с root motion</title>
<style>html,body{margin:0;height:100%;background:#f3efe8;color:#141210;font:15px system-ui}main{max-width:1100px;margin:auto;padding:20px}
h1{font-size:24px;margin:0 0 12px}canvas{width:100%;height:620px;display:block;background:#e8e3da;border:1px solid #bbb0a4}
button,input{font:inherit;accent-color:#a75b32}p{margin:10px 0}small{color:#655b52}</style>
<main><h1>MotionWeave · ходьба с root motion</h1><canvas id="view" width="1100" height="620"></canvas>
<p><button id="toggle">Пауза</button> <input id="seek" type="range" min="0" max="120" value="0" style="width:65%"> <span id="label"></span></p>
<small>Перетащите изображение для поворота камеры. Синяя левая сторона, красная правая. Пунктир показывает траекторию корня. 3D-визуализация вычисленных поз без меша и Metal-рендера.</small></main>
<script>
const frames=FRAMES, canvas=document.querySelector('#view'), ctx=canvas.getContext('2d'), seek=document.querySelector('#seek');
const bones=[['root','hips'],['hips','spine'],['spine','chest'],['chest','upperChest'],['upperChest','neck'],['neck','head'],
 ['hips','thighL'],['thighL','shinL'],['shinL','footL'],['footL','toeL'],['hips','thighR'],['thighR','shinR'],['shinR','footR'],['footR','toeR'],
 ['upperChest','clavicleL'],['clavicleL','upperArmL'],['upperArmL','foreArmL'],['foreArmL','handL'],
 ['upperChest','clavicleR'],['clavicleR','upperArmR'],['foreArmR','handR'],['upperArmR','foreArmR']];
let frame=0,playing=true,last=performance.now(),yaw=.60,elev=.34,drag=false,lastX=0;
function project(p,focus){let dx=p[0]-focus[0],dy=p[1],dz=p[2]-focus[2];
 let side=dx*Math.cos(yaw)-dz*Math.sin(yaw),depth=dx*Math.sin(yaw)+dz*Math.cos(yaw);
 let vertical=dy*Math.cos(elev)-depth*Math.sin(elev);return [550+side*245,480-vertical*245,depth]}
function stroke(a,b,color,width){ctx.beginPath();ctx.moveTo(a[0],a[1]);ctx.lineTo(b[0],b[1]);ctx.strokeStyle=color;ctx.lineWidth=width;ctx.lineCap='round';ctx.stroke()}
function draw(){let p=frames[frame],focus=p.root;ctx.clearRect(0,0,1100,620);
 let grad=ctx.createLinearGradient(0,0,0,620);grad.addColorStop(0,'#e7e0d6');grad.addColorStop(1,'#f8f4ed');ctx.fillStyle=grad;ctx.fillRect(0,0,1100,620);
 for(let x=-3;x<=3;x+=.5)stroke(project([x,0,focus[2]-2],focus),project([x,0,focus[2]+3],focus),'#cec6bb',1);
 for(let z=-2;z<=3;z+=.5)stroke(project([-3,0,focus[2]+z],focus),project([3,0,focus[2]+z],focus),'#cec6bb',1);
 ctx.setLineDash([8,7]);stroke(project([0,.005,0],focus),project([0,.005,2.4],focus),'#8c744e',3);ctx.setLineDash([]);
 let shadow=project([focus[0],0,focus[2]],focus);ctx.fillStyle='#8c776633';ctx.beginPath();ctx.ellipse(shadow[0],shadow[1],44,13,0,0,Math.PI*2);ctx.fill();
 let segments=bones.map(([a,b])=>({a:project(p[a],focus),b:project(p[b],focus),name:b}));
 segments.sort((u,v)=>((u.a[2]+u.b[2])-(v.a[2]+v.b[2])));
 for(let s of segments){let color=s.name.endsWith('L')?'#3672bc':s.name.endsWith('R')?'#ce6248':'#34302e';
 stroke(s.a,s.b,'#4c454050',22);stroke(s.a,s.b,color,s.name.includes('thigh')||s.name.includes('shin')?16:12);
 ctx.fillStyle=color;ctx.beginPath();ctx.arc(s.b[0],s.b[1],s.name.includes('foot')?8:6,0,Math.PI*2);ctx.fill()}
 let head=project(p.head,focus);ctx.fillStyle='#383431';ctx.beginPath();ctx.ellipse(head[0],head[1]-16,18,22,0,0,Math.PI*2);ctx.fill();
 ctx.fillStyle='#141210';ctx.font='16px system-ui';ctx.fillText('Z →  '+focus[2].toFixed(2)+' м',24,34);
 seek.value=frame;document.querySelector('#label').textContent=(frame/60).toFixed(2)+' с · root Z '+focus[2].toFixed(2)+' м'}
canvas.onpointerdown=e=>{drag=true;lastX=e.clientX;canvas.setPointerCapture(e.pointerId)};
canvas.onpointerup=()=>drag=false;canvas.onpointermove=e=>{if(drag){yaw+=(e.clientX-lastX)*.008;lastX=e.clientX;draw()}};
document.querySelector('#toggle').onclick=()=>{playing=!playing;document.querySelector('#toggle').textContent=playing?'Пауза':'Играть'};
seek.oninput=()=>{frame=+seek.value;playing=false;document.querySelector('#toggle').textContent='Играть';draw()};
function tick(now){if(playing){let n=Math.floor((now-last)/1000*60);if(n>0){frame=(frame+n)%frames.length;last=now;draw()}}else last=now;requestAnimationFrame(tick)}
draw();requestAnimationFrame(tick);
</script></html>'''
output.write_text(html.replace("FRAMES", json.dumps(frames, separators=(",", ":"))), encoding="utf-8")
print(output)

if len(sys.argv) > 3:
    target = Path(sys.argv[3])
    width, height = 1200, 720
    pixels = bytearray([243, 239, 232] * width * height)

    def dot(x, y, color, radius=2):
        for yy in range(max(0, y-radius), min(height, y+radius+1)):
            for xx in range(max(0, x-radius), min(width, x+radius+1)):
                offset = (yy*width+xx)*3
                pixels[offset:offset+3] = bytes(color)

    def line(a, b, color, radius=2):
        steps = max(abs(b[0]-a[0]), abs(b[1]-a[1]), 1)
        for step in range(steps+1):
            t = step/steps
            dot(round(a[0]*(1-t)+b[0]*t), round(a[1]*(1-t)+b[1]*t), color, radius)

    yaw, elev = 0.60, 0.34
    def project(point, ox, oy, focus_z):
        x, y, z = point
        z -= focus_z
        side = x*math.cos(yaw)-z*math.sin(yaw)
        depth = x*math.sin(yaw)+z*math.cos(yaw)
        vertical = y*math.cos(elev)-depth*math.sin(elev)
        return round(ox+side*180), round(oy-vertical*180)

    for slot, index in enumerate((8, 24, 40, 56)):
        pose = frames[index]
        ox, oy = 150+slot*300, 520
        focus = pose["root"][2]
        for x in range(-2, 3):
            line(project([x*.5,0,focus-1],ox,oy,focus),
                 project([x*.5,0,focus+1.5],ox,oy,focus),(210,201,190),1)
        for z in range(-2, 4):
            line(project([-1,0,focus+z*.5],ox,oy,focus),
                 project([1,0,focus+z*.5],ox,oy,focus),(210,201,190),1)
        pairs = (("root","hips"),("hips","spine"),("spine","chest"),
                 ("chest","upperChest"),("upperChest","neck"),("neck","head"),
                 ("hips","thighL"),("thighL","shinL"),("shinL","footL"),("footL","toeL"),
                 ("hips","thighR"),("thighR","shinR"),("shinR","footR"),("footR","toeR"),
                 ("upperChest","clavicleL"),("clavicleL","upperArmL"),
                 ("upperArmL","foreArmL"),("foreArmL","handL"),
                 ("upperChest","clavicleR"),("clavicleR","upperArmR"),
                 ("upperArmR","foreArmR"),("foreArmR","handR"))
        for a,b in pairs:
            color = (53,110,187) if b.endswith("L") else (202,94,67) if b.endswith("R") else (50,45,42)
            line(project(pose[a],ox,oy,focus),project(pose[b],ox,oy,focus),color,6)
        head=project(pose["head"],ox,oy,focus)
        for angle in range(360):
            theta=math.radians(angle)
            dot(round(head[0]+11*math.cos(theta)),round(head[1]-12+14*math.sin(theta)),(50,45,42),2)

    def chunk(kind,data):
        return struct.pack(">I",len(data))+kind+data+struct.pack(">I",zlib.crc32(kind+data))
    rows_bytes=b"".join(b"\0"+pixels[y*width*3:(y+1)*width*3] for y in range(height))
    png=b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">2I5B",width,height,8,2,0,0,0))
    png+=chunk(b"IDAT",zlib.compress(rows_bytes,9))+chunk(b"IEND",b"")
    target.write_bytes(png)
    print(target)
