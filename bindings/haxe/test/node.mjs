// Run a Haxe program built for the JS target under node, on a headless web host:
//   node node.mjs <wgf-host.js> <program.js>
// The host is made first and left at globalThis.wgfHost, where wgf.Runtime.run finds it.
import { pathToFileURL } from 'node:url';
import { resolve } from 'node:path';

const [hostPath, programPath] = process.argv.slice(2);
const createWgfHost = (await import(pathToFileURL(resolve(hostPath)).href)).default;
globalThis.wgfHost = await createWgfHost();
await import(pathToFileURL(resolve(programPath)).href);
