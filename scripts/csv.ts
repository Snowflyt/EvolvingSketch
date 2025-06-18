#!/usr/bin/env node
import fs from "node:fs";
import { createInterface } from "node:readline";
import { parseArgs } from "node:util";

type Options = ConcatOptions | SliceOptions | SelectOptions | JoinOptions;

interface ConcatOptions {
  command: "concat";
  files: string[];
  output?: string;
}

interface SliceOptions {
  command: "slice";
  file: string;
  start: number;
  end: number;
  output?: string;
}

interface SelectOptions {
  command: "select";
  file: string;
  columns: number[];
  output?: string;
}

interface JoinOptions {
  command: "join";
  leftFile: string;
  rightFile: string;
  leftColumn: number;
  rightColumn: number;
  output?: string;
}

function printUsage() {
  console.log("Usage: node csv.ts <command> [options]");
  console.log("");
  console.log("Commands:");
  console.log("  concat    Concatenate multiple CSV files");
  console.log("  slice     Extract rows from CSV file");
  console.log("  select    Select specific columns from CSV file");
  console.log("  join      Inner join two CSV files");
  console.log("");
  console.log("Options:");
  console.log("  -h, --help            Show this help message");
  console.log("");
  console.log('Use "node csv.ts <command> --help" for more information about a command.');
}

function printConcatUsage() {
  console.log("Usage: node csv.ts concat <input_files>... [options]");
  console.log("");
  console.log(
    "Concatenate multiple CSV files into one, preserving the header from the first file."
  );
  console.log("");
  console.log("Arguments:");
  console.log("  input_files           One or more input CSV files to concatenate");
  console.log("");
  console.log("Options:");
  console.log("  --output <file.csv>   Output file (default: stdout)");
  console.log("  -o <file.csv>         Short form of --output");
  console.log("  -h, --help            Show this help message");
  console.log("");
  console.log("Examples:");
  console.log("  node csv.ts concat file1.csv file2.csv file3.csv --output merged.csv");
  console.log("  node csv.ts concat file1.csv file2.csv -o merged.csv");
  console.log("  node csv.ts concat file1.csv file2.csv > merged.csv");
}

function printSliceUsage() {
  console.log("Usage: node csv.ts slice <input_file.csv> <start> <end> [options]");
  console.log("");
  console.log("Extract a range of data rows from a CSV file, preserving the header.");
  console.log("");
  console.log("Arguments:");
  console.log("  input_file.csv        Path to input CSV file");
  console.log("  start                 Starting data row number (0-based, inclusive)");
  console.log("  end                   Ending data row number (0-based, exclusive)");
  console.log("");
  console.log("Options:");
  console.log("  --output <file.csv>   Output file (default: stdout)");
  console.log("  -o <file.csv>         Short form of --output");
  console.log("  -h, --help            Show this help message");
  console.log("");
  console.log("Examples:");
  console.log("  node csv.ts slice input.csv 100 1000 --output subset.csv");
  console.log("  node csv.ts slice input.csv 0 500 -o first500.csv");
  console.log("  node csv.ts slice input.csv 100 1000 > subset.csv");
}

function printSelectUsage() {
  console.log("Usage: node csv.ts select <input_file.csv> <column_indices> [options]");
  console.log("");
  console.log("Select specific columns from a CSV file by their indices.");
  console.log("");
  console.log("Arguments:");
  console.log("  input_file.csv        Path to input CSV file");
  console.log("  column_indices        Comma-separated column indices (0-based)");
  console.log("");
  console.log("Options:");
  console.log("  --output <file.csv>   Output file (default: stdout)");
  console.log("  -o <file.csv>         Short form of --output");
  console.log("  -h, --help            Show this help message");
  console.log("");
  console.log("Examples:");
  console.log("  node csv.ts select input.csv 0,1,3 --output selected.csv");
  console.log("  node csv.ts select input.csv 2,5,7 -o columns.csv");
  console.log("  node csv.ts select input.csv 0,1,3 > selected.csv");
}

