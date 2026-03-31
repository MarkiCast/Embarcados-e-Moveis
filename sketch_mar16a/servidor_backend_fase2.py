import json
import random
import sqlite3
import threading
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


HOST = "0.0.0.0"
PORT = 8100
DB_PATH = Path(__file__).with_name("backend_fase2.sqlite3")
FLORIPA_LAT = -27.5954
FLORIPA_LON = -48.5480
OPEN_METEO_URL = (
    "https://api.open-meteo.com/v1/forecast"
    f"?latitude={FLORIPA_LAT}&longitude={FLORIPA_LON}&current=temperature_2m&timezone=UTC"
)
DEFAULT_MOCK_INTERVAL_SECONDS = 20
BACKFILL_DAYS = 7


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def parse_iso_or_none(value: str | None) -> str | None:
    if not value:
        return None
    normalized = value.strip()
    if normalized.endswith("Z"):
        normalized = normalized[:-1] + "+00:00"
    parsed = datetime.fromisoformat(normalized)
    return parsed.astimezone(timezone.utc).isoformat()


class Phase2Backend:
    def __init__(self, db_path: Path) -> None:
        self.db_path = db_path
        self.lock = threading.RLock()
        self.db = sqlite3.connect(str(db_path), check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        self.last_floripa_temp: float | None = None
        self.last_floripa_timestamp: str | None = None
        self.last_fetch_provider: str | None = None
        self.last_error: str | None = None
        self.mock_interval_seconds = DEFAULT_MOCK_INTERVAL_SECONDS
        self.mock_running = False
        self.mock_thread: threading.Thread | None = None
        self.mock_stop_event = threading.Event()
        self._init_db()
        self.backfill_floripa_history(days=BACKFILL_DAYS)

    def _init_db(self) -> None:
        with self.lock:
            self.db.execute(
                """
                CREATE TABLE IF NOT EXISTS room_readings (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    timestamp TEXT NOT NULL,
                    temp_c REAL NOT NULL,
                    source TEXT NOT NULL
                )
                """
            )
            self.db.execute(
                """
                CREATE TABLE IF NOT EXISTS floripa_readings (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    timestamp TEXT NOT NULL,
                    temp_c REAL NOT NULL,
                    provider TEXT NOT NULL
                )
                """
            )
            self.db.execute(
                """
                CREATE TABLE IF NOT EXISTS comparisons (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    timestamp TEXT NOT NULL,
                    temp_room_c REAL NOT NULL,
                    temp_floripa_c REAL NOT NULL,
                    diff_c REAL NOT NULL,
                    source TEXT NOT NULL,
                    provider TEXT NOT NULL
                )
                """
            )
            self.db.execute("CREATE INDEX IF NOT EXISTS idx_room_ts ON room_readings(timestamp)")
            self.db.execute("CREATE INDEX IF NOT EXISTS idx_floripa_ts ON floripa_readings(timestamp)")
            self.db.execute("CREATE INDEX IF NOT EXISTS idx_comp_ts ON comparisons(timestamp)")
            self.db.commit()

    def _fetch_floripa_live(self) -> tuple[float, str]:
        request = urllib.request.Request(
            OPEN_METEO_URL,
            method="GET",
            headers={"User-Agent": "fase2-backend/1.0"},
        )
        with urllib.request.urlopen(request, timeout=8) as response:
            data = json.loads(response.read().decode("utf-8"))
        current = data.get("current", {})
        temperature = current.get("temperature_2m")
        if temperature is None:
            raise ValueError("API Open-Meteo sem temperature_2m")
        value = float(temperature)
        self.last_floripa_temp = value
        self.last_floripa_timestamp = now_iso()
        self.last_fetch_provider = "open-meteo-live"
        return value, "open-meteo-live"

    def _fetch_floripa_archive(self, days: int) -> list[tuple[str, float]]:
        now_utc = datetime.now(timezone.utc)
        start_utc = now_utc - timedelta(days=max(1, days))
        archive_url = (
            "https://archive-api.open-meteo.com/v1/archive"
            f"?latitude={FLORIPA_LAT}&longitude={FLORIPA_LON}"
            f"&start_date={start_utc.strftime('%Y-%m-%d')}"
            f"&end_date={now_utc.strftime('%Y-%m-%d')}"
            "&hourly=temperature_2m&timezone=UTC"
        )
        request = urllib.request.Request(
            archive_url,
            method="GET",
            headers={"User-Agent": "fase2-backend/1.0"},
        )
        with urllib.request.urlopen(request, timeout=12) as response:
            data = json.loads(response.read().decode("utf-8"))
        hourly = data.get("hourly", {})
        times = hourly.get("time") or []
        temps = hourly.get("temperature_2m") or []
        if len(times) != len(temps):
            raise ValueError("API archive retornou arrays com tamanhos diferentes")

        rows: list[tuple[str, float]] = []
        for time_text, temp in zip(times, temps):
            if temp is None:
                continue
            iso = f"{time_text}:00+00:00"
            rows.append((parse_iso_or_none(iso), float(temp)))
        return rows

    def backfill_floripa_history(self, days: int = 7) -> dict:
        now_utc = datetime.now(timezone.utc)
        from_utc = now_utc - timedelta(days=max(1, days))
        from_iso = from_utc.isoformat()
        with self.lock:
            existing = self.db.execute(
                """
                SELECT COUNT(*) AS c
                FROM floripa_readings
                WHERE provider = 'open-meteo-archive'
                  AND timestamp >= ?
                """,
                (from_iso,),
            ).fetchone()
        if int(existing["c"]) >= 24 * max(1, days) - 3:
            return {"inserted": 0, "skipped": True}

        try:
            rows = self._fetch_floripa_archive(days=days)
            inserted = 0
            with self.lock:
                for ts, temp_c in rows:
                    duplicate = self.db.execute(
                        """
                        SELECT 1
                        FROM floripa_readings
                        WHERE timestamp = ? AND provider = 'open-meteo-archive'
                        LIMIT 1
                        """,
                        (ts,),
                    ).fetchone()
                    if duplicate:
                        continue
                    self.db.execute(
                        "INSERT INTO floripa_readings(timestamp, temp_c, provider) VALUES(?, ?, ?)",
                        (ts, temp_c, "open-meteo-archive"),
                    )
                    inserted += 1
                self.db.commit()
            return {"inserted": inserted, "skipped": False}
        except (urllib.error.URLError, TimeoutError, ValueError, json.JSONDecodeError) as exc:
            with self.lock:
                self.last_error = f"{now_iso()} | falha backfill archive: {exc}"
            return {"inserted": 0, "skipped": False, "error": str(exc)}

    def get_floripa_temp(self, cache_max_age_seconds: int = 600) -> tuple[float, str]:
        with self.lock:
            if self.last_floripa_temp is not None and self.last_floripa_timestamp:
                age = (
                    datetime.now(timezone.utc)
                    - datetime.fromisoformat(self.last_floripa_timestamp)
                ).total_seconds()
                if age <= cache_max_age_seconds:
                    return self.last_floripa_temp, "open-meteo-cache"
        try:
            return self._fetch_floripa_live()
        except (urllib.error.URLError, TimeoutError, ValueError, json.JSONDecodeError) as exc:
            with self.lock:
                self.last_error = f"{now_iso()} | falha open-meteo: {exc}"
                if self.last_floripa_temp is not None:
                    return self.last_floripa_temp, "open-meteo-stale-cache"
            raise RuntimeError(f"falha ao buscar Open-Meteo: {exc}") from exc

    def ingest_temperature(self, temp_c: float, timestamp: str | None, source: str) -> dict:
        ts = parse_iso_or_none(timestamp) or now_iso()
        room_temp = float(temp_c)
        floripa_temp, provider = self.get_floripa_temp()
        diff = room_temp - floripa_temp
        with self.lock:
            self.db.execute(
                "INSERT INTO room_readings(timestamp, temp_c, source) VALUES(?, ?, ?)",
                (ts, room_temp, source),
            )
            self.db.execute(
                "INSERT INTO floripa_readings(timestamp, temp_c, provider) VALUES(?, ?, ?)",
                (ts, floripa_temp, provider),
            )
            self.db.execute(
                """
                INSERT INTO comparisons(timestamp, temp_room_c, temp_floripa_c, diff_c, source, provider)
                VALUES(?, ?, ?, ?, ?, ?)
                """,
                (ts, room_temp, floripa_temp, diff, source, provider),
            )
            self.db.commit()
        return {
            "timestamp": ts,
            "tempRoomC": round(room_temp, 2),
            "tempFloripaC": round(floripa_temp, 2),
            "diffC": round(diff, 2),
            "source": source,
            "provider": provider,
        }

    def _build_time_filter(self, from_ts: str | None, to_ts: str | None) -> tuple[str, list]:
        clauses = []
        params: list = []
        if from_ts:
            clauses.append("timestamp >= ?")
            params.append(parse_iso_or_none(from_ts))
        if to_ts:
            clauses.append("timestamp <= ?")
            params.append(parse_iso_or_none(to_ts))
        where = f"WHERE {' AND '.join(clauses)}" if clauses else ""
        return where, params

    def get_current(self) -> dict | None:
        with self.lock:
            row = self.db.execute(
                """
                SELECT timestamp, temp_room_c, temp_floripa_c, diff_c, source, provider
                FROM comparisons
                ORDER BY timestamp DESC
                LIMIT 1
                """
            ).fetchone()
        if not row:
            return None
        return {
            "timestamp": row["timestamp"],
            "tempRoomC": round(row["temp_room_c"], 2),
            "tempFloripaC": round(row["temp_floripa_c"], 2),
            "diffC": round(row["diff_c"], 2),
            "source": row["source"],
            "provider": row["provider"],
        }

    def get_history(self, from_ts: str | None, to_ts: str | None, limit: int) -> list[dict]:
        where, params = self._build_time_filter(from_ts, to_ts)
        sql = f"""
            SELECT timestamp, temp_room_c, temp_floripa_c, diff_c, source, provider
            FROM comparisons
            {where}
            ORDER BY timestamp DESC
            LIMIT ?
        """
        with self.lock:
            rows = self.db.execute(sql, [*params, max(1, min(limit, 5000))]).fetchall()
        return [
            {
                "timestamp": row["timestamp"],
                "tempRoomC": round(row["temp_room_c"], 2),
                "tempFloripaC": round(row["temp_floripa_c"], 2),
                "diffC": round(row["diff_c"], 2),
                "source": row["source"],
                "provider": row["provider"],
            }
            for row in rows
        ]

    def get_floripa_history(self, from_ts: str | None, to_ts: str | None, limit: int) -> list[dict]:
        where, params = self._build_time_filter(from_ts, to_ts)
        sql = f"""
            SELECT timestamp, temp_c, provider
            FROM floripa_readings
            {where}
            ORDER BY timestamp DESC
            LIMIT ?
        """
        with self.lock:
            rows = self.db.execute(sql, [*params, max(1, min(limit, 5000))]).fetchall()
        return [
            {
                "timestamp": row["timestamp"],
                "tempC": round(row["temp_c"], 2),
                "provider": row["provider"],
            }
            for row in rows
        ]

    def get_hourly_stats(self, from_ts: str | None, to_ts: str | None) -> list[dict]:
        return self.get_interval_stats(from_ts=from_ts, to_ts=to_ts, interval_minutes=60)

    def get_interval_stats(self, from_ts: str | None, to_ts: str | None, interval_minutes: int) -> list[dict]:
        safe_interval = max(30, min(int(interval_minutes), 240))
        bucket_seconds = safe_interval * 60
        where, params = self._build_time_filter(from_ts, to_ts)
        sql = f"""
            SELECT
                strftime(
                    '%Y-%m-%dT%H:%M:%S+00:00',
                    datetime((CAST(strftime('%s', timestamp) AS INTEGER) / ?) * ?, 'unixepoch')
                ) AS period_bucket,
                COUNT(*) AS count,
                AVG(temp_room_c) AS avg_room,
                MIN(temp_room_c) AS min_room,
                MAX(temp_room_c) AS max_room,
                AVG(temp_floripa_c) AS avg_floripa,
                MIN(temp_floripa_c) AS min_floripa,
                MAX(temp_floripa_c) AS max_floripa,
                AVG(diff_c) AS avg_diff,
                MIN(diff_c) AS min_diff,
                MAX(diff_c) AS max_diff
            FROM comparisons
            {where}
            GROUP BY period_bucket
            ORDER BY period_bucket ASC
        """
        with self.lock:
            rows = self.db.execute(sql, [bucket_seconds, bucket_seconds, *params]).fetchall()
        return [
            {
                "periodStart": row["period_bucket"],
                "intervalMinutes": safe_interval,
                "count": row["count"],
                "avgRoomC": round(row["avg_room"], 2),
                "minRoomC": round(row["min_room"], 2),
                "maxRoomC": round(row["max_room"], 2),
                "avgFloripaC": round(row["avg_floripa"], 2),
                "minFloripaC": round(row["min_floripa"], 2),
                "maxFloripaC": round(row["max_floripa"], 2),
                "avgDiffC": round(row["avg_diff"], 2),
                "minDiffC": round(row["min_diff"], 2),
                "maxDiffC": round(row["max_diff"], 2),
            }
            for row in rows
        ]

    def get_summary_stats(
        self,
        from_ts: str | None,
        to_ts: str | None,
        tolerance_c: float,
        interval_minutes: int = 30,
    ) -> dict:
        safe_interval = max(30, min(int(interval_minutes), 240))
        bucket_seconds = safe_interval * 60
        where, params = self._build_time_filter(from_ts, to_ts)
        sql = f"""
            WITH bucketed AS (
                SELECT
                    datetime((CAST(strftime('%s', timestamp) AS INTEGER) / ?) * ?, 'unixepoch') AS bucket_utc,
                    AVG(temp_room_c) AS avg_room,
                    AVG(temp_floripa_c) AS avg_floripa,
                    AVG(diff_c) AS avg_diff
                FROM comparisons
                {where}
                GROUP BY bucket_utc
            )
            SELECT
                COUNT(*) AS count,
                AVG(avg_room) AS avg_room,
                AVG(avg_floripa) AS avg_floripa,
                AVG(avg_diff) AS avg_diff,
                MIN(avg_diff) AS min_diff,
                MAX(avg_diff) AS max_diff,
                SUM(CASE WHEN avg_diff > 0 THEN 1 ELSE 0 END) AS above_count,
                SUM(CASE WHEN abs(avg_diff) <= ? THEN 1 ELSE 0 END) AS within_count
            FROM bucketed
        """
        with self.lock:
            row = self.db.execute(sql, [bucket_seconds, bucket_seconds, *params, tolerance_c]).fetchone()
        count = int(row["count"] or 0)
        if count == 0:
            return {
                "count": 0,
                "intervalMinutes": safe_interval,
                "toleranceC": tolerance_c,
                "avgRoomC": None,
                "avgFloripaC": None,
                "avgDiffC": None,
                "minDiffC": None,
                "maxDiffC": None,
                "aboveFloripaPct": 0.0,
                "withinTolerancePct": 0.0,
            }
        return {
            "count": count,
            "intervalMinutes": safe_interval,
            "toleranceC": tolerance_c,
            "avgRoomC": round(row["avg_room"], 2),
            "avgFloripaC": round(row["avg_floripa"], 2),
            "avgDiffC": round(row["avg_diff"], 2),
            "minDiffC": round(row["min_diff"], 2),
            "maxDiffC": round(row["max_diff"], 2),
            "aboveFloripaPct": round((row["above_count"] / count) * 100, 2),
            "withinTolerancePct": round((row["within_count"] / count) * 100, 2),
        }

    def _row_count(self, table_name: str) -> int:
        with self.lock:
            row = self.db.execute(f"SELECT COUNT(*) AS c FROM {table_name}").fetchone()
        return int(row["c"])

    def health(self) -> dict:
        return {
            "ok": True,
            "time": now_iso(),
            "mock": {
                "running": self.mock_running,
                "intervalSeconds": self.mock_interval_seconds,
            },
            "storage": {
                "dbPath": str(self.db_path),
                "comparisons": self._row_count("comparisons"),
                "roomReadings": self._row_count("room_readings"),
                "floripaReadings": self._row_count("floripa_readings"),
            },
            "openMeteo": {
                "url": OPEN_METEO_URL,
                "lastProvider": self.last_fetch_provider,
                "lastTempC": self.last_floripa_temp,
                "lastFetchAt": self.last_floripa_timestamp,
            },
            "lastError": self.last_error,
        }

    def emit_mock_once(self) -> dict:
        temp = round(random.uniform(15.0, 35.0), 2)
        return self.ingest_temperature(temp_c=temp, timestamp=None, source="mock")

    def _mock_loop(self) -> None:
        while not self.mock_stop_event.wait(self.mock_interval_seconds):
            try:
                self.emit_mock_once()
            except Exception as exc:
                self.last_error = f"{now_iso()} | falha mock: {exc}"

    def start_mock(self, interval_seconds: int | None = None) -> dict:
        if interval_seconds is not None:
            self.mock_interval_seconds = max(2, min(interval_seconds, 3600))
        if self.mock_running:
            return {
                "running": True,
                "intervalSeconds": self.mock_interval_seconds,
            }
        self.mock_stop_event.clear()
        self.mock_thread = threading.Thread(target=self._mock_loop, daemon=True)
        self.mock_thread.start()
        self.mock_running = True
        return {
            "running": True,
            "intervalSeconds": self.mock_interval_seconds,
        }

    def stop_mock(self) -> dict:
        if not self.mock_running:
            return {"running": False}
        self.mock_stop_event.set()
        if self.mock_thread:
            self.mock_thread.join(timeout=2)
        self.mock_running = False
        return {"running": False}


backend = Phase2Backend(DB_PATH)


class Phase2Handler(BaseHTTPRequestHandler):
    def _send_json(self, status: int, payload: dict) -> None:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self) -> dict:
        content_length = int(self.headers.get("Content-Length", "0"))
        raw_body = self.rfile.read(content_length).decode("utf-8") if content_length > 0 else "{}"
        return json.loads(raw_body) if raw_body else {}

    def do_OPTIONS(self) -> None:
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET,POST,OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_GET(self) -> None:
        try:
            parsed = urllib.parse.urlparse(self.path)
            query = urllib.parse.parse_qs(parsed.query)
            path = parsed.path
            if path == "/api/health":
                self._send_json(200, backend.health())
                return
            if path == "/api/current":
                current = backend.get_current()
                self._send_json(200, {"ok": True, "current": current})
                return
            if path == "/api/history":
                from_ts = query.get("from", [None])[0]
                to_ts = query.get("to", [None])[0]
                limit = int(query.get("limit", ["500"])[0])
                rows = backend.get_history(from_ts=from_ts, to_ts=to_ts, limit=limit)
                self._send_json(200, {"ok": True, "count": len(rows), "items": rows})
                return
            if path == "/api/floripa/history":
                from_ts = query.get("from", [None])[0]
                to_ts = query.get("to", [None])[0]
                limit = int(query.get("limit", ["500"])[0])
                rows = backend.get_floripa_history(from_ts=from_ts, to_ts=to_ts, limit=limit)
                self._send_json(200, {"ok": True, "count": len(rows), "items": rows})
                return
            if path == "/api/stats/hourly":
                from_ts = query.get("from", [None])[0]
                to_ts = query.get("to", [None])[0]
                interval = int(query.get("intervalMinutes", ["60"])[0])
                hourly = backend.get_interval_stats(
                    from_ts=from_ts,
                    to_ts=to_ts,
                    interval_minutes=interval,
                )
                self._send_json(200, {"ok": True, "count": len(hourly), "items": hourly})
                return
            if path == "/api/stats/summary":
                from_ts = query.get("from", [None])[0]
                to_ts = query.get("to", [None])[0]
                tolerance = float(query.get("toleranceC", ["1.0"])[0])
                interval = int(query.get("intervalMinutes", ["30"])[0])
                summary = backend.get_summary_stats(
                    from_ts=from_ts,
                    to_ts=to_ts,
                    tolerance_c=tolerance,
                    interval_minutes=interval,
                )
                self._send_json(200, {"ok": True, "summary": summary})
                return
            self._send_json(404, {"ok": False, "error": "rota nao encontrada"})
        except Exception as exc:
            self._send_json(500, {"ok": False, "error": str(exc)})

    def do_POST(self) -> None:
        try:
            parsed = urllib.parse.urlparse(self.path)
            path = parsed.path
            payload = self._read_json()
            if path == "/api/temperature":
                temp_c = payload.get("tempC")
                if temp_c is None:
                    self._send_json(400, {"ok": False, "error": "tempC é obrigatório"})
                    return
                item = backend.ingest_temperature(
                    temp_c=float(temp_c),
                    timestamp=payload.get("timestamp"),
                    source=payload.get("source", "api"),
                )
                self._send_json(200, {"ok": True, "item": item})
                return
            if path == "/api/mock/emit":
                if "tempC" in payload:
                    item = backend.ingest_temperature(
                        temp_c=float(payload["tempC"]),
                        timestamp=payload.get("timestamp"),
                        source="mock",
                    )
                else:
                    item = backend.emit_mock_once()
                self._send_json(200, {"ok": True, "item": item})
                return
            if path == "/api/mock/start":
                interval = payload.get("intervalSeconds")
                state = backend.start_mock(interval_seconds=int(interval) if interval is not None else None)
                self._send_json(200, {"ok": True, "mock": state})
                return
            if path == "/api/mock/stop":
                state = backend.stop_mock()
                self._send_json(200, {"ok": True, "mock": state})
                return
            self._send_json(404, {"ok": False, "error": "rota nao encontrada"})
        except json.JSONDecodeError:
            self._send_json(400, {"ok": False, "error": "json inválido"})
        except Exception as exc:
            self._send_json(500, {"ok": False, "error": str(exc)})


def run() -> None:
    backend.start_mock(DEFAULT_MOCK_INTERVAL_SECONDS)
    server = ThreadingHTTPServer((HOST, PORT), Phase2Handler)
    print(f"Backend Fase 2 ativo em http://{HOST}:{PORT}")
    server.serve_forever()


if __name__ == "__main__":
    run()
