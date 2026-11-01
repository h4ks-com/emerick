# Service security

What can go wrong when untrusted agents drive Emerick through the service, and the limits we apply.

## Threats and limits

| Risk | What we apply | Where |
|---|---|---|
| Memory blowup from huge renders, many voices or Lua | RLIMIT_DATA per session (`EMERICK_SESSION_MEMORY_MB`), `--lua-memory-mb`, `--max-render-seconds` | service, app |
| CPU burn from endless Lua or long renders | `--lua-timeout-ms`, `--max-render-seconds`, idle and age reaping | app, service |
| Disk filling with exports and recordings | RLIMIT_FSIZE per file, per-project quota the reaper checks (`EMERICK_PROJECT_MAX_MB`), RLIMIT_CORE 0 | service |
| Path escape | `--agent-read-root` and `--agent-write-root` set to the project folder; the app resolves symlinks and `..`. File downloads resolve every path and refuse one that leaves the project | app, service |
| Plugin loading | `--bundled-plugins-only`, an empty environment and a session `HOME`, so no `CLAP_PATH` or user plugin folder reaches the app | app, service |
| Too many sessions | `EMERICK_MAX_SESSIONS` and one session per project | service |
| Exposed session MCP | each session binds 127.0.0.1 on a port from `EMERICK_PORT_MIN` to `EMERICK_PORT_MAX`; only `/sessions/{id}/mcp` reaches it | service |
| Network egress | the app needs none; the example NetworkPolicy denies all egress, and with `s3` storage one more rule allows the S3 endpoint | orchestrator |
| Process escape | non-root user 10001, read-only root filesystem with writable `/data` and `/tmp`, no privilege escalation, all capabilities dropped, the runtime's default seccomp profile | image, orchestrator |
| Pids, CPU, memory and scratch disk of the whole pod | requests and limits, `ephemeral-storage`, the kubelet's pid limit | orchestrator |
| Anyone calling the API | a reverse proxy that authenticates every request; the service has no auth of its own | proxy |
| Huge requests | the service reads each MCP request body into memory before passing it to the session, so cap the body size at the proxy; a few MB fits the largest song scripts sent through `lua.eval` (`client_max_body_size 8m` in nginx, a `Buffering` middleware `maxRequestBodyBytes` in Traefik) | proxy |

The sum of `EMERICK_SESSION_MEMORY_MB` over `EMERICK_MAX_SESSIONS` plus about 200 MB for the service should fit the pod's memory limit, so the kernel stops a session before it stops the pod.

## What stays open

- Sessions share the pod's CPU, so one heavy session slows the others.
- RLIMIT_DATA caps each process on its own, so a session and the plugin scanner it starts can each use the whole limit.
- Any authenticated caller can list, open and delete every project; the service knows no owners.
- A crash in the app or a library it links runs as the service user and sees the other sessions' files under `/data`.
- With `s3` storage the pod's credentials reach the whole bucket prefix.

## Example deployment

`deploy/kubernetes.yaml` holds a Deployment, a Service and a NetworkPolicy with these settings. Put an authenticating ingress or proxy in front of the Service.
