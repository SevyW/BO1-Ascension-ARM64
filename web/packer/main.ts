// main.ts - entry point of bo1-pack (cli.ts): `bun packer/main.ts <folder>` here, `node bo1-pack.mjs <folder>` bundled.

import { main } from "./cli";

main(process.argv.slice(2)).then(
  (code) => process.exit(code),
  (e: unknown) => {
    process.stderr.write(`error: ${e instanceof Error ? e.message : String(e)}\n`);
    process.exit(1);
  },
);
