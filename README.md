# HTTP/1.0 Server

A very basic HTTP/1.0 server in C.

**Warning: Not for Production Use**

This project is built strictly for educational purposes (I mainly wanted to learn C and also some networking along the way). It is **not production-ready** and **should not be used in a production environment**.
Please, do not use in production.


---

## Key Features

* **Event-Driven & Multi-threaded Architecture:** Uses `epoll` in Edge-Triggered mode (`EPOLLET`) on non-blocking sockets. Each worker thread runs its own `epoll` event loop sharing port `8080` via `SO_REUSEPORT`.
* **Zero-Copy State Machine HTTP Parser:** Incrementally parses the method, URI, protocol version, headers, and body using pointers directly into the read buffer, avoiding unnecessary dynamic allocations during parsing.
* **Supported HTTP Methods:**
  * `GET`: Serves static files from the `data/` directory with automatic `Content-Type` detection.
  * `POST`: Creates or overwrites a resource on the filesystem (`201 Created` / `200 OK`).
  * `PUT`: Creates a new resource (`201 Created`) or returns `409 Conflict` if the file already exists.
  * `DELETE`: Removes an existing resource and returns `204 No Content`.
* **Security & Validation:** Protects against Path Traversal attacks (blocking `..` and `\`) and enforces maximum limits on header count and payload size (`413` / `431`).

---

## Requirements

* **Operating System:** Linux (requires `epoll` and `SO_REUSEPORT` APIs)
* **Compiler:** `gcc` with POSIX threads support (`-pthread`)
* **Build Tool:** `make`

---

## Build & Run

1. **Build the project** using the `Makefile`:
   ```bash
   make
   ```

2. **Start the server**:
   ```bash
   ./server
   ```
   The server will listen on port **`8080`**.

3. **Clean build artifacts** (optional):
   ```bash
   make clean
   ```

---

## Usage Examples (`curl`)

* **Read a file (`GET`):**
  ```bash
  curl -i http://localhost:8080/index.html
  ```

---

## Handled HTTP Status Codes

| Code | Status | Description |
| :--- | :--- | :--- |
| **200** | `OK` | `GET` request succeeded or `POST` updated an existing file |
| **201** | `Created` | New resource created via `POST` or `PUT` |
| **204** | `No Content` | Resource successfully deleted via `DELETE` |
| **400** | `Bad Request` | Malformed syntax, unsupported protocol version, or unsafe URI |
| **404** | `Not Found` | Requested resource via `GET` does not exist |
| **405** | `Method Not Allowed` | Unsupported HTTP method |
| **409** | `Conflict` | `PUT` request attempted on an already existing resource |
| **413** | `Request Entity Too Large` | Request or body size exceeds the allowed limit |
| **431** | `Request Header Fields Too Large` | Number of headers exceeds `MAX_HEADERS` (128) |
