# How it talks to Bambuddy

REST only, with the `X-API-Key` header. Written against Bambuddy 1.3.1. The panel names itself
in `User-Agent` (`bambuddy-panel/0.1.0`), so Bambuddy's log shows which firmware asked.

| When | Request |
|---|---|
| Every ~4 s | `GET /api/v1/printers/{id}/status` |
| Every ~4 s | `GET /api/v1/queue/?status=pending,skipped,printing&printer_id={id}` |
| Every ~4 s, not while printing | `GET /api/v1/archives/last-per-printer` |
| Every 60 s | `GET /api/v1/maintenance/printers/{id}` |
| Every 60 s | `GET /api/v1/settings/` (only *Require plate clear* is kept) |
| Every 60 s, for a printer with slots | `GET /api/v1/inventory/assignments?printer_id={id}` |
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

Responses are read through a field filter straight off the socket, so the panel never holds a
whole status document in memory. A button sends the ids the screen showed when it was pressed,
and runs between two reads rather than after a whole round.

Which API key permission each of these needs is under
[Get started](get-started.md#set-up-a-panel).
