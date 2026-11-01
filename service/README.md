# Emerick service

Runs one headless Emerick per session, so several agents drive their own projects over MCP.

The service has no authentication. Run it behind a reverse proxy that authenticates every request; `docs/service-security.md` lists the rest of what a deployment needs.

## Run

```
docker run --rm -p 127.0.0.1:8080:8080 -v emerick-data:/data emerick
```

Without Docker, from the repo root: `make service-run`.

## Settings

Environment variables. Set a limit to `none` to leave its flag off.

| Variable | Default | Meaning |
|---|---|---|
| `EMERICK_HOST` | `127.0.0.1` (`0.0.0.0` in the image) | address the service binds |
| `EMERICK_PORT` | `8080` | port the service binds |
| `EMERICK_PUBLIC_URL` | request base URL | base of the `mcp_url` the service returns |
| `EMERICK_BIN` | `emerick` (`/opt/emerick/emerick` in the image) | the Emerick binary |
| `EMERICK_DATA_DIR` | `data` (`/data` in the image) | database and local projects |
| `EMERICK_WORK_DIR` | `data/sessions` (`/tmp/emerick-sessions` in the image) | per-session profiles, logs and S3 checkouts |
| `EMERICK_STORAGE` | `local` | `local` or `s3` |
| `EMERICK_S3_BUCKET` | | bucket for `s3` |
| `EMERICK_S3_PREFIX` | `projects/` | key prefix for `s3` |
| `EMERICK_S3_ENDPOINT_URL` | | S3 endpoint, such as a MinIO URL; credentials come from the usual `AWS_*` variables |
| `EMERICK_MAX_SESSIONS` | `4` | live sessions at once; more answer 429 |
| `EMERICK_SESSION_IDLE_SECONDS` | `900` | close a session with no MCP call for this long |
| `EMERICK_SESSION_MAX_SECONDS` | `14400` | close a session this old |
| `EMERICK_REAPER_INTERVAL_SECONDS` | `15` | how often we check sessions for idle time, age and disk use |
| `EMERICK_START_TIMEOUT_SECONDS` | `30` | wait this long for a session's MCP |
| `EMERICK_STOP_TIMEOUT_SECONDS` | `10` | wait this long after SIGTERM before SIGKILL |
| `EMERICK_PORT_MIN`, `EMERICK_PORT_MAX` | `20000`, `20999` | loopback ports for sessions |
| `EMERICK_SESSION_MEMORY_MB` | `2048` | RLIMIT_DATA of a session |
| `EMERICK_SESSION_MAX_FILE_MB` | `1024` | RLIMIT_FSIZE of a session |
| `EMERICK_SESSION_MAX_OPEN_FILES` | `1024` | RLIMIT_NOFILE of a session |
| `EMERICK_PROJECT_MAX_MB` | `2048` | close a session whose project grows past this |
| `EMERICK_LUA_TIMEOUT_MS` | `30000` | `--lua-timeout-ms` |
| `EMERICK_LUA_MEMORY_MB` | `256` | `--lua-memory-mb` |
| `EMERICK_MAX_RENDER_SECONDS` | `900` | `--max-render-seconds` |
| `EMERICK_BUNDLED_PLUGINS_ONLY` | `true` | `--bundled-plugins-only` |

At startup the service reads `emerick --help` and exits naming any flag the binary lacks.

## API

| Method and path | Does |
|---|---|
| `GET /health` | answers `ok` with the live session count |
| `POST /projects` `{"name"}` | creates a project |
| `GET /projects`, `GET /projects/{id}` | lists or reads projects |
| `DELETE /projects/{id}` | deletes a project and its files, 409 while it has a live session |
| `GET /projects/{id}/files` | lists the project's files |
| `GET /projects/{id}/files/{path}` | downloads one file |
| `PUT /projects/{id}/files/{path}` | uploads one file (the body), 413 past `EMERICK_PROJECT_MAX_MB`; a live session sees it at once |
| `GET /projects/{id}/bundle?path=song.erk` | downloads a zip of the bundle folder, to open in Emerick |
| `POST /sessions` `{"project_id"}` | starts a session, 429 when the pool is full, 409 when the project has one |
| `GET /sessions`, `GET /sessions/{id}` | lists or reads sessions, `?live=true` for live ones only |
| `POST /sessions/{id}/save` | saves the project and stores it |
| `DELETE /sessions/{id}` | saves the project, stops the session and stores the project |
| `POST /sessions/{id}/mcp` | the session's Emerick MCP |
| `POST /mcp` | the service's MCP, below |

## Agents

Register one MCP server, `<service url>/mcp`. Its tools are `project.create`, `project.list`, `project.upload`, `project.bundle`, `session.create`, `session.list`, `session.tools`, `session.call` and `session.close`.

```
project.create {name}                       -> {id: P}
session.create {project_id: P}              -> {id: S, project_dir, mcp_url}
session.call {session_id: S, tool: "lua.eval", arguments: {code}}
session.call {session_id: S, tool: "actions.run", arguments: {id, params}}
session.close {session_id: S}
```

- The MCP endpoint is stateless and every call names its session. The session itself is stateful: one Emerick keeps the project open between calls until `session.close`, or until it idles for `EMERICK_SESSION_IDLE_SECONDS`. Both save the project first.
- An agent that loses its session id finds it with `session.list`, from any connection.
- One agent can run several sessions, one per project, up to `EMERICK_MAX_SESSIONS` for the whole service.
- Agents that pass a session id between them share that session. Its Emerick handles one call at a time, so hand the project over between agents rather than editing it from two at once.
- An agent that can add MCP servers while it runs can use the session's `mcp_url` directly, with the same tools `session.tools` lists.

A live session's `project_dir` is the folder its Emerick works in, with the bundle at `project.erk`. Emerick reads and writes only inside that folder, so an agent exports there, such as `export.mix` with `{"path": "<project_dir>/mix.wav"}`.

The `skills/` folder holds song production skills for the agent that drives a session; the agent loads them, and the image leaves them out.

## Example

```
base=http://127.0.0.1:8080
project=$(curl -s -X POST $base/projects -H 'Content-Type: application/json' -d '{"name":"demo"}' | jq -r .id)
session=$(curl -s -X POST $base/sessions -H 'Content-Type: application/json' -d "{\"project_id\":\"$project\"}")
mcp=$(echo "$session" | jq -r .mcp_url)
curl -s -X POST "$mcp" -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"actions.run","arguments":{"id":"project.summary"}}}'
curl -s -X DELETE $base/sessions/$(echo "$session" | jq -r .id)
```
