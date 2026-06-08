#!/usr/bin/env python3
"""Minimal Synadia Agent Protocol (v0.3) echo responder for local testing.

Listens on the prompt endpoint subject and replies with the streaming chunk
contract (core-protocol.md §6): an `ack` status chunk first, then one or more
`response` chunks echoing the prompt, then the zero-byte header-less terminator.

This is a test stub, NOT a compliant agent — it does not register as a `$SRV`
micro service or publish heartbeats (that arrives with M2 discovery). It exists
so Milestone 1 (static roster + send one prompt + render the streamed reply) can
be exercised end-to-end against a local nats-server.

Run:
    nats-server &                       # if not already running
    python3 scripts/echo-responder.py   # uses nats-py in a venv; see README
"""
import asyncio
import json
import os

from nats.aio.client import Client as NATS

SUBJECT = os.environ.get("ECHO_SUBJECT", "agents.prompt.>")
SERVER = os.environ.get("NATS_URL", "nats://127.0.0.1:4222")


def extract_prompt(raw: bytes) -> str:
    """Per §5.3: a leading '{' means JSON envelope; otherwise plain text."""
    text = raw.decode("utf-8", "replace")
    stripped = text.lstrip(" \t\r\n")
    if stripped.startswith("{"):
        try:
            obj = json.loads(stripped)
            if isinstance(obj, dict) and isinstance(obj.get("prompt"), str):
                return obj["prompt"]
        except json.JSONDecodeError:
            pass
    return text


async def main() -> None:
    nc = NATS()
    await nc.connect(SERVER)

    async def handler(msg):
        if not msg.reply:
            return
        prompt = extract_prompt(msg.data)
        reply = msg.reply

        # 1. mandatory ack (§6.4) — first message on the reply subject.
        await nc.publish(reply, json.dumps({"type": "status", "data": "ack"}).encode())

        # 2. response, split into two chunks to exercise concatenation (§6.3).
        full = f"echo: {prompt}"
        mid = (len(full) + 1) // 2
        for part in (full[:mid], full[mid:]):
            if part:
                await nc.publish(
                    reply, json.dumps({"type": "response", "data": part}).encode()
                )

        # 3. terminator (§6.5): zero-byte body, no headers.
        await nc.publish(reply, b"")
        await nc.flush()
        print(f"echoed {len(prompt)} chars -> {reply}")

    await nc.subscribe(SUBJECT, cb=handler)
    print(f"echo responder listening on {SUBJECT} at {SERVER}")
    try:
        while True:
            await asyncio.sleep(3600)
    except asyncio.CancelledError:
        pass
    finally:
        await nc.drain()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
