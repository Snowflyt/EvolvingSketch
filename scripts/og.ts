#!/usr/bin/env node
import fs from "node:fs";
import { parseArgs } from "node:util";

interface Request {
  timestamp: bigint; // in seconds
  obj_id: bigint; // hash of object id (string)
  obj_size: bigint; // in bytes
  next_access_vtime: bigint; // logical time, -1 if not accessed later
}
const UNALIGNED_REQUEST_SIZE = 4 + 8 + 4 + 8; // size of `Request` in bytes

class CachingTrace {
  readonly #filePath: string;
  readonly #size: number;

  constructor(filePath: string) {
    this.#filePath = filePath;

    // Get file size without loading the entire file
    const stats = fs.statSync(filePath);
    this.#size = Math.floor(stats.size / UNALIGNED_REQUEST_SIZE);
  }

  get filePath(): string {
    return this.#filePath;
  }

  get size(): number {
    return this.#size;
  }

  async *[Symbol.asyncIterator](): AsyncIterator<Request, void, undefined> {
    const CHUNK_SIZE = 1_000_000; // Process in chunks of 1M requests
    const CHUNK_BUFFER_SIZE = UNALIGNED_REQUEST_SIZE * CHUNK_SIZE;

    const stream = fs.createReadStream(this.#filePath, {
      highWaterMark: CHUNK_BUFFER_SIZE,
    });

    let buffer = Buffer.alloc(0);
    let bytesProcessed = 0;

    for await (const chunk of stream) {
      buffer = Buffer.concat([buffer, chunk as Buffer]);

      // Process complete requests
      while (buffer.length >= UNALIGNED_REQUEST_SIZE) {
        const timestamp = BigInt(buffer.readUInt32LE(0));
        const obj_id = BigInt(buffer.readBigUInt64LE(4));
        const obj_size = BigInt(buffer.readUInt32LE(12));
        const next_access_vtime = BigInt(buffer.readBigInt64LE(16));

        // Update buffer to remove the processed request
        buffer = buffer.subarray(UNALIGNED_REQUEST_SIZE);
        bytesProcessed += UNALIGNED_REQUEST_SIZE;

        yield {
          timestamp,
          obj_id,
          obj_size,
          next_access_vtime,
        };
      }
    }
  }
}

type Options = StatOptions | SliceOptions | ConcatOptions | MixOptions;

interface StatOptions {
  command: "stat";
  file: string;
}

interface SliceOptions {
  command: "slice";
  file: string;
  start: number;
  end: number;
  output?: string;
}

interface ConcatOptions {
  command: "concat";
  files: string[];
  output?: string;
}

interface MixOptions {
  command: "mix";
  file1: string;
  file2: string;
  switches: number;
  output?: string;
}

function printUsage() {
  console.log("Usage: node og.ts <command> [options]");
  console.log("");
  console.log("Commands:");
  console.log("  stat      Display statistics and information about oracleGeneral file");
  console.log("  slice     Extract a range of requests from oracleGeneral file");
  console.log("  concat    Concatenate multiple oracleGeneral files");
  console.log("  mix       Mix two oracleGeneral files with random switching");
  console.log("");
  console.log("Options:");
  console.log("  -h, --help            Show this help message");
  console.log("");
  console.log('Use "node og.ts <command> --help" for more information about a command.');
}

function printStatUsage() {
  console.log("Usage: node og.ts stat <input_file.oracleGeneral>");
  console.log("");
  console.log("Display statistics and basic information about an oracleGeneral trace file.");
  console.log("");
  console.log("Arguments:");
  console.log("  input_file.oracleGeneral  Path to input oracleGeneral file");
  console.log("");
  console.log("Options:");
  console.log("  -h, --help                Show this help message");
  console.log("");
  console.log("Examples:");
  console.log("  node og.ts stat trace.oracleGeneral");
}

