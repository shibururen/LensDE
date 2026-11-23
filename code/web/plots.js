const bands = [
  {label: 'R', wavelength: 0.65627250, color: '#CC0000'},
  {label: 'G', wavelength: 0.58756180, color: '#006600'},
  {label: 'B', wavelength: 0.48613270, color: '#0066CC'},
];

export function parseCsv(text) {
  const rows = [];
  let row = [], value = '', quoted = false;
  for (let index = 0; index < text.length; index++) {
    const char = text[index];
    if (char === '"') {
      if (quoted && text[index + 1] === '"') {
        value += '"';
        index++;
      } else {
        quoted = !quoted;
      }
    } else if (char === ',' && !quoted) {
      row.push(value);
      value = '';
    } else if ((char === '\n' || char === '\r') && !quoted) {
      if (char === '\r' && text[index + 1] === '\n') index++;
      row.push(value);
      if (row.some(cell => cell !== '')) rows.push(row);
      row = [];
      value = '';
    } else {
      value += char;
    }
  }
  if (quoted) throw new Error('Unclosed CSV quote');
  if (value !== '' || row.length) {
    row.push(value);
    rows.push(row);
  }
  const headers = (rows.shift() ?? []).map(value => value.replace(/^\uFEFF/, ''));
  return {headers, rows};
}

function wavelengthBand(wavelength) {
  return bands.reduce((nearest, band) =>
    Math.abs(band.wavelength - wavelength) < Math.abs(nearest.wavelength - wavelength) ? band : nearest);
}

function centroid(points) {
  let x = 0, y = 0, weight = 0;
  for (const point of points) {
    x += point.x * point.weight;
    y += point.y * point.weight;
    weight += point.weight;
  }
  return weight > 0 ? {x: x / weight, y: y / weight} : null;
}

function spotStats(points) {
  const green = points.filter(point => point.band.label === 'G');
  const center = centroid(green.length ? green : points);
  if (!center) return null;

  const byBand = new Map();
  let maxRadius = 0;
  for (const point of points) {
    const distance = Math.hypot(point.x - center.x, point.y - center.y);
    maxRadius = Math.max(maxRadius, distance);
    if (!byBand.has(point.band)) byBand.set(point.band, {sum: 0, weight: 0});
    const totals = byBand.get(point.band);
    totals.sum += point.weight * distance * distance;
    totals.weight += point.weight;
  }
  // Match the original workbench's average of the weighted RGB-band RMS values.
  const perBand = [...byBand.values()].filter(band => band.weight > 0);
  const rms = perBand.reduce((sum, band) => sum + Math.sqrt(band.sum / band.weight), 0) / perBand.length;
  return {center, rms, span: Math.max(maxRadius, rms * 1.35, 1e-6)};
}

export function spotData(text, wavelengths = []) {
  const {headers, rows} = parseCsv(text);
  const [hxIndex, hyIndex, waveIndex, xIndex, yIndex, validIndex, weightIndex] =
    ['field_hx', 'field_hy', 'wavelength_um', 'x_mm', 'y_mm', 'valid', 'pupil_weight']
      .map(name => headers.indexOf(name));
  if ([hxIndex, hyIndex, waveIndex, xIndex, yIndex, validIndex].includes(-1)) {
    throw new Error('Missing native spot CSV columns');
  }
  const fields = new Map(), waveInfo = new Map();
  for (const row of rows) {
    if (!(Number(row[validIndex]) > 0)) continue;
    const x = Number(row[xIndex]), y = Number(row[yIndex]);
    if (!row[xIndex]?.trim() || !row[yIndex]?.trim() || !Number.isFinite(x) || !Number.isFinite(y)) continue;
    const hx = Number(row[hxIndex]), hy = Number(row[hyIndex]), wave = Number(row[waveIndex]);
    if (![hx, hy, wave].every(Number.isFinite)) continue;
    if (!waveInfo.has(wave)) {
      const setting = wavelengths.find(entry => Math.abs(Number(entry.value_um) - wave) < 1e-7);
      waveInfo.set(wave, {band: wavelengthBand(wave), weight: Math.max(0, Number(setting?.weight ?? 1))});
    }
    const info = waveInfo.get(wave);
    const pupilWeight = row[weightIndex]?.trim() ? Number(row[weightIndex]) : 1;
    const weight = Math.max(0, Number.isFinite(pupilWeight) ? pupilWeight : 1) * info.weight;
    const key = hx + '|' + hy;
    if (!fields.has(key)) fields.set(key, {hx, hy, points: []});
    fields.get(key).points.push({x, y, wave, weight, band: info.band});
  }
  const groups = [];
  for (const field of fields.values()) {
    const stats = spotStats(field.points);
    if (stats) groups.push({...field, ...stats});
  }
  return {groups, wavelengths: waveInfo, sharedSpan: Math.max(1e-6, ...groups.map(group => group.span))};
}

