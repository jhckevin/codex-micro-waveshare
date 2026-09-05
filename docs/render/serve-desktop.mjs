import {createServer} from "../../desktop/node_modules/vite/dist/node/index.js";
import {tmpdir} from "node:os";
import {join} from "node:path";
import {fileURLToPath} from "node:url";
const server=await createServer({root:fileURLToPath(new URL("../../desktop/",import.meta.url)),cacheDir:join(tmpdir(),"codex-docs-vite-cache"),server:{host:"127.0.0.1",port:14210,strictPort:true}});
await server.listen(); console.log("Documentation preview listening on 127.0.0.1:14210");
