#!/usr/bin/env python3
"""Build a self-contained stick-figure preview from MotionWeave joint CSV."""
import csv
import json
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
bones = (("root", "hips"), ("hips", "spine"), ("spine", "chest"),
         ("chest", "upperChest"), ("upperChest", "neck"), ("neck", "head"),
         ("hips", "thighL"), ("thighL", "shinL"), ("shinL", "footL"), ("footL", "toeL"),
         ("hips", "thighR"), ("thighR", "shinR"), ("shinR", "footR"), ("footR", "toeR"),
         ("upperChest", "clavicleL"), ("clavicleL", "upperArmL"),
         ("upperArmL", "foreArmL"), ("foreArmL", "handL"),
         ("upperChest", "clavicleR"), ("clavicleR", "upperArmR"),
         ("upperArmR", "foreArmR"), ("foreArmR", "handR"))
frames = [{name: [float(row[name + axis]) for axis in "XYZ"] for name in names}
          for row in rows]
html = '''<!doctype html><html lang="ru"><meta charset="utf-8">
<title>MotionWeave · GeneratedWalk preview</title>
<style>body{margin:0;background:#f3efe8;color:#141210;font:16px system-ui;display:grid;place-items:center;min-height:100vh}
main{max-width:850px;padding:24px}h1{font-size:28px}svg{width:100%;background:#fff;border:1px solid #d7ccc0}
button,input{accent-color:#a45b2a;font:inherit}small{display:block;color:#625d56;margin-top:12px}</style>
<main><h1>MotionWeave · сгенерированная ходьба</h1>
<svg viewBox="0 0 800 500" aria-label="Анимация скелета, вид сбоку"></svg>
<p><button id="toggle">Пауза</button> <input id="seek" type="range" min="0" max="120" value="0" style="width:65%"> <span id="time"></span></p>
<small>Два цикла по 1 секунде · тестовый 53-суставный скелет · вид сбоку (+Z вперёд). Это вычисленные позы клипа, не кадры Metal. Стопы пока скользят.</small></main>
<script>const frames=FRAMES,bones=BONES,svg=document.querySelector('svg'),seek=document.querySelector('#seek');
let frame=0,playing=true,last=performance.now();
function point(p){return [400+p[2]*210,450-p[1]*210]}
function line(a,b,color,width=5){let x=point(a),y=point(b);return `<line x1="${x[0]}" y1="${x[1]}" x2="${y[0]}" y2="${y[1]}" stroke="${color}" stroke-width="${width}" stroke-linecap="round"/>`}
function draw(){let p=frames[frame];let s='<line x1="0" y1="450" x2="800" y2="450" stroke="#b9afa5" stroke-width="2"/>';
for(let [a,b] of bones){let color=b.endsWith('L')?'#3574c2':b.endsWith('R')?'#cc5d42':'#302a27';s+=line(p[a],p[b],color)}
let h=point(p.head);s+=`<circle cx="${h[0]}" cy="${h[1]-19}" r="19" fill="none" stroke="#302a27" stroke-width="4"/>`;
svg.innerHTML=s;seek.value=frame;document.querySelector('#time').textContent=(frame/60).toFixed(2)+' с'}
document.querySelector('#toggle').onclick=()=>{playing=!playing;document.querySelector('#toggle').textContent=playing?'Пауза':'Играть'};
seek.oninput=()=>{frame=+seek.value;playing=false;document.querySelector('#toggle').textContent='Играть';draw()};
function tick(now){if(playing){let n=Math.floor((now-last)/1000*60);if(n>0){frame=(frame+n)%frames.length;last=now;draw()}}else last=now;requestAnimationFrame(tick)}
draw();requestAnimationFrame(tick);</script></html>'''
html = html.replace("FRAMES", json.dumps(frames, separators=(",", ":")))
html = html.replace("BONES", json.dumps(bones, separators=(",", ":")))
output.write_text(html, encoding="utf-8")
print(output)

if len(sys.argv) > 3:
    sheet = Path(sys.argv[3])
    width, height = 1200, 680
    pixels = bytearray([243, 239, 232] * width * height)

    def put(x, y, color, radius=2):
        for yy in range(max(0, y-radius), min(height, y+radius+1)):
            for xx in range(max(0, x-radius), min(width, x+radius+1)):
                at = (yy*width+xx)*3
                pixels[at:at+3] = bytes(color)

    def draw(a, b, color, radius=2):
        steps = max(abs(b[0]-a[0]), abs(b[1]-a[1]), 1)
        for j in range(steps+1):
            u = j/steps
            put(round(a[0]*(1-u)+b[0]*u), round(a[1]*(1-u)+b[1]*u), color, radius)

    for slot in range(8):
        pose = frames[slot*8]
        col, row = slot % 4, slot // 4
        ox, ground = 150+col*300, 320+row*330
        draw((ox-120, ground), (ox+120, ground), (184, 174, 164), 1)
        def projected(name):
            p = pose[name]
            return round(ox+p[2]*105), round(ground-p[1]*105)
        for a, b in bones:
            color = (52, 112, 194) if b.endswith("L") else (205, 92, 66) if b.endswith("R") else (45, 40, 37)
            draw(projected(a), projected(b), color)
        head = projected("head")
        for degrees in range(0, 360, 2):
            import math
            theta = math.radians(degrees)
            put(round(head[0]+10*math.cos(theta)), round(head[1]-10+10*math.sin(theta)), (45, 40, 37), 1)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind+data))
    scanlines = b"".join(b"\0"+pixels[y*width*3:(y+1)*width*3] for y in range(height))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">2I5B", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(scanlines, 9)) + chunk(b"IEND", b"")
    sheet.write_bytes(png)
    print(sheet)
