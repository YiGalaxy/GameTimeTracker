#!/usr/bin/env python3
"""
GameTimeTracker Backend API Server
Provides user auth, real-time game event sync, heartbeat, and data persistence.

Features:
  - User registration & login with JWT
  - Real-time game start/stop event push
  - Heartbeat receiver for currently active games
  - Batch upload/download for full sync
  - Record dedup by (user_id, game_name, start_time)
  - Daily stats aggregation

Usage:
    # Dev mode
    python3 server.py

    # Production
    gunicorn -w 4 -b 0.0.0.0:8080 server:app
"""

import os
import sqlite3
import hashlib
import secrets
import logging
from datetime import datetime, timedelta, timezone

import jwt
from flask import Flask, request, jsonify, g
from flask_cors import CORS

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
DATABASE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "server.db")
JWT_SECRET = os.environ.get("JWT_SECRET", secrets.token_hex(32))
JWT_EXPIRE_HOURS = int(os.environ.get("JWT_EXPIRE_HOURS", "720"))  # 30 days
HOST = os.environ.get("HOST", "0.0.0.0")
PORT = int(os.environ.get("PORT", "8080"))
LOG_LEVEL = os.environ.get("LOG_LEVEL", "INFO")

logging.basicConfig(
    level=getattr(logging, LOG_LEVEL.upper()),
    format="%(asctime)s [%(levelname)s] %(message)s",
)
logger = logging.getLogger("gametracker")

app = Flask(__name__)
CORS(app)

# ---------------------------------------------------------------------------
# Database
# ---------------------------------------------------------------------------
def get_db():
    if "db" not in g:
        g.db = sqlite3.connect(DATABASE)
        g.db.row_factory = sqlite3.Row
        g.db.execute("PRAGMA journal_mode=WAL")
        g.db.execute("PRAGMA synchronous=NORMAL")
    return g.db

@app.teardown_appcontext
def close_db(exception):
    db = g.pop("db", None)
    if db:
        db.close()

def init_db():
    db = sqlite3.connect(DATABASE)
    db.execute("PRAGMA journal_mode=WAL")
    db.executescript("""
        CREATE TABLE IF NOT EXISTS users (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            username    TEXT    NOT NULL UNIQUE,
            password    TEXT    NOT NULL,
            nickname    TEXT    DEFAULT '',
            created_at  DATETIME DEFAULT CURRENT_TIMESTAMP
        );

        /* Game play records with dedup key: (user_id, game_name, start_time) */
        CREATE TABLE IF NOT EXISTS game_records (
            id               INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id          INTEGER NOT NULL,
            game_name        TEXT    NOT NULL,
            process_name     TEXT    NOT NULL DEFAULT '',
            start_time       DATETIME NOT NULL,
            end_time         DATETIME,
            duration_seconds INTEGER DEFAULT 0,
            date             DATE    NOT NULL,
            created_at       DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at       DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (user_id) REFERENCES users(id)
        );

        /* Unique index for dedup: same user, same game, same start_time = one record */
        CREATE UNIQUE INDEX IF NOT EXISTS idx_records_dedup
            ON game_records(user_id, game_name, start_time);

        /* Active sessions (heartbeat data) */
        CREATE TABLE IF NOT EXISTS active_sessions (
            id           INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id      INTEGER NOT NULL UNIQUE,
            game_name    TEXT,
            process_name TEXT,
            start_time   DATETIME,
            updated_at   DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (user_id) REFERENCES users(id)
        );

        /* Index for daily queries */
        CREATE INDEX IF NOT EXISTS idx_records_user_date
            ON game_records(user_id, date);
    """)
    db.commit()
    db.close()
    logger.info("Database initialized: %s", DATABASE)

# ---------------------------------------------------------------------------
# Auth helpers
# ---------------------------------------------------------------------------
def hash_password(password):
    salt = "gametracker_salt_v1"
    return hashlib.sha256((password + salt).encode()).hexdigest()

def generate_token(user_id, username, nickname):
    payload = {
        "user_id": user_id,
        "username": username,
        "nickname": nickname,
        "exp": datetime.now(timezone.utc) + timedelta(hours=JWT_EXPIRE_HOURS),
        "iat": datetime.now(timezone.utc),
    }
    return jwt.encode(payload, JWT_SECRET, algorithm="HS256")

def verify_token(token):
    try:
        return jwt.decode(token, JWT_SECRET, algorithms=["HS256"])
    except jwt.ExpiredSignatureError:
        logger.warning("Token expired")
        return None
    except jwt.InvalidTokenError as e:
        logger.warning("Invalid token: %s", e)
        return None

def require_auth():
    auth = request.headers.get("Authorization", "")
    if not auth.startswith("Bearer "):
        return None
    return verify_token(auth[7:])