function printSliceUsage() {
  console.log("Usage: node og.ts slice <input_file.oracleGeneral> <start> <end> [options]");
  console.log("");
  console.log("Extract a range of requests from an oracleGeneral file.");
  console.log("");
  console.log("Arguments:");
  console.log("  input_file.oracleGeneral  Path to input oracleGeneral file");
  console.log("  start                     Starting request index (0-based, inclusive)");
  console.log("  end                       Ending request index (0-based, exclusive)");
  console.log("");
  console.log("Options:");
  console.log("  --output <file.oracleGeneral>  Output file (default: stdout as binary)");
  console.log("  -o <file.oracleGeneral>        Short form of --output");
  console.log("  -h, --help                     Show this help message");
  console.log("");
  console.log("Examples:");
  console.log("  node og.ts slice trace.oracleGeneral 100 1000 --output subset.oracleGeneral");
  console.log("  node og.ts slice trace.oracleGeneral 0 500 -o first500.oracleGeneral");
}

function printConcatUsage() {
  console.log("Usage: node og.ts concat <input_files>... [options]");
  console.log("");
  console.log("Concatenate multiple oracleGeneral files into one.");
  console.log("");
  console.log("Arguments:");
  console.log("  input_files               One or more input oracleGeneral files to concatenate");
  console.log("");
  console.log("Options:");
  console.log("  --output <file.oracleGeneral>  Output file (default: stdout as binary)");
  console.log("  -o <file.oracleGeneral>        Short form of --output");
  console.log("  -h, --help                     Show this help message");
  console.log("");
  console.log("Examples:");
  console.log(
    "  node og.ts concat file1.oracleGeneral file2.oracleGeneral --output merged.oracleGeneral"
  );
  console.log(
    "  node og.ts concat file1.oracleGeneral file2.oracleGeneral -o merged.oracleGeneral"
  );
  console.log("  node og.ts concat file1.oracleGeneral file2.oracleGeneral > merged.oracleGeneral");
}

function printMixUsage() {
  console.log(
    "Usage: node og.ts mix <file1.oracleGeneral> <file2.oracleGeneral> <switches> [options]"
  );
  console.log("");
  console.log("Mix two oracleGeneral files with random switching between them.");
  console.log("");
  console.log("Arguments:");
  console.log("  file1.oracleGeneral       Path to first input oracleGeneral file");
  console.log("  file2.oracleGeneral       Path to second input oracleGeneral file");
  console.log("  switches                  Approximate number of switches between files");
  console.log("");
  console.log("Options:");
  console.log("  --output <file.oracleGeneral>  Output file (default: stdout as binary)");
  console.log("  -o <file.oracleGeneral>        Short form of --output");
  console.log("  -h, --help                     Show this help message");
  console.log("");
  console.log("Examples:");
  console.log(
    "  node og.ts mix trace1.oracleGeneral trace2.oracleGeneral 10 --output mixed.oracleGeneral"
  );
  console.log("  node og.ts mix file1.oracleGeneral file2.oracleGeneral 5 -o result.oracleGeneral");
  console.log("  node og.ts mix trace1.oracleGeneral trace2.oracleGeneral 8 > mixed.oracleGeneral");
  console.log("");
  console.log("Note: The actual number of switches will be approximately the specified number,");
  console.log("      with random variations to create a natural mixing pattern.");
}

