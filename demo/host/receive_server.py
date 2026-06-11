#!/usr/bin/env python3
"""Minimal host-side receiver for the RK3588 demo upload protocol."""

import base64
import json
import os
from datetime import datetime
from pathlib import Path

from flask import Flask, jsonify, request


BASE_DIR = Path(__file__).resolve().parent
OUTPUT_DIR = Path(os.getenv("DEMO_RECEIVE_DIR", BASE_DIR / "received"))
HOST = os.getenv("DEMO_RECEIVE_HOST", "0.0.0.0")
PORT = int(os.getenv("DEMO_RECEIVE_PORT", "8000"))

app = Flask(__name__)


def decode_and_save(payload, key, output_path):
    encoded = payload.get(key)
    if not encoded:
        return None
    try:
        data = base64.b64decode(encoded, validate=True)
    except (ValueError, TypeError):
        raise ValueError(f"invalid base64 field: {key}")
    output_path.write_bytes(data)
    return output_path.name


@app.get("/health")
def health():
    return jsonify({"status": "ok"})


@app.post("/receive")
def receive():
    payload = request.get_json(silent=True)
    if not isinstance(payload, dict):
        return jsonify({"ok": False, "error": "expected JSON object"}), 400

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    event_id = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    saved = []

    try:
        image_name = decode_and_save(payload, "image", OUTPUT_DIR / f"{event_id}.png")
        if image_name:
            saved.append(image_name)

        video_ext = payload.get("video_ext", ".mp4")
        if not isinstance(video_ext, str) or not video_ext.startswith("."):
            video_ext = ".mp4"
        video_name = decode_and_save(payload, "video", OUTPUT_DIR / f"{event_id}{video_ext}")
        if video_name:
            saved.append(video_name)
    except ValueError as exc:
        return jsonify({"ok": False, "error": str(exc)}), 400

    record = {
        "event_id": event_id,
        "remote_addr": request.remote_addr,
        "text": payload.get("text", ""),
        "files": saved,
    }
    with (OUTPUT_DIR / "events.jsonl").open("a", encoding="utf-8") as log_file:
        log_file.write(json.dumps(record, ensure_ascii=False) + "\n")

    print(f"[receive] {event_id} from {request.remote_addr}: files={saved}")
    if record["text"]:
        print(record["text"])
    return jsonify({"ok": True, "event_id": event_id, "files": saved})


if __name__ == "__main__":
    print(f"Receiving demo uploads at http://{HOST}:{PORT}/receive")
    print(f"Saving received data to {OUTPUT_DIR}")
    app.run(host=HOST, port=PORT, debug=False)
