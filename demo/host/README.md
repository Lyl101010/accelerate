# Host receive side

`receive_server.py` provides the HTTP `/receive` endpoint expected by the
board-side `send_info_to_host()` call. It accepts the JSON upload, saves
base64-encoded images/videos, and records received text in `received/events.jsonl`.

Start the receiver on the computer:

```bash
cd host
python3 receive_server.py
```

The service listens on `0.0.0.0:8000` by default. Check it locally with:

```bash
curl http://127.0.0.1:8000/health
```

Run the board program in offline mode when only local detection and reasoning
are needed:

```bash
DEMO_UPLOAD_MODE=offline ./demo
```

Run it in online mode after this receive server is running:

```bash
DEMO_UPLOAD_MODE=online DEMO_HOST=192.168.0.100:8000 ./demo
```

Optional environment variables:

- `DEMO_RECEIVE_HOST`: listening address, default `0.0.0.0`
- `DEMO_RECEIVE_PORT`: listening port, default `8000`
- `DEMO_RECEIVE_DIR`: output directory, default `host/received`
- `DEMO_UPLOAD_CONNECT_TIMEOUT_SEC`: board connect timeout, default `3`
- `DEMO_UPLOAD_TIMEOUT_SEC`: board total upload timeout, default `60`

`DEMO_UPLOAD=0/1` is still accepted for compatibility; `DEMO_UPLOAD_MODE`
takes priority when both are set.
