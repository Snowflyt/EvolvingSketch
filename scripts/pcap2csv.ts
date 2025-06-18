#!/usr/bin/env node
import { Buffer } from "node:buffer";
import { EventEmitter } from "node:events";
import fs from "node:fs";

let processedCount = 0;
let ipV6Count = 0;
let truncatedCount = 0;

interface FourTuple {
  srcIP: string;
  dstIP: string;
  srcPort: number;
  dstPort: number;
}

// See: https://github.com/indutny/node-ip/blob/3b0994a74eca51df01f08c40d6a65ba0e1845d04/lib/ip.js#L61-L83
function ip2string(buffer: Buffer): string {
  const result: (number | string)[] = [];

  // IPv4
  if (buffer.length === 4) {
    for (let i = 0; i < buffer.length; i++) result.push(buffer[i]);
    return result.join(".");
  }

  // IPv6
  if (buffer.length === 16) {
    for (let i = 0; i < buffer.length; i += 2) result.push(buffer.readUInt16BE(i).toString(16));
    return result
      .join(":")
      .replace(/(^|:)0(:0)*:0(:|$)/, "$1::$3")
      .replace(/:{3,4}/, "::");
  }

  throw new TypeError(
    `Invalid IP address length: ${buffer.length}. Expected 4 (IPv4) or 16 (IPv6) bytes.`,
  );
}

// See: https://github.com/indutny/node-ip/blob/3b0994a74eca51df01f08c40d6a65ba0e1845d04/lib/ip.js#L429-L436
function ip2long(ip: string): number {
  let ipl = 0;
  ip.split(".").forEach((octet) => {
    ipl <<= 8;
    ipl += parseInt(octet);
  });
  return ipl >>> 0;
}

interface ConvertOptions {
  ipv4Only: boolean;
  convertIpToUint: boolean;
}

function parsePacket(data: Buffer, options: ConvertOptions): FourTuple | null {
  const ipVersion = (data[0] >> 4) & 0x0f;

  if (ipVersion === 6) {
    if (options.ipv4Only) {
      ipV6Count++;
      return null;
    }

    if (data.length < 40) {
      console.warn(
        "Warning: IPv6 packet too short (" + data.length + " bytes < 40 bytes), skipping",
      );
      return null;
    }

    const srcIP = ip2string(data.subarray(8, 24));
    const dstIP = ip2string(data.subarray(24, 40));

    const protocol = data.readUInt8(6);
    if (protocol !== /* TCP */ 6) return null;

    const srcPort = data.readUInt16BE(40);
    const dstPort = data.readUInt16BE(42);

    ipV6Count++;

    return { srcIP, dstIP, srcPort, dstPort };
  }

  if (ipVersion !== 4) {
    console.warn(`Warning: Unknown IP version ${ipVersion}, skipping packet`);
    return null;
  }

  // IPv4
  const protocol = data.readUInt8(9);
  const transportOffset = (data[0] & 0x0f) * 4;

  if (transportOffset < 20 || transportOffset > data.length) {
    console.warn(`Warning: IPv4 header length ${transportOffset} is invalid, skipping packet`);
    return null;
  }

  const srcIPString = ip2string(data.subarray(12, 16));
  const dstIPString = ip2string(data.subarray(16, 20));

  const srcIP = options.convertIpToUint ? ip2long(srcIPString).toString() : srcIPString;
  const dstIP = options.convertIpToUint ? ip2long(dstIPString).toString() : dstIPString;

  if (protocol !== /* TCP */ 6) return null;

  if (data.length < transportOffset + 4) {
    truncatedCount++;
    return null;
  }

  const srcPort = data.readUInt16BE(transportOffset);
  const dstPort = data.readUInt16BE(transportOffset + 2);

  return { srcIP, dstIP, srcPort, dstPort };
}

function countTotalPackets(filePath: string): Promise<number> {
  return new Promise((resolve) => {
    let count = 0;
    const reader = new PCAPReader(filePath);
    reader.on("packet", () => count++);
    reader.on("end", () => resolve(count));
  });
}

