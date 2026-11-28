export const physisLayers=['Temperature (°C)','Relative humidity','Pressure (hPa)','Rain','Snow','Surface wetness','Snowpack (m)','Surface ice','Wind speed (m/s)','Vertical wind (m/s)','Gravity (m/s²)','Fire','Quake','Local flood (m)'];

export function isPhysisLayer(value: unknown): value is number {
  return typeof value === "number" && Number.isInteger(value) && value >= 0 && value < physisLayers.length;
}