function printJoinUsage() {
  console.log(
    "Usage: node csv.ts join <left_file.csv> <right_file.csv> <left_column> <right_column> [options]"
  );
  console.log("");
  console.log("Inner join two CSV files on specified columns.");
  console.log("");
  console.log("Arguments:");
  console.log("  left_file.csv         Path to left CSV file");
  console.log("  right_file.csv        Path to right CSV file");
  console.log("  left_column           Column index in left file (0-based)");
  console.log("  right_column          Column index in right file (0-based)");
  console.log("");
  console.log("Options:");
  console.log("  --output <file.csv>   Output file (default: stdout)");
  console.log("  -o <file.csv>         Short form of --output");
  console.log("  -h, --help            Show this help message");
  console.log("");
  console.log("Examples:");
  console.log("  node csv.ts join users.csv orders.csv 0 1 --output joined.csv");
  console.log("  node csv.ts join left.csv right.csv 2 0 -o result.csv");
  console.log("  node csv.ts join file1.csv file2.csv 1 1 > joined.csv");
  console.log("");
  console.log(
    "Note: The join column from the right file will be excluded from output to avoid duplication."
  );
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

    return { command: "slice", file, start, end, output };
  }

  if (command === "select") {
    if (values.help) {
      printSelectUsage();
      process.exit(0);
    }

    if (positionals.length !== 3) {
      console.error("Error: select command requires exactly 2 arguments: <file> <column_indices>");
      printSelectUsage();
      process.exit(1);
    }

    const file = positionals[1];
    const columnIndicesStr = positionals[2];

    if (!fs.existsSync(file)) {
      console.error(`Error: Input file "${file}" does not exist`);
      process.exit(1);
    }

    // Parse column indices
    const columns = columnIndicesStr.split(",").map((str) => {
      const index = parseInt(str.trim());
      if (isNaN(index) || index < 0) {
        console.error(
          `Error: Invalid column index "${str.trim()}". Indices must be non-negative numbers.`
        );
        process.exit(1);
      }
      return index;
    });

    if (columns.length === 0) {
      console.error("Error: At least one column index must be specified");
      process.exit(1);
    }

    return { command: "select", file, columns, output };
  }

  if (command === "join") {
    if (values.help) {
      printJoinUsage();
      process.exit(0);
    }

    if (positionals.length !== 5) {
      console.error(
        "Error: join command requires exactly 4 arguments: <left_file> <right_file> <left_column> <right_column>"
      );
      printJoinUsage();
      process.exit(1);
    }

    const leftFile = positionals[1];
    const rightFile = positionals[2];
    const leftColumn = parseInt(positionals[3]);
    const rightColumn = parseInt(positionals[4]);

    if (!fs.existsSync(leftFile)) {
      console.error(`Error: Left file "${leftFile}" does not exist`);
      process.exit(1);
    }

    if (!fs.existsSync(rightFile)) {
      console.error(`Error: Right file "${rightFile}" does not exist`);
      process.exit(1);
    }

    if (isNaN(leftColumn) || isNaN(rightColumn)) {
      console.error("Error: Column indices must be valid numbers");
      process.exit(1);
    }

    if (leftColumn < 0 || rightColumn < 0) {
      console.error("Error: Column indices must be non-negative");
      process.exit(1);
    }

    return { command: "join", leftFile, rightFile, leftColumn, rightColumn, output };
  }

  console.error(`Error: Unknown command "${command}"`);
  printUsage();
  process.exit(1);
}

async function concatCSV(options: ConcatOptions): Promise<void> {
  const outputTarget = options.output || "stdout";

  // Only show progress when outputting to file
  if (options.output) {
    const output = fs.createWriteStream(options.output, { encoding: "utf-8" });
    let processedRows = 0;
    let isFirstFile = true;

    for (let i = 0; i < options.files.length; i++) {
      const filePath = options.files[i];

      // Show progress
      console.log(`Processing ${i + 1}/${options.files.length} files: "${filePath}"...`);

      const rl = createInterface({
        input: fs.createReadStream(filePath),
        crlfDelay: Infinity,
      });

      let isFirstLine = true;

      for await (const line of rl) {
        // Skip header for subsequent files
        if (!isFirstFile && isFirstLine) {
          isFirstLine = false;
          continue;
        }

        // Handle backpressure
        const canContinue = output.write(line + "\n");
        if (!canContinue) await new Promise<void>((resolve) => output.once("drain", resolve));

        processedRows++;
        isFirstLine = false;
      }

      isFirstFile = false;
    }

    output.end();

    console.log(`Concatenated ${options.files.length} CSV files to "${outputTarget}"`);
    console.log(`${processedRows} total rows written.`);
  } else {
    // Direct output to stdout, no progress info
    let isFirstFile = true;

    for (const filePath of options.files) {
      const rl = createInterface({
        input: fs.createReadStream(filePath),
        crlfDelay: Infinity,
      });

      let isFirstLine = true;

      for await (const line of rl) {
        // Skip header for subsequent files
        if (!isFirstFile && isFirstLine) {
          isFirstLine = false;
          continue;
        }

        // Handle backpressure for stdout
        const canContinue = process.stdout.write(line + "\n");
        if (!canContinue) {
          await new Promise((resolve) => process.stdout.once("drain", resolve));
        }

        isFirstLine = false;
      }

      isFirstFile = false;
    }
  }
}