# ---------------------------------------------------------------------------
# Routes: Auth
# ---------------------------------------------------------------------------
@app.route("/api/auth/register", methods=["POST"])
def register():
    data = request.get_json(silent=True) or {}
    username = (data.get("username") or "").strip()
    password = data.get("password") or ""
    nickname = (data.get("nickname") or "").strip()

    if len(username) < 3 or len(username) > 20:
        return jsonify({"message": "Username must be 3-20 chars"}), 400
    if len(password) < 6:
        return jsonify({"message": "Password must be >= 6 chars"}), 400

    db = get_db()
    if db.execute("SELECT id FROM users WHERE username = ?", (username,)).fetchone():
        return jsonify({"message": "Username already exists"}), 409

    db.execute(
        "INSERT INTO users (username, password, nickname) VALUES (?, ?, ?)",
        (username, hash_password(password), nickname),
    )
    db.commit()

    user = db.execute("SELECT id FROM users WHERE username = ?", (username,)).fetchone()
    token = generate_token(user["id"], username, nickname)
    logger.info("New user registered: %s", username)
    return jsonify({"token": token, "username": username, "nickname": nickname}), 201


@app.route("/api/auth/login", methods=["POST"])
def login():
    data = request.get_json(silent=True) or {}
    username = (data.get("username") or "").strip()
    password = data.get("password") or ""

    if not username or not password:
        return jsonify({"message": "Username and password required"}), 400

    db = get_db()
    user = db.execute(
        "SELECT id, username, nickname, password FROM users WHERE username = ?",
        (username,),
    ).fetchone()

    if not user or user["password"] != hash_password(password):
        logger.warning("Failed login attempt for: %s", username)
        return jsonify({"message": "Invalid username or password"}), 401

    token = generate_token(user["id"], user["username"], user["nickname"] or "")
    logger.info("User logged in: %s", username)
    return jsonify({
        "token": token,
        "username": user["username"],
        "nickname": user["nickname"] or "",
    })

# ---------------------------------------------------------------------------
# Routes: Real-time events
# ---------------------------------------------------------------------------
@app.route("/api/sync/event", methods=["POST"])
def sync_event():
    """
    Receive a real-time game start/stop event from the client.
    Client pushes this immediately when a game starts or stops.
    On 'stop', the record is upserted to game_records.
    On 'start', the record is saved with end_time=null.
    """
    payload = require_auth()
    if not payload:
        return jsonify({"message": "Auth required"}), 401

    data = request.get_json(silent=True) or {}
    game_name = data.get("game_name", "")
    process_name = data.get("process_name", "")
    event_type = data.get("event_type", "")  # "start" or "stop"
    duration = data.get("duration_seconds", 0)
    date_str = data.get("date", datetime.now().strftime("%Y-%m-%d"))

    if not game_name:
        return jsonify({"message": "game_name required"}), 400

    user_id = payload["user_id"]
    now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S")
    db = get_db()

    if event_type == "stop":
        start_time = data.get("start_time", "")
        end_time = data.get("end_time", now)

        # Upsert: use INSERT OR REPLACE on dedup index
        # But we need to keep the id stable, so use INSERT ON CONFLICT UPDATE
        db.execute(
            """INSERT INTO game_records
               (user_id, game_name, process_name, start_time, end_time, duration_seconds, date)
               VALUES (?, ?, ?, ?, ?, ?, ?)
               ON CONFLICT(user_id, game_name, start_time)
               DO UPDATE SET end_time=excluded.end_time,
                             duration_seconds=excluded.duration_seconds,
                             updated_at=CURRENT_TIMESTAMP""",
            (user_id, game_name, process_name, start_time, end_time, duration, date_str),
        )
        db.commit()

        # Clear active session for this user
        db.execute("DELETE FROM active_sessions WHERE user_id = ?", (user_id,))
        db.commit()

        logger.info("[event] STOP  %s | user=%s | dur=%ds", game_name, payload["username"], duration)

    elif event_type == "start":
        start_time = data.get("event_time", now)

        # Insert start record (end_time = null)
        db.execute(
            """INSERT OR IGNORE INTO game_records
               (user_id, game_name, process_name, start_time, end_time, duration_seconds, date)
               VALUES (?, ?, ?, ?, NULL, 0, ?)""",
            (user_id, game_name, process_name, start_time, date_str),
        )
        db.commit()

        # Upsert active session
        db.execute(
            """INSERT INTO active_sessions (user_id, game_name, process_name, start_time, updated_at)
               VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP)
               ON CONFLICT(user_id)
               DO UPDATE SET game_name=excluded.game_name,
                             process_name=excluded.process_name,
                             start_time=excluded.start_time,
                             updated_at=CURRENT_TIMESTAMP""",
            (user_id, game_name, process_name, start_time),
        )
        db.commit()

        logger.info("[event] START %s | user=%s", game_name, payload["username"])

    return jsonify({"status": "ok"}), 200


