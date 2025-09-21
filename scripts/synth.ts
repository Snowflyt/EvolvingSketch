#!/usr/bin/env node
import { once } from "node:events";
import { createWriteStream } from "node:fs";
import { mkdir } from "node:fs/promises";
import { dirname } from "node:path";
import { parseArgs } from "node:util";

type Opts = {
  output: string;
  rows: number;
  products: number;
  days: number;
  alpha: number;
  startDate: string;
  seed: number;
  phases: number[]; // Phase proportions (will be normalized)
  drifts: number[]; // Daily drift intensity per phase (>= 0, can be > 1)
  driftWindows: number[]; // Swap window per phase in (0, 1]
};

const args = parseArgs({
  options: {
    output: { type: "string", short: "o" },
    rows: { type: "string", short: "n", default: "30000000" },
    products: { type: "string", short: "p", default: "40000" },
    days: { type: "string", short: "d", default: "720" },
    alpha: { type: "string", short: "a", default: "1.0" },
    startDate: { type: "string", default: "2023-01-01" },
    seed: { type: "string", short: "s", default: "42" },
    phases: { type: "string", default: "0.3,0.4,0.3" },
    drifts: { type: "string", default: "0.04,4,0.04" },
    driftWindows: { type: "string", default: "0.01,0.1,0.01" },
  },
  allowPositionals: false,
});

function toInt(name: string, v: string): number {
  const n = Number(v);
  if (!Number.isFinite(n) || !Number.isInteger(n)) {
    throw new Error(`Invalid integer for ${name}: ${v}`);
  }
  return n;
}
function toNum(name: string, v: string): number {
  const n = Number(v);
  if (!Number.isFinite(n)) throw new Error(`Invalid number for ${name}: ${v}`);
  return n;
}
function parseNumList(name: string, v: string): number[] {
  const arr = v.split(",").map((s) => Number(s.trim()));
  if (arr.length === 0 || arr.some((x) => !Number.isFinite(x))) {
    throw new Error(`Invalid list for ${name}: ${v}`);
  }
  return arr;
}

const opts: Opts = {
  output: args.values.output ?? "data/synthetic.csv",
  rows: toInt("rows", args.values.rows as string),
  products: toInt("products", args.values.products as string),
  days: toInt("days", args.values.days as string),
  alpha: toNum("alpha", args.values.alpha as string),
  startDate: args.values.startDate as string,
  seed: toInt("seed", args.values.seed as string),
  phases: parseNumList("phases", args.values.phases as string),
  drifts: parseNumList("drifts", args.values.drifts as string),
  driftWindows: parseNumList("driftWindows", args.values.driftWindows as string),
};

if (opts.products <= 1) throw new Error("products must be > 1");
if (opts.rows <= 0) throw new Error("rows must be > 0");
if (opts.days <= 0) throw new Error("days must be > 0");
if (opts.alpha <= 0) throw new Error("alpha must be > 0");
if (!/^\d{4}-\d{2}-\d{2}$/.test(opts.startDate)) throw new Error("startDate must be YYYY-MM-DD");

// Validate and normalize phases; also validate lengths/ranges of drifts/driftWindows
(function validatePhaseParams() {
  const m = opts.phases.length;
  if (opts.drifts.length !== m || opts.driftWindows.length !== m) {
    throw new Error("phases, drifts, driftWindows must have the same length");
  }
  if (opts.drifts.some((x) => x < 0)) {
    throw new Error("drifts must be >= 0");
  }
  if (opts.driftWindows.some((w) => !(w > 0 && w <= 1))) {
    throw new Error("driftWindows must be in (0,1]");
  }
  const sum = opts.phases.reduce((a, b) => a + b, 0);
  if (!(sum > 0)) throw new Error("sum(phases) must be > 0");
  // Normalize to sum to 1 (even if user-specified values do not)
  opts.phases = opts.phases.map((x) => x / sum);
})();

// Mulberry32 PRNG for deterministic runs
function mulberry32(seed: number): () => number {
  let t = seed >>> 0;
  return () => {
    t += 0x6d2b79f5;
    let r = Math.imul(t ^ (t >>> 15), 1 | t);
    r ^= r + Math.imul(r ^ (r >>> 7), 61 | r);
    return ((r ^ (r >>> 14)) >>> 0) / 4294967296;
  };
}