export async function loadSpotData(source, wavelengths) {
  const response = await fetch(source);
  if (!response.ok) throw new Error('Spot data: HTTP ' + response.status);
  return spotData(await response.text(), wavelengths);
}

const compact = value => String(Number(value.toPrecision(3)));
const lengthLabel = mm => Math.abs(mm) >= 0.1 ? compact(mm) + ' mm' : compact(mm * 1000) + ' µm';

function spotCanvas(group, scale, label) {
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = 400;
  canvas.setAttribute('role', 'img');
  canvas.setAttribute('aria-label', label);
  const ctx = canvas.getContext('2d');
  ctx.scale(2, 2);

  ctx.lineWidth = 1;
  ctx.strokeStyle = '#cbd5e1';
  ctx.beginPath();
  ctx.moveTo(18, 100);
  ctx.lineTo(182, 100);
  ctx.moveTo(100, 18);
  ctx.lineTo(100, 182);
  ctx.stroke();

  ctx.globalAlpha = 0.66;
  let lastBand;
  for (const point of group.points) {
    if (point.band !== lastBand) {
      ctx.fillStyle = point.band.color;
      lastBand = point.band;
    }
    ctx.beginPath();
    ctx.arc(100 + (point.x - group.center.x) * scale, 100 - (point.y - group.center.y) * scale, 1.9, 0, 2 * Math.PI);
    ctx.fill();
  }

  ctx.globalAlpha = 1;
  // Keep the RMS circle legible when dense rays fill the entire field.
  const rmsRadius = Math.max(2, group.rms * scale);
  ctx.setLineDash([4, 3]);
  ctx.beginPath();
  ctx.arc(100, 100, rmsRadius, 0, 2 * Math.PI);
  ctx.strokeStyle = '#fff';
  ctx.lineWidth = 3.5;
  ctx.stroke();
  ctx.strokeStyle = '#111827';
  ctx.lineWidth = 1.45;
  ctx.stroke();
  ctx.setLineDash([]);

  const target = 46 / scale;
  const power = 10 ** Math.floor(Math.log10(target));
  const normalized = target / power;
  const length = power * (normalized >= 5 ? 5 : normalized >= 2 ? 2 : 1);
  const end = 20 + length * scale;
  ctx.fillStyle = ctx.strokeStyle = '#334155';
  ctx.font = '10px system-ui, sans-serif';
  ctx.fillText(lengthLabel(length), 20, 177);
  ctx.lineWidth = 1.2;
  ctx.beginPath();
  ctx.moveTo(20, 184);
  ctx.lineTo(end, 184);
  ctx.moveTo(20, 181);
  ctx.lineTo(20, 187);
  ctx.moveTo(end, 181);
  ctx.lineTo(end, 187);
  ctx.stroke();
  return canvas;
}

export function renderSpotDiagrams(grid, legend, data, {field, rays, rmsInfo, scaleMode}) {
  const plots = document.createDocumentFragment(), keys = document.createDocumentFragment();
  for (const [wave, {band}] of data.groups.length ? data.wavelengths : []) {
    const key = document.createElement('span');
    const swatch = document.createElement('i');
    swatch.style.backgroundColor = band.color;
    key.title = String(wave) + ' µm';
    key.append(swatch, Number(wave.toPrecision(4)) + ' µm');
    keys.append(key);
  }
  data.groups.forEach((group, index) => {
    const card = document.createElement('article');
    card.className = 'spot-card';
    const heading = document.createElement('h3');
    heading.textContent = field + ' ' + (index + 1);
    const rms = document.createElement('strong');
    rms.className = 'spot-rms';
    rms.textContent = 'RMS ' + lengthLabel(group.rms);
    rms.title = rmsInfo;
    const details = document.createElement('p');
    details.className = 'spot-meta';
    details.textContent = 'Hx ' + compact(group.hx) + ' · Hy ' + compact(group.hy) + ' · ' + group.points.length + ' ' + rays;
    const scale = 82 / (scaleMode === 'fit' ? group.span : data.sharedSpan);
    card.append(heading, rms, spotCanvas(group, scale, heading.textContent + ' · ' + rms.textContent), details);
    plots.append(card);
  });
  legend.replaceChildren(keys);
  grid.replaceChildren(plots);
}