async function sliceCSV(options: SliceOptions): Promise<void> {
  const outputTarget = options.output || "stdout";

  // Only show progress when outputting to file
  if (options.output)
    console.log(`Slicing "${options.file}" from data row ${options.start} to ${options.end}...`);

  const rl = createInterface({
    input: fs.createReadStream(options.file),
    crlfDelay: Infinity,
  });

  const output = options.output
    ? fs.createWriteStream(options.output, { encoding: "utf-8" })
    : process.stdout;
  let currentLine = 0;
  let dataLine = 0; // Track data lines separately from total lines
  let writtenLines = 0;

  // Progress bar setup
  const totalExpectedDataLines = options.end - options.start;
  const dots = 100;
  const linesPerDot = Math.max(1, Math.floor(totalExpectedDataLines / dots));
  let dotsShown = 0;

  if (options.output) process.stdout.write("Progress: [");

  for await (const line of rl) {
    if (currentLine === 0) {
      // Always preserve header
      const canContinue = output.write(line + "\n");
      if (!canContinue) await new Promise<void>((resolve) => output.once("drain", resolve));
      writtenLines++;
    } else {
      // This is a data line (currentLine >= 1)
      if (dataLine >= options.start && dataLine < options.end) {
        const canContinue = output.write(line + "\n");
        if (!canContinue) await new Promise<void>((resolve) => output.once("drain", resolve));
        writtenLines++;

        // Show progress dot
        if (options.output && writtenLines > 1) {
          // > 1 to exclude header
          const dataWritten = writtenLines - 1; // Exclude header
          const expectedDots = Math.floor(dataWritten / linesPerDot);
          while (dotsShown < expectedDots && dotsShown < dots) {
            process.stdout.write(".");
            dotsShown++;
          }
        }
      }
      dataLine++;

      // Early exit if we’ve processed all needed data lines
      if (dataLine >= options.end) break;
    }

    currentLine++;
  }

  // Fill remaining dots if we reached end of file before expected end
  if (options.output) {
    while (dotsShown < dots) {
      process.stdout.write(".");
      dotsShown++;
    }
    process.stdout.write("] Done!\n");

    output.end();
    const dataLines = writtenLines - 1; // Exclude header
    console.log(`${dataLines} total rows written to "${outputTarget}".`);
  }
}

async function selectCSV(options: SelectOptions): Promise<void> {
  const outputTarget = options.output || "stdout";

  // Only show progress when outputting to file
  if (options.output) {
    console.log(`Selecting columns [${options.columns.join(", ")}] from "${options.file}"...`);
  }

  const rl = createInterface({
    input: fs.createReadStream(options.file),
    crlfDelay: Infinity,
  });

  const output = options.output
    ? fs.createWriteStream(options.output, { encoding: "utf-8" })
    : process.stdout;
  let processedRows = 0;
  let maxColumnIndex = -1;

  for await (const line of rl) {
    // Split the CSV line (simple comma splitting - doesn't handle quoted commas)
    const fields = line.split(",");

    // Check if column indices are valid on first row
    if (processedRows === 0) {
      maxColumnIndex = Math.max(...options.columns);
      if (maxColumnIndex >= fields.length) {
        console.error(
          `Error: Column index ${maxColumnIndex} is out of range. File has only ${
            fields.length
          } columns (0-${fields.length - 1}).`
        );
        process.exit(1);
      }
    }

    // Select the specified columns
    const selectedFields = options.columns.map((index) => fields[index] || "");
    const outputLine = selectedFields.join(",");

    const canContinue = output.write(outputLine + "\n");
    if (!canContinue) await new Promise<void>((resolve) => output.once("drain", resolve));

    processedRows++;
  }

  if (options.output) {
    output.end();
    console.log(`Selected ${options.columns.length} columns from ${processedRows} rows.`);
    console.log(`Output written to "${outputTarget}".`);
  }
}

