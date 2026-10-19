// REST calls to the Backend (login, characters, entry ticket). Mirrors tools/loadgen BackendApi.
export class BackendApi {
  constructor(baseUrl) {
    this.baseUrl = baseUrl.replace(/\/$/, '');
    this.token = null;
  }

  async #call(method, path, body) {
    const headers = { 'content-type': 'application/json' };
    if (this.token) headers.authorization = `Bearer ${this.token}`;
    const response = await fetch(this.baseUrl + path, {
      method, headers, body: body === undefined ? undefined : JSON.stringify(body),
    });
    const text = await response.text();
    const json = text ? JSON.parse(text) : null;
    if (!response.ok) {
      const detail = json?.detail ?? json?.title ?? response.statusText;
      throw new Error(`${method} ${path}: ${response.status} ${detail}`);
    }
    return json;
  }

  register(username, password) { return this.#call('POST', '/api/v1/auth/register', { username, password }); }

  async login(username, password) {
    const result = await this.#call('POST', '/api/v1/auth/login', { username, password });
    this.token = result.accessToken;
    return result;
  }

  listCharacters(worldId) { return this.#call('GET', `/api/v1/worlds/${worldId}/characters`); }

  createCharacter(worldId, name, classId) {
    return this.#call('POST', `/api/v1/worlds/${worldId}/characters`, { name, classId });
  }

  // Returns { entryTicket (base64), gateway: { host, port }, expiresAt }.
  enter(worldId, characterId) { return this.#call('POST', `/api/v1/worlds/${worldId}/characters/${characterId}/enter`); }
}