async function convert(inputFile: string, outputFile: string, options: ConvertOptions) {
  const totalPackets = await countTotalPackets(inputFile);
  console.log(`Processing ${totalPackets} packets from "${inputFile}"...`);

  const reader = new PCAPReader(inputFile);
  const output = fs.createWriteStream(outputFile, { encoding: "utf-8" });

  output.write("timestamp_μs,src_ip,src_port,dst_ip,dst_port\n");

  const dots = 100;
  const linesPerDot = Math.max(1, Math.floor(totalPackets / dots));
  let processed = 0;

  process.stdout.write("Progress: [");

  reader.on("packet", (packet) => {
    const tuple = parsePacket(packet.data, options);
    if (tuple) {
      processedCount++;

      const tsSec = packet.header.timestampSeconds;
      const tsUsec = packet.header.timestampMicroseconds;
      const timestamp = tsSec * 1_000_000 + tsUsec;

      const row =
        timestamp +
        "," +
        tuple.srcIP +
        "," +
        tuple.srcPort +
        "," +
        tuple.dstIP +
        "," +
        tuple.dstPort +
        "\n";
      if (!output.write(row)) {
        reader.pause();
        // console.log("paused");
        output.once("drain", () => reader.resume());
      }
    }

    processed++;
    if (processed % linesPerDot === 0) process.stdout.write(".");
  });

  reader.on("end", () => {
    output.end();
    process.stdout.write("] Done!\n");

    // Summary
    console.log(
      `* ${processedCount} (${((processedCount / totalPackets) * 100).toFixed(2)}%) packets processed`,
    );
    console.log(
      `* ${truncatedCount} (${((truncatedCount / totalPackets) * 100).toFixed(2)}%) were truncated packets and skipped`,
    );
    if (!options.ipv4Only) {
      console.log(
        `* Among all processed packets, ${processedCount - ipV6Count} (${(((processedCount - ipV6Count) / processedCount) * 100).toFixed(2)}%) were IPv4 packets`,
      );
    } else {
      console.log(
        `* ${ipV6Count} (${((ipV6Count / totalPackets) * 100).toFixed(2)}%) IPv6 packets were skipped due to --ipv4-only option`,
      );
    }
  });
}

function parseArgs(): { inputFile: string; outputFile: string; options: ConvertOptions } {
  const args = process.argv.slice(2);
  let inputFile = "";
  let outputFile = "";
  let ipv4Only = false;
  let convertIpToUint = false;

  for (let i = 0; i < args.length; i++) {
    const arg = args[i];

    if (arg === "--ipv4-only") {
      ipv4Only = true;
    } else if (arg === "--convert-ip-to-uint") {
      convertIpToUint = true;
    } else if (arg === "--help" || arg === "-h") {
      printUsage();
      process.exit(0);
    } else if (!inputFile) {
      inputFile = arg;
    } else if (!outputFile) {
      outputFile = arg;
    } else {
      console.error(`Unknown argument: ${arg}`);
      printUsage();
      process.exit(1);
    }
  }

  if (!inputFile || !outputFile) {
    console.error(
      `Error: Missing required arguments:${inputFile ? "" : " <input_file.pcap>"}${outputFile ? "" : " <output_file.csv>"}`,
    );
    printUsage();
    process.exit(1);
  }

  return { inputFile, outputFile, options: { ipv4Only, convertIpToUint } };
}

function printUsage() {
  console.log("Usage: node pcap2csv.ts <input_file.pcap> <output_file.csv> [options]");
  console.log("");
  console.log("Arguments:");
  console.log("  input_file.pcap       Path to input PCAP file");
  console.log("  output_file.csv       Path to output CSV file");
  console.log("");
  console.log("Options:");
  console.log("  --ipv4-only           Only process IPv4 packets, skip IPv6 packets");
  console.log("  --convert-ip-to-uint  Convert IP addresses to unsigned integers");
  console.log("  -h, --help            Show this help message");
  console.log("");
  console.log("Examples:");
  console.log("  node pcap2csv.ts input.pcap output.csv");
  console.log("  node pcap2csv.ts input.pcap output.csv --ipv4-only");
  console.log("  node pcap2csv.ts input.pcap output.csv --convert-ip-to-uint");
  console.log("  node pcap2csv.ts input.pcap output.csv --ipv4-only --convert-ip-to-uint");
}

// Entry point
if (process.argv[1] === import.meta.filename) {
  const { inputFile, outputFile, options } = parseArgs();
  Promise.resolve().then(() => convert(inputFile, outputFile, options));
}

//
// ==== PCAPReader Implementation ====
//
const GLOBAL_HEADER_LENGTH = 24; // Bytes
const PACKET_HEADER_LENGTH = 16; // Bytes

interface GlobalHeader {
  magicNumber: number;
  majorVersion: number;
  minorVersion: number;
  gmtOffset: number;
  timestampAccuracy: number;
  snapshotLength: number;
  linkLayerType: number;
}

interface PacketHeader {
  timestampSeconds: number;
  timestampMicroseconds: number;
  capturedLength: number;
  originalLength: number;
}

interface Packet {
  header: PacketHeader;
  data: Buffer;
}

type ParserState = () => boolean;

export class PCAPReader extends EventEmitter {
  #stream: fs.ReadStream;
  #buffer: Buffer | null = null;
  #state: ParserState;
  #endianness: "BE" | "LE" | null = null;
  #currentPacketHeader: PacketHeader | null = null;
  #errored = false;
  #paused = false;
  #pendingData: Buffer[] = [];

