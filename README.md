# Modeling Tremor Suppression

Advanced Computer Science studies in Sweden, Group 9.

## Authors

- Chadbourne David
- Izzy Cahling
- Fedi Nabli
- Neil Naik

## Docs

- [`docs/ui_contract.md`](docs/ui_contract.md) — WebSocket protocol v1 (Pi C++ server ↔ browser)
- [`docs/ui_handoff.md`](docs/ui_handoff.md) — How to run the web UI, mock Pi, and protocol client

The UI lives in `web/` (branch `feat/websocket`). The Pi does not serve that page; the browser connects to `ws://<pi>:<port>`.


