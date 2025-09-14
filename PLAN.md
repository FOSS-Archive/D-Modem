# Plan for Adding Incoming Call Support to D-Modem

## Overview
The current implementation places outbound SIP calls from `d-modem` and is invoked on demand by `slmodemd`. There is no mechanism for registering with a SIP server or handling unsolicited inbound calls. `slmodemd` launches `d-modem` only when dialing a number, and the data socket carries only audio frames.

To support receiving calls, we need a long-lived `d-modem` process that registers with the SIP server, reports incoming call events to `slmodemd`, and answers when the user issues `ATA`.

## Proposed Changes

### 1. Extend `d-modem.c`
- **Add SIP registration for listening**: `pjsua_acc_config` currently disables auto-registration (`cfg.register_on_acc_add = false` at line 236 of `d-modem.c`). Enable registration or call `pjsua_acc_set_registration` so the account can receive calls.
- **Incoming call callback**: Add `cfg.cb.on_incoming_call` to `pjsua_config` and implement a handler that reports the event to `slmodemd` instead of immediately placing a call.
- **Command channel**: Accept an additional file descriptor for control messages. Use it to notify `slmodemd` of `RING`, `CONNECT`, and `DISCONNECT` and to receive commands (`ANSWER`, `DIAL`, `HANGUP`).
- **Listening mode**: Allow starting `d-modem` without a dial string (e.g., `d-modem --listen <audio-fd> <ctrl-fd>`). In this mode, skip the `pjsua_call_make_call` section (lines 248‑254) and wait for incoming calls.
- **Answering calls**: When control channel receives an `ANSWER` command, call `pjsua_call_answer` to accept the pending call and connect audio using existing media callbacks (`on_call_media_state`).

### 2. Modify `slmodemd` (`modem_main.c` and `modem.c`)
- **Startup**: Spawn `d-modem` in listening mode during `socket_start` and pass both the audio and new control sockets via `execl`.
- **Control channel management**: Track the control socket in a global descriptor and monitor it in `modem_run` to receive signalling messages (`RING`, `DISCONNECT`).
- **Command helper**: Provide a global `dmodem_command()` helper so other modules can send text commands to `d-modem` without direct access to the socket.
- **Ring and hangup handling**: `modem_run` calls `modem_ring` when a `RING` message is received and `modem_remote_hangup` on `DISCONNECT`.
- **Answer, dial, and hangup commands**: `modem_answer`, `modem_dial`, and the local branch of `modem_hup` send `ANSWER`, `DIAL <number>`, and `HANGUP` commands respectively through `dmodem_command()`.

### 3. Documentation and Build Scripts
- Update `README.md` to describe new inbound-call capability and how to run `slmodemd`/`d-modem` in listening mode.
- Adjust `Makefile` if new source files or compilation flags are required (e.g., for control channel helper).

## Testing
- Build the project with `make` to ensure new files compile.
- Simulate an inbound call to verify `RING` notifications appear on the TTY and that `ATA` answers the call.
- Confirm outbound calling continues to work using existing procedures.