function randInt(rng: () => number, min: number, max: number): number {
  return Math.floor(rng() * (max - min + 1)) + min;
}

function buildZipfCDF(n: number, alpha: number): Float64Array {
  const cdf = new Float64Array(n);
  let sum = 0;
  for (let r = 1; r <= n; r++) {
    sum += 1 / Math.pow(r, alpha);
    cdf[r - 1] = sum;
  }
  for (let i = 0; i < n; i++) cdf[i] /= sum;
  return cdf;
}

function sampleRank(cdf: Float64Array, u: number): number {
  let lo = 0,
    hi = cdf.length - 1;
  while (lo < hi) {
    const mid = (lo + hi) >>> 1;
    if (u <= cdf[mid]) hi = mid;
    else lo = mid + 1;
  }
  return lo;
}

function fisherYates(n: number, rng: () => number): Uint32Array {
  const arr = new Uint32Array(n);
  for (let i = 0; i < n; i++) arr[i] = i;
  for (let i = n - 1; i > 0; i--) {
    const j = randInt(rng, 0, i);
    const tmp = arr[i];
    arr[i] = arr[j];
    arr[j] = tmp;
  }
  return arr;
}

// Continuous drift: perform a number of small local swaps with reflection at boundaries.
// The maximum step size is controlled by windowFraction.
function applyDriftSteps(
  permutation: Uint32Array,
  steps: number,
  rng: () => number,
  windowFraction: number
): void {
  if (steps <= 0) return;
  const n = permutation.length;
  const maxDist = Math.max(1, Math.floor((n - 1) * windowFraction));
  for (let k = 0; k < steps; k++) {
    const i = randInt(rng, 0, n - 1);
    // Prefer short moves (triangular distribution via min of two uniforms)
    const a = randInt(rng, 1, maxDist);
    const b = randInt(rng, 1, maxDist);
    const step = Math.min(a, b);
    const sign = rng() < 0.5 ? -1 : 1;
    let j = i + sign * step;
    // Reflect at boundaries to reduce endpoint bias
    if (j < 0) j = -j;
    if (j >= n) j = 2 * (n - 1) - j;
    if (j === i) j = Math.min(n - 1, i + 1);
    const tmp = permutation[i];
    permutation[i] = permutation[j];
    permutation[j] = tmp;
  }
}

// Assign a phase index to each day using the largest remainder method (smooth allocation)
function assignPhasesToDays(totalDays: number, phases: number[]): Int16Array {
  const m = phases.length;
  const raw = phases.map((p) => p * totalDays);
  const base = raw.map((x) => Math.floor(x));
  let used = base.reduce((a, b) => a + b, 0);
  let remain = totalDays - used;

  const idx = Array.from({ length: m }, (_, i) => i);
  idx.sort((i, j) => raw[i] - base[i] - (raw[j] - base[j])).reverse();
  for (let k = 0; k < m && remain > 0; k++, remain--) {
    base[idx[k]]++;
  }
  // If totalDays < m, some phases may get 0 days; this is acceptable.

  const dayPhase = new Int16Array(totalDays);
  let pos = 0;
  for (let i = 0; i < m; i++) {
    const cnt = base[i];
    for (let t = 0; t < cnt && pos < totalDays; t++) {
      dayPhase[pos++] = i as number;
    }
  }
  // Fallback: if not filled due to rounding, pad with the last phase
  while (pos < totalDays) dayPhase[pos++] = (m - 1) as number;
  return dayPhase;
}

function parseStartDateUTC(s: string): Date {
  const [Y, M, D] = s.split("-").map(Number);
  return new Date(Date.UTC(Y, M - 1, D));
}
function addDaysUTC(d: Date, days: number): Date {
  const nd = new Date(d.getTime());
  nd.setUTCDate(nd.getUTCDate() + days);
  return nd;
}
function fmtDateUTC(d: Date): string {
  const Y = d.getUTCFullYear();
  const M = (d.getUTCMonth() + 1).toString().padStart(2, "0");
  const D = d.getUTCDate().toString().padStart(2, "0");
  return `${Y}-${M}-${D}`;
}

async function ensureDirForFile(path: string) {
  await mkdir(dirname(path), { recursive: true });
}

// Sample a positive integer length with given expectation using a geometric-like distribution
function geomLen(mean: number, rng: () => number): number {
  const p = 1 / Math.max(1, mean);
  let u = 0;
  do {
    u = rng();
  } while (u <= 1e-12);
  return 1 + Math.floor(Math.log(1 - u) / Math.log(1 - p));
}

