#!/usr/bin/env python3
"""Headless end-to-end test of the M1 stack — no display, no manual steps.

Orchestrates the whole loop in one process:
  1. starts a local nats-server,
  2. runs the §6 echo responder in-process (nats-py),
  3. runs the app's built-in smoke path (AGENT_CHAT_SMOKE) against it,
  4. asserts the streamed reply echoes the prompt, then tears everything down.

This exercises the real C++ NatsClient + AgentProtocol over the wire (handshake,
SUB/PUB, MSG/HMSG framing, ack/response chunks, headerless terminator) without a
GUI, so it works on a host with no display and is suitable for CI.

Usage:
    QT_ROOT=~/Qt/6.8.2/gcc_64 python3 scripts/smoke-test.py [path-to-binary]
Requires nats-server on PATH and nats-py (e.g. ~/.venvs/natstest).
"""
import asyncio
import json
import os
import subprocess
import sys

from nats.aio.client import Client as NATS

PROMPT = "hello from milestone 1"
SUBJECT = "agents.prompt.echo.local.test"


async def serve_echo(nc: NATS) -> None:
    async def handler(msg):
        if not msg.reply:
            return
        raw = msg.data.decode("utf-8", "replace").lstrip()
        prompt = raw
        if raw.startswith("{"):
            try:
                prompt = json.loads(raw).get("prompt", raw)
            except json.JSONDecodeError:
                pass
        await nc.publish(msg.reply, json.dumps({"type": "status", "data": "ack"}).encode())
        full = f"echo: {prompt}"
        mid = (len(full) + 1) // 2
        for part in (full[:mid], full[mid:]):
            await nc.publish(msg.reply, json.dumps({"type": "response", "data": part}).encode())
        await nc.publish(msg.reply, b"")  # terminator
        await nc.flush()

    await nc.subscribe("agents.prompt.>", cb=handler)


async def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build-desktop/rm-agents"
    qt_root = os.environ.get("QT_ROOT", os.path.expanduser("~/Qt/6.8.2/gcc_64"))
    if not os.path.exists(binary):
        print(f"binary not found: {binary} (build it first)", file=sys.stderr)
        return 3

    server = subprocess.Popen(
        ["nats-server", "-p", "4222"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        await asyncio.sleep(1.0)
        nc = NATS()
        await nc.connect("nats://127.0.0.1:4222")
        await serve_echo(nc)

        env = dict(os.environ)
        env["AGENT_CHAT_SMOKE"] = PROMPT
        env["AGENT_CHAT_SMOKE_SUBJECT"] = SUBJECT
        env["QT_QPA_PLATFORM"] = "offscreen"
        env["LD_LIBRARY_PATH"] = os.path.join(qt_root, "lib") + ":" + env.get("LD_LIBRARY_PATH", "")

        proc = await asyncio.create_subprocess_exec(
            binary, env=env,
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT,
        )
        out_b, _ = await asyncio.wait_for(proc.communicate(), timeout=15)
        out = out_b.decode()
        print(out, end="" if out.endswith("\n") else "\n")

        await nc.drain()

        # The reply streams as multiple `response` chunks, each logged on its own
        # line; reconstruct the whole reply before asserting.
        chunks = [
            line.split("response chunk: ", 1)[1]
            for line in out.splitlines()
            if "response chunk: " in line
        ]
        reply = "".join(chunks)
        ok = (
            proc.returncode == 0
            and "[smoke] complete" in out
            and reply == "echo: " + PROMPT
        )
        print(f"reconstructed reply: {reply!r}")
        print(f"RESULT: {'PASS' if ok else 'FAIL'} (smoke_exit={proc.returncode})")
        return 0 if ok else 1
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()


if __name__ == "__main__":
    try:
        sys.exit(asyncio.run(main()))
    except KeyboardInterrupt:
        sys.exit(130)