@app.route("/api/sync/heartbeat", methods=["POST"])
def heartbeat():
    """
    Client sends its current active games every few seconds.
    Server updates active_sessions so it knows what the user is playing right now.
    """
    payload = require_auth()
    if not payload:
        return jsonify({"message": "Auth required"}), 401

    data = request.get_json(silent=True) or {}
    user_id = payload["user_id"]
    db = get_db()
    active = data.get("active_games", [])

    if active and len(active) > 0:
        game = active[0]  # Use the first active game
        db.execute(
            """INSERT INTO active_sessions (user_id, game_name, process_name, start_time, updated_at)
               VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP)
               ON CONFLICT(user_id)
               DO UPDATE SET game_name=excluded.game_name,
                             process_name=excluded.process_name,
                             start_time=excluded.start_time,
                             updated_at=CURRENT_TIMESTAMP""",
            (user_id, game.get("game_name", ""),
             game.get("process_name", ""),
             game.get("start_time", "")),
        )
    else:
        db.execute("DELETE FROM active_sessions WHERE user_id = ?", (user_id,))

    db.commit()
    return jsonify({"status": "ok"}), 200


@app.route("/api/sync/status", methods=["GET"])
def sync_status():
    """
    Returns the user's current online status and active sessions.
    Useful for multi-device scenarios or server-side monitoring.
    """
    payload = require_auth()
    if not payload:
        return jsonify({"message": "Auth required"}), 401

    db = get_db()
    session = db.execute(
        "SELECT game_name, process_name, start_time, updated_at "
        "FROM active_sessions WHERE user_id = ?", (payload["user_id"],)
    ).fetchone()

    result = {
        "online": session is not None,
        "active_game": None,
    }
    if session:
        result["active_game"] = {
            "game_name": session["game_name"],
            "process_name": session["process_name"],
            "start_time": session["start_time"],
            "last_heartbeat": session["updated_at"],
        }

    return jsonify(result), 200

# ---------------------------------------------------------------------------
# Routes: Batch sync
# ---------------------------------------------------------------------------
@app.route("/api/sync/upload", methods=["POST"])
def sync_upload():
    payload = require_auth()
    if not payload:
        return jsonify({"message": "Auth required"}), 401

    records = (request.get_json(silent=True) or {}).get("records", [])
    if not records:
        return jsonify({"message": "No records"}), 200

    user_id = payload["user_id"]
    db = get_db()
    count = 0

    for r in records:
        try:
            db.execute(
                """INSERT INTO game_records
                   (user_id, game_name, process_name, start_time, end_time, duration_seconds, date)
                   VALUES (?, ?, ?, ?, ?, ?, ?)
                   ON CONFLICT(user_id, game_name, start_time)
                   DO UPDATE SET end_time=excluded.end_time,
                                 duration_seconds=excluded.duration_seconds,
                                 updated_at=CURRENT_TIMESTAMP""",
                (
                    user_id,
                    r.get("game_name", ""),
                    r.get("process_name", ""),
                    r.get("start_time", ""),
                    r.get("end_time", ""),
                    r.get("duration_seconds", 0),
                    r.get("date", ""),
                ),
            )
            count += 1
        except Exception as e:
            logger.error("Upload insert error: %s", e)

    db.commit()
    logger.info("[batch] uploaded %d records for user=%s", count, payload["username"])
    return jsonify({"message": "OK", "count": count}), 200


@app.route("/api/sync/download", methods=["GET"])
def sync_download():
    payload = require_auth()
    if not payload:
        return jsonify({"message": "Auth required"}), 401

    db = get_db()
    rows = db.execute(
        """SELECT game_name, process_name, start_time, end_time, duration_seconds, date
           FROM game_records WHERE user_id = ?
           ORDER BY date DESC, start_time DESC""",
        (payload["user_id"],),
    ).fetchall()

    records = [{
        "game_name": r["game_name"],
        "process_name": r["process_name"],
        "start_time": r["start_time"],
        "end_time": r["end_time"],
        "duration_seconds": r["duration_seconds"],
        "date": r["date"],
    } for r in rows]

    return jsonify({"records": records}), 200


@app.route("/api/stats/daily", methods=["GET"])
def stats_daily():
    """Return daily aggregated stats for the logged-in user."""
    payload = require_auth()
    if not payload:
        return jsonify({"message": "Auth required"}), 401

    date = request.args.get("date", datetime.now().strftime("%Y-%m-%d"))
    db = get_db()

    rows = db.execute(
        """SELECT game_name, SUM(duration_seconds) as total, COUNT(*) as sessions
           FROM game_records
           WHERE user_id = ? AND date = ?
           GROUP BY game_name
           ORDER BY total DESC""",
        (payload["user_id"], date),
    ).fetchall()

    stats = [{
        "game_name": r["game_name"],
        "total_seconds": r["total"],
        "session_count": r["sessions"],
    } for r in rows]

    return jsonify({"date": date, "stats": stats}), 200


# ---------------------------------------------------------------------------
# Health check
# ---------------------------------------------------------------------------
@app.route("/api/health", methods=["GET"])
def health():
    return jsonify({
        "status": "ok",
        "version": "1.0.0",
        "timestamp": datetime.now().isoformat(),
    }), 200


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    init_db()
    logger.info("=" * 50)
    logger.info("GameTimeTracker Server v1.0.0")
    logger.info("JWT_SECRET: %s", JWT_SECRET)
    logger.info("Listening on http://%s:%s", HOST, PORT)
    logger.info("=" * 50)
    app.run(host=HOST, port=PORT, debug=False)
