// Syntax check only: no execution of Edge code, network access or production calls.
import { readFileSync } from 'node:fs';
import { stripTypeScriptTypes } from 'node:module';
import { spawnSync } from 'node:child_process';
const source = readFileSync(new URL('../../supabase/functions/truckbox-diagnostics/index.ts', import.meta.url), 'utf8');
const checked = spawnSync(process.execPath, ['--check', '--input-type=module'], {
  input: stripTypeScriptTypes(source, { mode: 'transform' }), encoding: 'utf8',
});
if (checked.status !== 0) { process.stderr.write(checked.stderr); process.exit(1); }
console.log('Edge TypeScript syntax PASS; deployment and live integration not performed.');
