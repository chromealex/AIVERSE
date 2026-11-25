/** Attachment transforms (column-major 4x4, as AttachmentLayoutCodec stores them) <-> offset, yaw (degrees), uniform scale. */
export interface Trs { t: [number, number, number]; yaw: number; scale: number; custom: boolean }
export const IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];

export function fromTrs(t: [number, number, number], yawDeg: number, scale: number): number[] {
  const a = yawDeg * Math.PI / 180, c = Math.cos(a) * scale, s = Math.sin(a) * scale;
  // columns: X axis, Y axis, Z axis, translation (rotation about +Y)
  return [c, 0, -s, 0, 0, scale, 0, 0, s, 0, c, 0, t[0], t[1], t[2], 1].map(v => Math.abs(v) < 1e-9 ? 0 : +v.toFixed(6));
}

/** custom=true when the matrix has tilt or non-uniform scale (the panel then keeps it as is unless edited). */
export function toTrs(m: number[] | undefined | null): Trs {
  const x = m && m.length === 16 ? m : IDENTITY;
  const scale = Math.hypot(x[0], x[1], x[2]);
  const yaw = Math.atan2(x[8], x[0]) * 180 / Math.PI;
  const back = fromTrs([x[12], x[13], x[14]], yaw, scale || 1);
  const custom = x.some((v, i) => Math.abs(v - back[i]) > 1e-4);
  return { t: [x[12], x[13], x[14]], yaw: +yaw.toFixed(3), scale: +(scale || 1).toFixed(4), custom };
}

export function editAffine(matrix:number[],column:number,row:number,value:number):number[] {
 const next=[...matrix];
 next[column*4+row]=value;
 return next;
}