async function main() {
  // Independent RNGs: decouple drawing and drifting
  const drawRng = mulberry32(opts.seed);
  const driftRng = mulberry32(opts.seed ^ 0x9e3779b9);

  const cdf = buildZipfCDF(opts.products, opts.alpha);
  const perm = fisherYates(opts.products, drawRng);

  await ensureDirForFile(opts.output);
  const ws = createWriteStream(opts.output, { encoding: "utf8", highWaterMark: 1 << 20 });

  if (!ws.write("t_dat,product_code\n")) await once(ws, "drain");

  const baseDate = parseStartDateUTC(opts.startDate);

  const rowsPerDay = Math.floor(opts.rows / opts.days);
  let remainder = opts.rows % opts.days;

  const CHUNK_LINES = 10000;
  let written = 0;
  const logEvery = Math.max(1, Math.floor(opts.days / 20));

  // Map phases to days
  const dayPhase = assignPhasesToDays(opts.days, opts.phases);

  // Drift accumulator: carry expected swap count across records/days for smoothness
  let driftAcc = 0;

  // Feature-block state (persists across days; used only in high-drift phases)
  let featRemain = 0;
  let featProduct = 1;

  for (let day = 0; day < opts.days; day++) {
    const phaseIdx = dayPhase[day];
    const drift = opts.drifts[phaseIdx]!;
    const window = opts.driftWindows[phaseIdx]!;

    // In the high-drift phase (e.g., phase 2), enable "feature blocks":
    // - pFeat: probability to use the current block product at this record
    // - meanBlock: average block length (in records), mapped to tens/hundreds
    const pFeat = drift >= 1 ? Math.min(0.8, drift / 8) : 0; // drift=4 => 0.5; other phases ~0
    const meanBlock = drift >= 1 ? Math.max(50, Math.round(2000 / drift)) : 1e12; // drift=4 => ~500

    const date = addDaysUTC(baseDate, day);
    const dateStr = fmtDateUTC(date);
    let today = rowsPerDay + (remainder > 0 ? 1 : 0);
    if (remainder > 0) remainder--;

    // Expected drift per record today (drift is per day; distribute over today's rows)
    const lambdaPerRecord = (opts.products * drift) / Math.max(1, today);

    let buf = "";
    let linesInBuf = 0;

    for (let i = 0; i < today; i++) {
      let productCode: number;

      // Feature blocks: in high-drift phases, output the same product for a short run
      if (pFeat > 0 && drawRng() < pFeat) {
        if (featRemain <= 0) {
          featProduct = randInt(drawRng, 1, opts.products);
          featRemain = geomLen(meanBlock, drawRng);
        }
        productCode = featProduct;
        featRemain--;
      } else {
        // Regular Zipf + permutation sampling
        const u = drawRng();
        const rankIdx = sampleRank(cdf, u);
        const productIdx = perm[rankIdx]; // 0..products-1
        productCode = productIdx + 1; // 1..products
      }

      buf += `${dateStr},${productCode}\n`;
      linesInBuf++;

      // Continuous drift: accumulate and apply integer steps when due
      driftAcc += lambdaPerRecord;
      const toApply = driftAcc | 0;
      if (toApply > 0) {
        applyDriftSteps(perm, toApply, driftRng, window);
        driftAcc -= toApply;
      }

      if (linesInBuf >= CHUNK_LINES) {
        if (!ws.write(buf)) await once(ws, "drain");
        buf = "";
        linesInBuf = 0;
      }
    }
    if (linesInBuf > 0) {
      if (!ws.write(buf)) await once(ws, "drain");
    }
    written += today;

    if (day % logEvery === 0) {
      console.error(
        `day ${day + 1}/${opts.days} phase=${phaseIdx + 1}/${
          opts.phases.length
        } drift=${drift} window=${window} pFeat=${pFeat.toFixed(2)} meanBlock=${
          isFinite(meanBlock) ? meanBlock : 0
        } written=${written}`
      );
    }
  }

  await new Promise<void>((resolve, reject) => {
    ws.end(() => resolve());
    ws.on("error", reject);
  });

  console.error(
    `done: ${opts.output} rows=${written} products=${opts.products} days=${opts.days} phases=${opts.phases.length}`
  );
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
