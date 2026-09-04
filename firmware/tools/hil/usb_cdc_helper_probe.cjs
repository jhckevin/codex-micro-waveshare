"use strict";
const {spawn} = require("node:child_process");
const path = require("node:path");
const helper = path.resolve(process.argv[2]);
const child = spawn("powershell.exe", [
  "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
  "-File", helper
], {stdio: ["pipe", "pipe", "pipe"]});
child.stdout.on("data", (data) => {
  process.stdout.write(data);
  if (data.toString().includes('"ready"')) {
    child.stdin.write('{"method":"app.hello","params":{"protocol":1}}\n');
  } else if (data.toString().includes('"protocol"')) {
    child.stdin.end("__close__\n");
  }
});
child.stderr.on("data", (data) => process.stderr.write(data));
setTimeout(() => { child.kill(); process.exitCode = 2; }, 6000).unref();
