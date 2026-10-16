// Gateway WebSocket link speaking the same protobuf contract as native clients. The .proto files
// are loaded from the repository so the viewer never duplicates message definitions.
const PROTO_FILES = [
  '/contracts/game/common.proto',
  '/contracts/game/framing.proto',
  '/contracts/game/client_messages.proto',
  '/contracts/game/server_messages.proto',
];

export async function loadContracts() {
  const root = new protobuf.Root();
  // protobufjs resolves imports relative to the root; map "game/x.proto" to our served path.
  root.resolvePath = (origin, target) => (target.startsWith('/') ? target : `/contracts/${target}`);
  await root.load(PROTO_FILES, { keepCase: true });
  return {
    ClientMessage: root.lookupType('sa.game.v1.ClientMessage'),
    ServerMessage: root.lookupType('sa.game.v1.ServerMessage'),
    ProtocolVersion: root.lookupEnum('sa.game.v1.ProtocolVersion'),
    KickReason: root.lookupEnum('sa.game.v1.KickReason'),
  };
}

export class GatewayLink {
  constructor(contracts, handlers) {
    this.contracts = contracts;
    this.handlers = handlers;  // { onWelcome, onSnapshot, onPong, onKick, onClose }
    this.socket = null;
    this.inputSeq = 0;
  }

  connect(url, ticketBase64) {
    this.socket = new WebSocket(url);
    this.socket.binaryType = 'arraybuffer';
    this.socket.onopen = () => {
      const ticket = Uint8Array.from(atob(ticketBase64), c => c.charCodeAt(0));
      this.#send({ hello: { protocol_version: this.contracts.ProtocolVersion.values.PROTOCOL_VERSION_CURRENT, entry_ticket: ticket } });
    };
    this.socket.onmessage = (event) => this.#onMessage(new Uint8Array(event.data));
    this.socket.onclose = (event) => this.handlers.onClose?.(event.code);
    this.socket.onerror = () => {};
  }

  close() { this.socket?.close(1000); }

  sendMove(direction) {
    this.inputSeq += 1;
    this.#send({ move_input: { input_seq: this.inputSeq, direction, client_tick: 0 } });
    return this.inputSeq;
  }

  sendPing(nonce) { this.#send({ ping: { nonce, client_time_ms: Date.now() } }); }

  #send(body) {
    if (this.socket?.readyState !== WebSocket.OPEN) return;
    const message = this.contracts.ClientMessage.create(body);
    this.socket.send(this.contracts.ClientMessage.encode(message).finish());
  }

  #onMessage(bytes) {
    const message = this.contracts.ServerMessage.decode(bytes);
    switch (message.body) {
      case 'welcome': this.handlers.onWelcome?.(message.welcome); break;
      case 'world_snapshot': this.handlers.onSnapshot?.(message.world_snapshot); break;
      case 'pong': this.handlers.onPong?.(message.pong); break;
      case 'kick': this.handlers.onKick?.(this.contracts.KickReason.valuesById[message.kick.reason]); break;
      default: break;
    }
  }
}
