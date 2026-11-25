async page => {
  await page.setViewportSize({width: 800, height: 500});
  const texture = page.getByRole('button', {name: 'Diffuse textures'});
  const lightmap = page.getByRole('button', {name: 'Baked lightmaps'});
  await texture.waitFor();
  async function capture() {
    await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() =>
      requestAnimationFrame(() => requestAnimationFrame(resolve)))));
    const image = await page.screenshot({clip: {x: 100, y: 75, width: 600, height: 350}});
    return image.toString('base64');
  }
  const base = await capture();
  await texture.click();
  if (await texture.getAttribute('aria-pressed') !== 'false') throw Error('Diffuse control did not toggle');
  const flat = await capture();
  await texture.click();
  await lightmap.click();
  if (await lightmap.getAttribute('aria-pressed') !== 'false') throw Error('Lightmap control did not toggle');
  const unbaked = await capture();
  await lightmap.click();
  await page.mouse.move(411,214); await page.mouse.down();
  await page.mouse.move(453,221,{steps:6}); await page.mouse.up();
  const orbit = await capture();
  return await page.evaluate(async screenshots => {
    const pixels = await Promise.all(screenshots.map(async encoded => {
      const bytes = Uint8Array.from(atob(encoded), c => c.charCodeAt(0));
      const bitmap = await createImageBitmap(new Blob([bytes], {type: 'image/png'}));
      const canvas = new OffscreenCanvas(bitmap.width, bitmap.height);
      const ctx = canvas.getContext('2d');
      ctx.drawImage(bitmap, 0, 0); bitmap.close();
      return ctx.getImageData(0, 0, canvas.width, canvas.height).data;
    }));
    const [baseline, ...variants] = pixels;
    const counts = variants.map(image => {
      let changed = 0;
      for (let i = 0; i < image.length; i += 4)
        if ([0, 1, 2].some(k => Math.abs(image[i+k]-baseline[i+k])>3)) changed++;
      return changed;
    });
    const sampled = baseline.length / 4;
    if (counts.some(n => n < sampled / 50)) throw Error('Insufficient optical response: '+counts.join(', '));
    return {sampled, diffuse:counts[0], lightmap:counts[1], orbit:counts[2]};
  }, [base, flat, unbaked, orbit]);
}
