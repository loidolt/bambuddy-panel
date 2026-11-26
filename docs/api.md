# How it talks to Bambuddy

REST only, with the `X-API-Key` header. Routes and payloads were checked against the Bambuddy 1.3.1 and 1.3.2 source trees. Some versioned routes are absent from the wiki reference. The panel names itself
in `User-Agent` (`bambuddy-panel/0.1.0`), so Bambuddy's log shows which firmware asked.

| When | Request |
|---|---|
| Each poll | `GET /api/v1/printers/{id}/status` |
| Each poll | `GET /api/v1/queue/?status=pending,skipped,printing&printer_id={id}` |
| Each poll, not while printing | `GET /api/v1/archives/last-per-printer` |
| About every 60 s | `GET /api/v1/maintenance/printers/{id}` |
| About every 60 s | `GET /api/v1/settings/` (only *Require plate clear* is kept) |
| About every 60 s, for a printer with slots | `GET /api/v1/inventory/assignments?printer_id={id}` |
| When the spool list is opened | `GET /api/v1/inventory/spools` (the first 150 are kept) |
| Pause, resume, stop | `POST /api/v1/printers/{id}/print/pause` · `resume` · `stop` |
| Plate is clear | `POST /api/v1/printers/{id}/clear-plate` |
| Clear error | `POST /api/v1/printers/{id}/hms/clear` |
| Start | `POST /api/v1/queue/{item}/start` |
| Resume queue | `POST /api/v1/queue/printer/{id}/resume` |
| Thumbs | `PATCH /api/v1/archives/{id}` with `user_verdict`, `user_verdict_source` and `run_id` |
| Task done | `POST /api/v1/maintenance/items/{id}/perform` |
| Assign a spool | `POST /api/v1/inventory/assignments` with `spool_id`, `printer_id`, `ams_id`, `tray_id`, `move` |
| Remove a spool | `DELETE /api/v1/inventory/assignments/{printer}/{ams}/{tray}` |

Setup lists printers with `GET /api/v1/printers/` and checks the selected printer’s status. In normal operation the worker waits about four seconds **between completed polling rounds**. A command wakes it early. The worker rereads its target before sending, then polls again before enabling another write. Bambuddy handles the actual scheduling and printer acknowledgement. A printer can still change state between the final read and the command.

Each response passes through a field filter with a 40 KiB JSON allocation budget and a 512 KiB body limit. When a response is too large or incomplete, controls that depend on it wait for fresh data. Outcome writes require both archive and run ids.

Which API key permission each of these needs is under
[Get started](get-started.md#set-up-a-panel).
