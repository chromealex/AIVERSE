#!/usr/bin/env python3
"""Fit a post-process colour LUT that maps an engine capture onto a reference frame.

Usage: fit_lut.py capture.ppm reference.png out_lut.png [--size 32] [--border 0.06]

The capture and the reference must show the same camera (pixel-aligned within a few pixels).
Both are box-downsampled to 320x180 so small misalignments average out; a border is ignored
(reference vignettes, letterboxing). The fit has two parts, so contrast is not flattened:
  1. a monotonic tone curve from luminance histogram matching (brights stay bright);
  2. a 17^3 chroma-only correction (luminance removed from the residual), regularised by a
     Laplacian so unobserved colours stay close to their neighbours.
Fit on a capture made WITHOUT vignette/grain (they follow the LUT in the postfx chain), with the
same exposure/grade as the target recipe. Output: N squares of N x N (red across, green down,
blue square by square) for the postfx `lut` effect. A preview of the capture is written next to it.
This is scene/camera colour matching (like a colourist's grade), not a lighting fix: it cannot
separate two surfaces that render the same colour but differ in the reference.
"""
import argparse,numpy as np
from PIL import Image
ap=argparse.ArgumentParser();ap.add_argument('capture');ap.add_argument('reference');ap.add_argument('out')
ap.add_argument('--size',type=int,default=32);ap.add_argument('--border',type=float,default=0.06)
args=ap.parse_args();ours_p,ref_p,out_p=args.capture,args.reference,args.out;N=17;BORDER=args.border
W,H=320,180
lum=lambda c:c[...,0]*.2126+c[...,1]*.7152+c[...,2]*.0722
a=np.asarray(Image.open(ours_p).convert('RGB').resize((W,H),Image.BOX),float)/255
b=np.asarray(Image.open(ref_p).convert('RGB').resize((W,H),Image.BOX),float)/255
m=np.zeros((H,W),bool);m[int(H*BORDER):int(H*(1-BORDER)),int(W*BORDER):int(W*(1-BORDER))]=True
# 1) monotonic tone curve by luminance histogram matching (keeps brights bright)
la=np.sort(lum(a[m]));lb=np.sort(lum(b[m]));q=np.linspace(0,1,257)
xa=np.quantile(la,q);yb=np.quantile(lb,q)
xa=np.maximum.accumulate(xa+np.arange(257)*1e-7)
def tone(L):return np.interp(L,np.concatenate([[0],xa,[1]]),np.concatenate([[0],yb,[1]]))
def applyTone(c):
    L=lum(c);t=tone(L);s=np.where(L>1e-4,t/np.maximum(L,1e-4),1)
    return np.clip(np.where(L[...,None]>1e-4,c*s[...,None],t[...,None]),0,1)
at=applyTone(a)
# 2) chroma-only 3D correction: luminance component removed from the delta
x=at[m];d=b[m]-x;d=d-lum(d)[:,None]
acc=np.zeros((N,N,N,3));wsum=np.zeros((N,N,N))
g=x*(N-1);i0=np.clip(np.floor(g).astype(int),0,N-2);f=g-i0
for dz in (0,1):
  for dy in (0,1):
    for dx in (0,1):
      w=(f[:,0] if dx else 1-f[:,0])*(f[:,1] if dy else 1-f[:,1])*(f[:,2] if dz else 1-f[:,2])
      idx=(i0[:,0]+dx,i0[:,1]+dy,i0[:,2]+dz);np.add.at(wsum,idx,w);np.add.at(acc,idx,w[:,None]*d)
obs=np.where(wsum[...,None]>0,acc/np.maximum(wsum[...,None],1e-9),0);conf=wsum/(wsum+2.0);delta=obs.copy()
for it in range(400):
    p=np.pad(delta,((1,1),(1,1),(1,1),(0,0)),mode='edge')
    nb=(p[2:,1:-1,1:-1]+p[:-2,1:-1,1:-1]+p[1:-1,2:,1:-1]+p[1:-1,:-2,1:-1]+p[1:-1,1:-1,2:]+p[1:-1,1:-1,:-2])/6
    delta=conf[...,None]*np.where(wsum[...,None]>0,obs,nb)+(1-conf[...,None])*nb*.995
def chroma(c):
    n=N;g=np.clip(c,0,1)*(n-1);i0=np.clip(np.floor(g).astype(int),0,n-2);f=g-i0;out=np.zeros_like(c)
    for dz in (0,1):
      for dy in (0,1):
        for dx in (0,1):
          w=(f[...,0] if dx else 1-f[...,0])*(f[...,1] if dy else 1-f[...,1])*(f[...,2] if dz else 1-f[...,2])
          out+=w[...,None]*delta[i0[...,0]+dx,i0[...,1]+dy,i0[...,2]+dz]
    return np.clip(c+out,0,1)
full=lambda c:chroma(applyTone(c))
S=args.size;r=np.linspace(0,1,S);B,G,R=np.meshgrid(r,r,r,indexing='ij');lut=full(np.stack([R,G,B],-1))
strip=np.zeros((S,S*S,3))
for bi in range(S):strip[:,bi*S:(bi+1)*S]=lut[bi]
Image.fromarray((strip*255+.5).astype(np.uint8)).save(out_p)
img=np.asarray(Image.open(ours_p).convert('RGB'),float)/255
Image.fromarray((full(img)*255+.5).astype(np.uint8)).save(out_p.rsplit('.',1)[0]+'_preview.png')