function parseArguments(): Options {
  const { values, positionals } = parseArgs({
    args: process.argv.slice(2),
    options: {
      output: {
        type: "string",
        short: "o",
      },
      help: {
        type: "boolean",
        short: "h",
      },
    },
    allowPositionals: true,
  });

  if (values.help && positionals.length === 0) {
    printUsage();
    process.exit(0);
  }

  if (positionals.length === 0) {
    console.error("Error: No command specified");
    printUsage();
    process.exit(1);
  }

  const command = positionals[0];
  const output = values.output;

  if (command === "stat") {
    if (values.help) {
      printStatUsage();
      process.exit(0);
    }

    if (positionals.length !== 2) {
      console.error("Error: stat command requires exactly 1 argument: <file>");
      printStatUsage();
      process.exit(1);
    }

    const file = positionals[1];

    if (!fs.existsSync(file)) {
      console.error(`Error: Input file "${file}" does not exist`);
      process.exit(1);
    }

    return { command: "stat", file };
  }

  if (command === "slice") {
    if (values.help) {
      printSliceUsage();
      process.exit(0);
    }

    if (positionals.length !== 4) {
      console.error("Error: slice command requires exactly 3 arguments: <file> <start> <end>");
      printSliceUsage();
      process.exit(1);
    }

    const file = positionals[1];
    const start = parseInt(positionals[2]);
    const end = parseInt(positionals[3]);

    if (!fs.existsSync(file)) {
      console.error(`Error: Input file "${file}" does not exist`);
      process.exit(1);
    }

    if (isNaN(start) || isNaN(end)) {
      console.error("Error: start and end must be valid numbers");
      process.exit(1);
    }

    if (start < 0 || end < 0) {
      console.error("Error: start and end must be non-negative");
      process.exit(1);
    }

    if (start >= end) {
      console.error("Error: start must be less than end");
      process.exit(1);
    }

    return { command: "slice", file, start, end, output };
  }

  if (command === "concat") {
    if (values.help) {
      printConcatUsage();
      process.exit(0);
    }

    const files = positionals.slice(1);

    if (files.length === 0) {
      console.error("Error: concat command requires at least one input file");
      printConcatUsage();
      process.exit(1);
    }

    // Validate input files exist
    for (const file of files) {
      if (!fs.existsSync(file)) {
        console.error(`Error: Input file "${file}" does not exist`);
        process.exit(1);
      }
    }

    return { command: "concat", files, output };
  }

  if (command === "mix") {
    if (values.help) {
      printMixUsage();
      process.exit(0);
    }

    if (positionals.length !== 4) {
      console.error("Error: mix command requires exactly 3 arguments: <file1> <file2> <switches>");
      printMixUsage();
      process.exit(1);
    }

    const file1 = positionals[1];
    const file2 = positionals[2];
    const switches = parseInt(positionals[3]);

    if (!fs.existsSync(file1)) {
      console.error(`Error: Input file "${file1}" does not exist`);
      process.exit(1);
    }

    if (!fs.existsSync(file2)) {
      console.error(`Error: Input file "${file2}" does not exist`);
      process.exit(1);
    }

    if (isNaN(switches) || switches <= 0) {
      console.error("Error: switches must be a positive number");
      process.exit(1);
    }

    return { command: "mix", file1, file2, switches, output };
  }

  console.error(`Error: Unknown command "${command}"`);
  printUsage();
  process.exit(1);
}

async function statOG(options: StatOptions): Promise<void> {
  const trace = new CachingTrace(options.file);

  // Get file size in bytes
  const stats = fs.statSync(options.file);
  const fileSizeBytes = stats.size;
  const fileSizeMB = (fileSizeBytes / (1024 * 1024)).toLocaleString();

  console.log(`File size: ${fileSizeBytes.toLocaleString()} bytes (${fileSizeMB} MB)`);
  console.log(`Total requests: ${trace.size.toLocaleString()}`);

  if (trace.size === 0) {
    console.log("File is empty or invalid.");
    return;
  }

  console.log("First 5 requests:");
  const iter = trace[Symbol.asyncIterator]();
  for (let i = 0; i < 5 && i < trace.size; i++) {
    const { done, value } = await iter.next();
    if (done) break;
    const { next_access_vtime, obj_id, obj_size, timestamp } = value;
    console.log(
      `  ${i}: timestamp=${timestamp}, obj_id=${obj_id}, obj_size=${obj_size}, next_access_vtime=${next_access_vtime}` +
        (i === (trace.size < 5 ? trace.size - 1 : 4) ? "\n" : "")
    );
  }
}