async function joinCSV(options: JoinOptions): Promise<void> {
  const outputTarget = options.output || "stdout";

  if (options.output) {
    console.log(
      `Joining "${options.leftFile}" and "${options.rightFile}" on columns ${options.leftColumn} and ${options.rightColumn}...`
    );
  }

  // First, read the right file into memory and build a lookup map
  const rightData = new Map<string, string[][]>();
  let rightHeader: string[] = [];

  const rightRl = createInterface({
    input: fs.createReadStream(options.rightFile),
    crlfDelay: Infinity,
  });

  let rightRowIndex = 0;
  for await (const line of rightRl) {
    const fields = line.split(",");

    if (rightRowIndex === 0) {
      rightHeader = fields;
      // Validate right column index
      if (options.rightColumn >= fields.length) {
        console.error(
          `Error: Right column index ${options.rightColumn} is out of range. Right file has only ${
            fields.length
          } columns (0-${fields.length - 1}).`
        );
        process.exit(1);
      }
    } else {
      const joinKey = fields[options.rightColumn] || "";
      if (!rightData.has(joinKey)) {
        rightData.set(joinKey, []);
      }
      rightData.get(joinKey)!.push(fields);
    }
    rightRowIndex++;
  }

  // Now process the left file and perform the join
  const leftRl = createInterface({
    input: fs.createReadStream(options.leftFile),
    crlfDelay: Infinity,
  });

  const output = options.output
    ? fs.createWriteStream(options.output, { encoding: "utf-8" })
    : process.stdout;

  let leftRowIndex = 0;
  let joinedRows = 0;
  let leftHeader: string[] = [];

  for await (const line of leftRl) {
    const leftFields = line.split(",");

    if (leftRowIndex === 0) {
      leftHeader = leftFields;
      // Validate left column index
      if (options.leftColumn >= leftFields.length) {
        console.error(
          `Error: Left column index ${options.leftColumn} is out of range. Left file has only ${
            leftFields.length
          } columns (0-${leftFields.length - 1}).`
        );
        process.exit(1);
      }

      // Create joined header (left header + right header excluding the join column)
      const rightHeaderFiltered = rightHeader.filter((_, index) => index !== options.rightColumn);
      const joinedHeader = [...leftHeader, ...rightHeaderFiltered];

      const canContinue = output.write(joinedHeader.join(",") + "\n");
      if (!canContinue) await new Promise<void>((resolve) => output.once("drain", resolve));
    } else {
      const joinKey = leftFields[options.leftColumn] || "";
      const matchingRightRows = rightData.get(joinKey);

      if (matchingRightRows) {
        // For each matching row in the right table
        for (const rightRow of matchingRightRows) {
          // Exclude the join column from the right row
          const rightRowFiltered = rightRow.filter((_, index) => index !== options.rightColumn);
          const joinedRow = [...leftFields, ...rightRowFiltered];

          const canContinue = output.write(joinedRow.join(",") + "\n");
          if (!canContinue) await new Promise<void>((resolve) => output.once("drain", resolve));

          joinedRows++;
        }
      }
    }
    leftRowIndex++;
  }

  if (options.output) {
    output.end();
    console.log(`Inner join completed. ${joinedRows} rows written to "${outputTarget}".`);
    console.log(
      `Left file: ${leftRowIndex - 1} data rows, Right file: ${rightRowIndex - 1} data rows.`
    );
  }
}

// Entry point
if (process.argv[1] === import.meta.filename) {
  const options = parseArguments();

  try {
    if (options.command === "concat") {
      await concatCSV(options);
    } else if (options.command === "slice") {
      await sliceCSV(options);
    } else if (options.command === "select") {
      await selectCSV(options);
    } else if (options.command === "join") {
      await joinCSV(options);
    }
  } catch (error) {
    console.error("Error:", error instanceof Error ? error.message : String(error));
    process.exit(1);
  }
}