  constructor(input: string | fs.ReadStream) {
    super();

    this.#stream = typeof input === "string" ? fs.createReadStream(input) : input;

    this.#state = this.#parseGlobalHeader.bind(this);

    this.#stream.on("data", (data) => {
      if (this.#errored) return;
      if (this.#paused) this.#pendingData.push(data as Buffer);
      this.#processData(data as Buffer);
    });
    this.#stream.on("error", (err) => {
      this.emit("error", err);
    });
    this.#stream.on("end", () => {
      this.emit("end");
    });

    // Start reading in next tick
    process.nextTick(() => {
      if (!this.#paused) {
        this.#stream.resume();
      }
    });
  }

  public pause(): void {
    this.#paused = true;
    this.#stream.pause();
  }

  public resume(): void {
    this.#paused = false;
    this.#stream.resume();

    // Process any pending data
    // eslint-disable-next-line @typescript-eslint/no-unnecessary-condition
    while (this.#pendingData.length > 0 && !this.#paused) {
      const data = this.#pendingData.shift()!;
      this.#processData(data);
    }
  }

  #processData(data: Buffer): void {
    this.#updateBuffer(data);
    while (this.#state() && !this.#paused);
  }

  #updateBuffer(data: Buffer): void {
    this.#buffer = this.#buffer ? Buffer.concat([this.#buffer, data]) : data;
  }

  #parseGlobalHeader(): boolean {
    const buffer = this.#buffer;
    if (!buffer || buffer.length < GLOBAL_HEADER_LENGTH) return false;

    const magicNumber = buffer.toString("hex", 0, 4);

    // Determine pcap endianness
    if (magicNumber === "a1b2c3d4") {
      this.#endianness = "BE";
    } else if (magicNumber === "d4c3b2a1") {
      this.#endianness = "LE";
    } else {
      this.#errored = true;
      this.#stream.pause();
      const msg = `unknown magic number: ${magicNumber}`;
      this.emit("error", new Error(msg));
      this.emit("end");
      return false;
    }

    const header: GlobalHeader =
      this.#endianness === "BE" ?
        {
          magicNumber: buffer.readUInt32BE(0),
          majorVersion: buffer.readUInt16BE(4),
          minorVersion: buffer.readUInt16BE(6),
          gmtOffset: buffer.readInt32BE(8),
          timestampAccuracy: buffer.readUInt32BE(12),
          snapshotLength: buffer.readUInt32BE(16),
          linkLayerType: buffer.readUInt32BE(20),
        }
      : {
          magicNumber: buffer.readUInt32LE(0),
          majorVersion: buffer.readUInt16LE(4),
          minorVersion: buffer.readUInt16LE(6),
          gmtOffset: buffer.readInt32LE(8),
          timestampAccuracy: buffer.readUInt32LE(12),
          snapshotLength: buffer.readUInt32LE(16),
          linkLayerType: buffer.readUInt32LE(20),
        };

    if (header.majorVersion !== 2 || header.minorVersion !== 4) {
      this.#errored = true;
      this.#stream.pause();
      const msg = `unsupported version ${header.majorVersion}.${header.minorVersion}. PCAPReader only parses libpcap file format 2.4`;
      this.emit("error", new Error(msg));
      this.emit("end");
      return false;
    }

    this.emit("globalHeader", header);
    this.#buffer = buffer.subarray(GLOBAL_HEADER_LENGTH);
    this.#state = this.#parsePacketHeader.bind(this);
    return true;
  }

  #parsePacketHeader(): boolean {
    const buffer = this.#buffer;
    if (!buffer || buffer.length < PACKET_HEADER_LENGTH) return false;

    const header: PacketHeader =
      this.#endianness === "BE" ?
        {
          timestampSeconds: buffer.readUInt32BE(0),
          timestampMicroseconds: buffer.readUInt32BE(4),
          capturedLength: buffer.readUInt32BE(8),
          originalLength: buffer.readUInt32BE(12),
        }
      : {
          timestampSeconds: buffer.readUInt32LE(0),
          timestampMicroseconds: buffer.readUInt32LE(4),
          capturedLength: buffer.readUInt32LE(8),
          originalLength: buffer.readUInt32LE(12),
        };

    this.#currentPacketHeader = header;
    this.emit("packetHeader", header);
    this.#buffer = buffer.subarray(PACKET_HEADER_LENGTH);
    this.#state = this.#parsePacketBody.bind(this);
    return true;
  }

  #parsePacketBody(): boolean {
    const buffer = this.#buffer;
    if (
      !buffer ||
      !this.#currentPacketHeader ||
      buffer.length < this.#currentPacketHeader.capturedLength
    )
      return false;

    const data = buffer.subarray(0, this.#currentPacketHeader.capturedLength);

    this.emit("packetData", data);
    this.emit("packet", {
      header: this.#currentPacketHeader,
      data,
    } as Packet);

    this.#buffer = buffer.subarray(this.#currentPacketHeader.capturedLength);
    this.#state = this.#parsePacketHeader.bind(this);
    return true;
  }
}
