import json
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer

HOST = "192.168.43.168"
PORT = 8000

latest_message = None
messages_received = 0


def now_iso():
    return datetime.now(timezone.utc).isoformat()


class SimpleApiHandler(BaseHTTPRequestHandler):
    def _send_json(self, status_code, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _request_path(self):
        return self.path.split("?", 1)[0]

    def do_GET(self):
        global latest_message, messages_received

        route = self._request_path()

        if route == "/health":
            self._send_json(
                200,
                {
                    "status": "ok",
                    "time": now_iso(),
                    "messagesReceived": messages_received,
                },
            )
            return

        if route == "/api/ping":
            self._send_json(
                200,
                {
                    "ok": True,
                    "time": now_iso(),
                    "clientIp": self.client_address[0],
                },
            )
            return

        if route == "/api/last-message":
            self._send_json(
                200,
                {
                    "messagesReceived": messages_received,
                    "latestMessage": latest_message,
                },
            )
            return

        if route == "/api/device-message":
            self._send_json(
                200,
                {
                    "ok": True,
                    "message": "use POST para enviar payload",
                    "messagesReceived": messages_received,
                },
            )
            return

        self._send_json(404, {"error": "rota nao encontrada"})

    def do_POST(self):
        global latest_message, messages_received

        route = self._request_path()
        if route != "/api/device-message":
            self._send_json(404, {"error": "rota nao encontrada"})
            return

        content_length = int(self.headers.get("Content-Length", "0"))
        raw_body = self.rfile.read(content_length).decode("utf-8")

        try:
            payload = json.loads(raw_body) if raw_body else {}
        except json.JSONDecodeError:
            self._send_json(400, {"error": "json invalido"})
            return

        messages_received += 1
        latest_message = {
            "receivedAt": now_iso(),
            "clientIp": self.client_address[0],
            "payload": payload,
        }
        print(f"[POST] {latest_message['receivedAt']} ip={latest_message['clientIp']} payload={payload}")

        self._send_json(
            200,
            {
                "ok": True,
                "messagesReceived": messages_received,
                "receivedAt": latest_message["receivedAt"],
            },
        )

    def log_message(self, format, *args):
        print(f"[HTTP] {self.address_string()} - {format % args}")


def run():
    server = HTTPServer((HOST, PORT), SimpleApiHandler)
    print(f"Servidor ativo em http://{HOST}:{PORT}")
    server.serve_forever()


if __name__ == "__main__":
    run()
