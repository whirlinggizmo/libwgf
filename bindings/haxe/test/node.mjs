// Run a Haxe program built for the JS target under node, on a headless web host:
//   node node.mjs <wgf-host.js> <wgf.js> <program.js> [<directory>=<path in the host> ...]
// The host is made first and the JS binding (bindings/js/wgf.js) attached to it, then
// left at globalThis.wgfJs, where the Haxe binding finds it; each directory given is
// copied into the host's own storage at its path first (a headless host's files are the
// wasm's, so a program's ../assets is /assets).
import { pathToFileURL } from 'node:url';
import { resolve, join, relative } from 'node:path';
import { readdirSync, readFileSync, statSync } from 'node:fs';

const [hostPath, bindingPath, programPath, ...mounts] = process.argv.slice(2);
const createWgfHost = (await import(pathToFileURL(resolve(hostPath)).href)).default;
const host = await createWgfHost();
for (const mount of mounts) {
    const [from, to] = mount.split('=');
    const walk = (dir) => {
        for (const name of readdirSync(dir)) {
            const path = join(dir, name);
            const inside = to + '/' + relative(from, path).split('\\').join('/');
            if (statSync(path).isDirectory()) {
                host["FS"].mkdirTree(inside);
                walk(path);
            } else {
                host["FS"].writeFile(inside, readFileSync(path));
            }
        }
    };
    host["FS"].mkdirTree(to);
    walk(from);
}
const wgf = await import(pathToFileURL(resolve(bindingPath)).href);
wgf.attach(host);
globalThis.wgfJs = wgf;
await import(pathToFileURL(resolve(programPath)).href);