async function sliceOG(options: SliceOptions): Promise<void> {
  const trace = new CachingTrace(options.file);
  const outputTarget = options.output || "stdout";

  if (options.start >= trace.size) {
    console.error(`Error: start index ${options.start} is beyond file size ${trace.size}`);
    process.exit(1);
  }

  const actualEnd = Math.min(options.end, trace.size);
  const totalRequests = actualEnd - options.start;

  console.log(`Slicing "${options.file}" from request ${options.start} to ${actualEnd}...`);
  console.log(`Extracting ${totalRequests} requests...`);

  // Calculate byte offsets
  const startOffset = options.start * UNALIGNED_REQUEST_SIZE;
  const endOffset = actualEnd * UNALIGNED_REQUEST_SIZE;
  const totalBytes = endOffset - startOffset;

  // Create output stream or use stdout
  const output = options.output ? fs.createWriteStream(options.output) : process.stdout;

  // Progress bar setup
  const dots = 100;
  const bytesPerDot = Math.max(1, Math.floor(totalBytes / dots));
  let dotsShown = 0;
  let bytesWritten = 0;

  if (options.output) process.stdout.write("Progress: [");

  // Read the specific range directly using file stream with start/end positions
  const inputStream = fs.createReadStream(options.file, {
    start: startOffset,
    end: endOffset - 1, // end is inclusive in createReadStream
    highWaterMark: 1024 * 1024, // 1MB chunks
  });

  for await (const chunk of inputStream) {
    const canContinue = output.write(chunk);
    if (!canContinue) {
      await new Promise<void>((resolve) => output.once("drain", resolve));
    }

    bytesWritten += chunk.length;

    // Show progress dot
    if (options.output) {
      const expectedDots = Math.floor(bytesWritten / bytesPerDot);
      while (dotsShown < expectedDots && dotsShown < dots) {
        process.stdout.write(".");
        dotsShown++;
      }
    }
  }

  // Fill remaining dots
  if (options.output) {
    while (dotsShown < dots) {
      process.stdout.write(".");
      dotsShown++;
    }
    process.stdout.write("] Done!\n");

    if (output !== process.stdout) {
      (output as fs.WriteStream).end();
    }
    console.log(`${totalRequests} requests written to "${outputTarget}".`);
  }
}

async function concatOG(options: ConcatOptions): Promise<void> {
  const outputTarget = options.output || "stdout";

  // Only show progress when outputting to file
  if (options.output) {
    console.log(`Concatenating ${options.files.length} oracleGeneral files...`);
  }

  // Create output stream or use stdout
  const output = options.output ? fs.createWriteStream(options.output) : process.stdout;

  let totalRequests = 0;
  let totalBytes = 0;

  // Calculate total size for progress tracking
  if (options.output) {
    for (const filePath of options.files) {
      const stats = fs.statSync(filePath);
      totalBytes += stats.size;
    }
  }

  // Progress bar setup
  const dots = 100;
  const bytesPerDot = Math.max(1, Math.floor(totalBytes / dots));
  let dotsShown = 0;
  let bytesWritten = 0;

  if (options.output) process.stdout.write("Progress: [");

  for (let i = 0; i < options.files.length; i++) {
    const filePath = options.files[i];

    // Show progress
    if (options.output) {
      console.log(`\nProcessing ${i + 1}/${options.files.length} files: "${filePath}"...`);
    }

    const trace = new CachingTrace(filePath);
    totalRequests += trace.size;

    // Read the file directly as binary data
    const inputStream = fs.createReadStream(filePath, {
      highWaterMark: 1024 * 1024, // 1MB chunks
    });

    for await (const chunk of inputStream) {
      const canContinue = output.write(chunk);
      if (!canContinue) {
        await new Promise<void>((resolve) => output.once("drain", resolve));
      }

      bytesWritten += chunk.length;

      // Show progress dot
      if (options.output) {
        const expectedDots = Math.floor(bytesWritten / bytesPerDot);
        while (dotsShown < expectedDots && dotsShown < dots) {
          process.stdout.write(".");
          dotsShown++;
        }
      }
    }
  }

  // Fill remaining dots
  if (options.output) {
    while (dotsShown < dots) {
      process.stdout.write(".");
      dotsShown++;
    }
    process.stdout.write("] Done!\n");

    if (output !== process.stdout) {
      (output as fs.WriteStream).end();
    }

    console.log(`Concatenated ${options.files.length} oracleGeneral files to "${outputTarget}"`);
    console.log(`${totalRequests.toLocaleString()} total requests written.`);
  }
}

