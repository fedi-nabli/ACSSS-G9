# MTS web UI

Browser client for the Pi WebSocket protocol ([`docs/ui_contract.md`](../docs/ui_contract.md)).

Full handoff: [`docs/ui_handoff.md`](../docs/ui_handoff.md).

## Scripts

```bash
npm install
npm run mock    # local stand-in for the C++ server, ws://localhost:8765
npm run dev     # http://localhost:5173
npm test
npm run build
```

Connect the UI to `ws://localhost:8765` (mock) or `ws://<pi>:<port>` (hardware). Calibrate, then Start.
