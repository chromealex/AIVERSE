#!/usr/bin/env python3
"""Measure consecutive-frame RGB differences in a frozen scene (not moving-camera quality)."""
import argparse
import json
from pathlib import Path


def read(path):
    header=path.read_bytes().split(b'\n',3)
    if header[0]!=b'P6' or header[2]!=b'255':raise ValueError('expected RGB8 PPM')
    w,h=map(int,header[1].split());data=header[3]
    if len(data)!=w*h*3:raise ValueError('invalid pixels')
    return w,h,data


def measure(root):
    frames=[read(root/f'0-{i}.ppm') for i in range(16)]
    w,h,_=frames[0]
    regions={'full':(0,0,w,h),'distant':(w*3//8,h//12,w*3//4,h//2)}
    result={}
    for name,(x0,y0,x1,y1) in regions.items():
        rows=[]
        for fw,fh,data in frames:
            if (fw,fh)!=(w,h):raise ValueError('resolution changed')
            rows.append(b''.join(data[(y*w+x0)*3:(y*w+x1)*3] for y in range(y0,y1)))
        mae=[]
        for a,b in zip(rows,rows[1:]):mae.append(sum(abs(x-y) for x,y in zip(a,b))/len(a))
        result[name]={'meanFrameDifferenceRgb8':sum(mae)/len(mae),'maxFrameDifferenceRgb8':max(mae)}
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('sequence',type=Path);a=p.parse_args()
    print(json.dumps(measure(a.sequence),indent=2))
