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
    RejectCode: root.lookupEnum('sa.game.v1.RejectCode'),
    EntityKind: root.lookupEnum('sa.game.v1.EntityKind'),
    CommandStatus: root.lookupEnum('sa.game.v1.CommandStatus'),
    TradeStatus: root.lookupEnum('sa.game.v1.TradeStatus'),
  };
}

export class GatewayLink {
  constructor(contracts, handlers) {
    this.contracts = contracts;
    this.handlers = handlers;  // { onWelcome, onSnapshot, onPong, onKick, onCommandResult, onCombatEvent, onInventoryUpdate, onTradeUpdate, onClose }
    this.socket = null;
    this.inputSeq = 0;
    this.requestId = 0;
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

  // Targeted skill at an entity, or a projectile skill along a direction. Returns the request id.
  sendCast(skillId, target) {
    this.requestId += 1;
    const body = { request_id: this.requestId, skill_id: skillId };
    if (typeof target === 'object' && 'x' in target) body.direction = target; else body.target_entity_id = target;
    this.#send({ cast_skill: body });
    return this.requestId;
  }

  // Asks the zone for a ground item by its entity id. Returns the request id; the result may
  // arrive as PENDING first and as the final verdict once the Backend has committed.
  sendPickup(itemEntityId) {
    this.requestId += 1;
    this.#send({ pickup_item: { request_id: this.requestId, item_entity_id: itemEntityId } });
    return this.requestId;
  }

  // Asks to leave through a portal. The answer is PENDING, then the destination zone's Welcome.
  // A retry after an unknown outcome passes the same request id, so the zone asks the Backend
  // about the same transfer.
  sendChangeZone(portalId, requestId = null) {
    requestId = this.#requestIdOr(requestId);
    this.#send({ change_zone: { request_id: requestId, portal_id: portalId } });
    return requestId;
  }

  // Trade steps (ADR-0013). The Backend owns the negotiation and the zone relays it: each step is
  // answered with a CommandResult, and the trade itself arrives as TradeUpdate to both parties.
  // Like ChangeZone, a retry after an unknown outcome passes the same request id.
  sendTradeOpen(targetEntityId, requestId = null) {
    requestId = this.#requestIdOr(requestId);
    this.#send({ trade_open: { request_id: requestId, target_entity_id: targetEntityId } });
    return requestId;
  }

  // Replaces our whole offer: the given gold and nothing from the inventory.
  sendTradeOffer(tradeId, gold, requestId = null) {
    requestId = this.#requestIdOr(requestId);
    this.#send({ trade_offer: { request_id: requestId, trade_id: tradeId, item_instance_ids: [], gold } });
    return requestId;
  }

  // Agrees to the offer as of `version`; the Backend refuses if it changed since.
  sendTradeAccept(tradeId, version, requestId = null) {
    requestId = this.#requestIdOr(requestId);
    this.#send({ trade_accept: { request_id: requestId, trade_id: tradeId, version } });
    return requestId;
  }

  sendTradeCancel(tradeId, requestId = null) {
    requestId = this.#requestIdOr(requestId);
    this.#send({ trade_cancel: { request_id: requestId, trade_id: tradeId } });
    return requestId;
  }

  // The retried request's id, or a fresh one.
  #requestIdOr(requestId) {
    if (requestId !== null) return requestId;
    this.requestId += 1;
    return this.requestId;
  }

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
      case 'command_result': this.handlers.onCommandResult?.(message.command_result, this.contracts.RejectCode.valuesById[message.command_result.reject_code]); break;
      case 'combat_event': this.handlers.onCombatEvent?.(message.combat_event); break;
      case 'inventory_update': this.handlers.onInventoryUpdate?.(message.inventory_update); break;
      case 'trade_update': this.handlers.onTradeUpdate?.(message.trade_update, this.contracts.TradeStatus.valuesById[message.trade_update.status]); break;
      default: break;
    }
  }
}