async function mixOG(options: MixOptions): Promise<void> {
  const outputTarget = options.output || "stdout";

  if (options.output) {
    console.log(
      `Mixing "${options.file1}" and "${options.file2}" with ~${options.switches} switches...`
    );
  }

  const trace1 = new CachingTrace(options.file1);
  const trace2 = new CachingTrace(options.file2);

  // Create output stream
  const output = options.output ? fs.createWriteStream(options.output) : process.stdout;

  // Calculate segment sizes with randomization
  const totalRequests = trace1.size + trace2.size;
  const totalSwitches = options.switches * 2; // Each switch creates two segments
  const avgSegmentSize = Math.floor(totalRequests / totalSwitches);

  // Generate segment sizes with random variation (±30% of average)
  const segments: Array<{ file: 1 | 2; size: number }> = [];
  let remaining1 = trace1.size;
  let remaining2 = trace2.size;
  let currentFile: 1 | 2 = 1; // Start with file 1

  while (remaining1 > 0 || remaining2 > 0) {
    const currentRemaining = currentFile === 1 ? remaining1 : remaining2;

    if (currentRemaining <= 0) {
      // Switch to the other file if current is exhausted
      currentFile = currentFile === 1 ? 2 : 1;
      continue;
    }

    // Calculate segment size with randomization
    const variation = Math.random() * 0.6 - 0.3; // -30% to +30%
    const segmentSize = Math.max(1, Math.floor(avgSegmentSize * (1 + variation)));
    const actualSize = Math.min(segmentSize, currentRemaining);

    segments.push({ file: currentFile, size: actualSize });

    if (currentFile === 1) {
      remaining1 -= actualSize;
    } else {
      remaining2 -= actualSize;
    }

    // Switch to the other file (if it has remaining data)
    const otherFile: 1 | 2 = currentFile === 1 ? 2 : 1;
    const otherRemaining = otherFile === 1 ? remaining1 : remaining2;
    if (otherRemaining > 0) {
      currentFile = otherFile;
    }
  }

  if (options.output) {
    console.log(
      `Generated ${segments.length} segments with actual switches: ${Math.floor(
        segments.length / 2
      )}`
    );
  }

  // Progress tracking
  let totalBytes = 0;
  if (options.output) {
    const stats1 = fs.statSync(options.file1);
    const stats2 = fs.statSync(options.file2);
    totalBytes = stats1.size + stats2.size;
  }

  const dots = 100;
  const bytesPerDot = Math.max(1, Math.floor(totalBytes / dots));
  let dotsShown = 0;
  let bytesWritten = 0;

  if (options.output) process.stdout.write("Progress: [");

  // Process segments
  let file1Offset = 0;
  let file2Offset = 0;

  for (let i = 0; i < segments.length; i++) {
    const segment = segments[i];
    const filePath = segment.file === 1 ? options.file1 : options.file2;
    const currentOffset = segment.file === 1 ? file1Offset : file2Offset;

    // Calculate byte offsets for this segment
    const startOffset = currentOffset * UNALIGNED_REQUEST_SIZE;
    const endOffset = (currentOffset + segment.size) * UNALIGNED_REQUEST_SIZE;

    // Read the segment
    const inputStream = fs.createReadStream(filePath, {
      start: startOffset,
      end: endOffset - 1, // end is inclusive in createReadStream
      highWaterMark: 1024 * 1024, // 1MB chunks
    });

    for await (const chunk of inputStream) {
      const canContinue = output.write(chunk);
      if (!canContinue) {
        await new Promise<void>((resolve) => output.once("drain", resolve));
      }

      bytesWritten += chunk.length;

      // Show progress dot
      if (options.output) {
        const expectedDots = Math.floor(bytesWritten / bytesPerDot);
        while (dotsShown < expectedDots && dotsShown < dots) {
          process.stdout.write(".");
          dotsShown++;
        }
      }
    }

    // Update offset for the processed file
    if (segment.file === 1) {
      file1Offset += segment.size;
    } else {
      file2Offset += segment.size;
    }
  }

  // Fill remaining dots
  if (options.output) {
    while (dotsShown < dots) {
      process.stdout.write(".");
      dotsShown++;
    }
    process.stdout.write("] Done!\n");

    if (output !== process.stdout) {
      (output as fs.WriteStream).end();
    }

    console.log(
      `Mixed ${trace1.size.toLocaleString()} + ${trace2.size.toLocaleString()} = ${totalRequests.toLocaleString()} requests to "${outputTarget}"`
    );
    console.log(
      `Created ${segments.length} segments with ${Math.floor(
        segments.length / 2
      )} effective switches.`
    );
  }
}

// Entry point
if (process.argv[1] === import.meta.filename) {
  const options = parseArguments();

  try {
    if (options.command === "stat") {
      await statOG(options);
    } else if (options.command === "slice") {
      await sliceOG(options);
    } else if (options.command === "concat") {
      await concatOG(options);
    } else if (options.command === "mix") {
      await mixOG(options);
    }
  } catch (error) {
    console.error("Error:", error instanceof Error ? error.message : String(error));
    process.exit(1);
  }
}
