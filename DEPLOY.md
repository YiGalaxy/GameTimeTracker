# GameTimeTracker Deployment Guide

## Project Structure

```
E:\Documents\ABC\
├── CMakeLists.txt              # Qt CMake build (Windows client only)
├── DEPLOY.md                   # This file
├── resources/resources.qrc
├── src/                        # Windows client C++ source (WinAPI only)
│   ├── main.cpp
│   ├── mainwindow.h/.cpp       # Main window + tray
│   ├── gamemonitor.h/.cpp      # Process monitoring (WinAPI)
│   ├── datamanager.h/.cpp      # SQLite local storage
│   ├── authmanager.h/.cpp      # Auth HTTP client
│   ├── syncmanager.h/.cpp      # Sync HTTP client
│   ├── logindialog.h/.cpp      # Login UI
│   ├── statswidget.h/.cpp      # Statistics UI
│   └── gamelistwidget.h/.cpp   # Game list management UI
└── build/                      # Build output (CMake)
```

## Architecture

```
Windows Client (WinAPI)                 Linux Server
┌─────────────────────────┐           ┌──────────────────────────┐
│  GameMonitor            │ event     │  POST /sync/event       │
│  (Win32 process poll)   │─────────► │  POST /sync/heartbeat   │
│                         │◄───────── │  GET  /sync/download    │
│  SyncManager (5min)     │ batch     │  POST /sync/upload      │
│                         │─────────► │                          │
│  Local SQLite (offline) │           │  Auth: JWT login/register│
│                         │           │  SQLite with dedup      │
│  StatsWidget (standalone)           │  Active session tracking │
└─────────────────────────┘           └──────────────────────────┘
```

> **Windows client only** (depends on Win32 `CreateToolhelp32Snapshot`).
> Linux server runs the Python backend API.

---

## ========================================================
## PART 1: LINUX SERVER DEPLOYMENT
## ========================================================

### 1.1 Requirements

- Ubuntu 20.04+ / Debian 11+ / CentOS 8+
- Python 3.10+
- pip

### 1.2 Quick Deploy

```bash
ssh root@YOUR_SERVER_IP
```

```bash
# Install Python
apt update && apt install -y python3 python3-pip python3-venv

# Create project
mkdir -p /opt/gametracker
cd /opt/gametracker

# Virtual env
python3 -m venv venv
source venv/bin/activate

# Install deps
pip install flask flask-cors pyjwt gunicorn
```

### 1.3 Create server.py

Run this to write the server file:

```bash
cat > /opt/gametracker/server.py << 'SERVEREOF'
#!/usr/bin/env python3
"""
GameTimeTracker Backend API Server
User auth, real-time game events, heartbeat, data persistence with dedup.
"""

import os, sqlite3, hashlib, secrets, logging
from datetime import datetime, timedelta, timezone
import jwt
from flask import Flask, request, jsonify, g
from flask_cors import CORS

DATABASE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "server.db")
JWT_SECRET = os.environ.get("JWT_SECRET", secrets.token_hex(32))
JWT_EXPIRE_HOURS = int(os.environ.get("JWT_EXPIRE_HOURS", "720"))
HOST = os.environ.get("HOST", "0.0.0.0")
PORT = int(os.environ.get("PORT", "8080"))
LOG_LEVEL = os.environ.get("LOG_LEVEL", "INFO")

logging.basicConfig(level=getattr(logging, LOG_LEVEL.upper()),
                    format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("gametracker")

app = Flask(__name__)
CORS(app)

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
    if db: db.close()

def init_db():
    db = sqlite3.connect(DATABASE)
    db.execute("PRAGMA journal_mode=WAL")
    db.executescript("""
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT NOT NULL UNIQUE,
            password TEXT NOT NULL,
            nickname TEXT DEFAULT '',
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );
        CREATE TABLE IF NOT EXISTS game_records (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER NOT NULL,
            game_name TEXT NOT NULL,
            process_name TEXT NOT NULL DEFAULT '',
            start_time DATETIME NOT NULL,
            end_time DATETIME,
            duration_seconds INTEGER DEFAULT 0,
            date DATE NOT NULL,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (user_id) REFERENCES users(id)
        );
        CREATE UNIQUE INDEX IF NOT EXISTS idx_records_dedup
            ON game_records(user_id, game_name, start_time);
        CREATE TABLE IF NOT EXISTS active_sessions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER NOT NULL UNIQUE,
            game_name TEXT, process_name TEXT, start_time DATETIME,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (user_id) REFERENCES users(id)
        );
        CREATE INDEX IF NOT EXISTS idx_records_user_date
            ON game_records(user_id, date);
    """)
    db.commit(); db.close()
    logger.info("Database: %s", DATABASE)

def hash_password(password):
    return hashlib.sha256((password + "gametracker_salt_v1").encode()).hexdigest()

def generate_token(user_id, username, nickname):
    payload = {"user_id": user_id, "username": username, "nickname": nickname,
               "exp": datetime.now(timezone.utc) + timedelta(hours=JWT_EXPIRE_HOURS),
               "iat": datetime.now(timezone.utc)}
    return jwt.encode(payload, JWT_SECRET, algorithm="HS256")

def verify_token(token):
    try: return jwt.decode(token, JWT_SECRET, algorithms=["HS256"])
    except (jwt.ExpiredSignatureError, jwt.InvalidTokenError): return None

def require_auth():
    auth = request.headers.get("Authorization", "")
    if not auth.startswith("Bearer "): return None
    return verify_token(auth[7:])

@app.route("/api/auth/register", methods=["POST"])
def register():
    data = request.get_json(silent=True) or {}
    username = (data.get("username") or "").strip()
    password = data.get("password") or ""
    nickname = (data.get("nickname") or "").strip()
    if len(username) < 3 or len(username) > 20:
        return jsonify({"message": "Username 3-20 chars"}), 400
    if len(password) < 6:
        return jsonify({"message": "Password >= 6 chars"}), 400
    db = get_db()
    if db.execute("SELECT id FROM users WHERE username=?", (username,)).fetchone():
        return jsonify({"message": "Username exists"}), 409
    db.execute("INSERT INTO users (username,password,nickname) VALUES (?,?,?)",
               (username, hash_password(password), nickname))
    db.commit()
    u = db.execute("SELECT id FROM users WHERE username=?", (username,)).fetchone()
    token = generate_token(u["id"], username, nickname)
    logger.info("Registered: %s", username)
    return jsonify({"token": token, "username": username, "nickname": nickname}), 201

@app.route("/api/auth/login", methods=["POST"])
def login():
    data = request.get_json(silent=True) or {}
    username = (data.get("username") or "").strip()
    password = data.get("password") or ""
    if not username or not password:
        return jsonify({"message": "Username and password required"}), 400
    db = get_db()
    u = db.execute("SELECT id,username,nickname,password FROM users WHERE username=?",
                   (username,)).fetchone()
    if not u or u["password"] != hash_password(password):
        return jsonify({"message": "Invalid credentials"}), 401
    token = generate_token(u["id"], u["username"], u["nickname"] or "")
    return jsonify({"token": token, "username": u["username"],
                    "nickname": u["nickname"] or ""})

@app.route("/api/sync/event", methods=["POST"])
def sync_event():
    p = require_auth()
    if not p: return jsonify({"message": "Auth required"}), 401
    d = request.get_json(silent=True) or {}
    game_name, event_type = d.get("game_name",""), d.get("event_type","")
    if not game_name: return jsonify({"message": "game_name required"}), 400
    uid, now = p["user_id"], datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S")
    db = get_db()
    if event_type == "stop":
        st = d.get("start_time","")
        et = d.get("end_time", now)
        dur = d.get("duration_seconds", 0)
        dt = d.get("date", datetime.now().strftime("%Y-%m-%d"))
        db.execute("""INSERT INTO game_records (user_id,game_name,process_name,start_time,end_time,duration_seconds,date)
            VALUES (?,?,?,?,?,?,?)
            ON CONFLICT(user_id,game_name,start_time)
            DO UPDATE SET end_time=excluded.end_time, duration_seconds=excluded.duration_seconds,
                          updated_at=CURRENT_TIMESTAMP""",
            (uid, game_name, d.get("process_name",""), st, et, dur, dt))
        db.commit()
        db.execute("DELETE FROM active_sessions WHERE user_id=?", (uid,))
        db.commit()
        logger.info("[event] STOP %s | user=%s | dur=%ds", game_name, p["username"], dur)
    elif event_type == "start":
        st = d.get("event_time", now)
        dt = d.get("date", datetime.now().strftime("%Y-%m-%d"))
        db.execute("""INSERT OR IGNORE INTO game_records (user_id,game_name,process_name,start_time,end_time,duration_seconds,date)
            VALUES (?,?,?,?,NULL,0,?)""", (uid, game_name, d.get("process_name",""), st, dt))
        db.commit()
        db.execute("""INSERT INTO active_sessions (user_id,game_name,process_name,start_time,updated_at)
            VALUES (?,?,?,?,CURRENT_TIMESTAMP)
            ON CONFLICT(user_id) DO UPDATE SET game_name=excluded.game_name,
            process_name=excluded.process_name, start_time=excluded.start_time,
            updated_at=CURRENT_TIMESTAMP""",
            (uid, game_name, d.get("process_name",""), st))
        db.commit()
        logger.info("[event] START %s | user=%s", game_name, p["username"])
    return jsonify({"status": "ok"}), 200

@app.route("/api/sync/heartbeat", methods=["POST"])
def heartbeat():
    p = require_auth()
    if not p: return jsonify({"message": "Auth required"}), 401
    d = request.get_json(silent=True) or {}
    db = get_db()
    active = d.get("active_games", [])
    if active and len(active) > 0:
        g = active[0]
        db.execute("""INSERT INTO active_sessions (user_id,game_name,process_name,start_time,updated_at)
            VALUES (?,?,?,?,CURRENT_TIMESTAMP)
            ON CONFLICT(user_id) DO UPDATE SET game_name=excluded.game_name,
            process_name=excluded.process_name, start_time=excluded.start_time,
            updated_at=CURRENT_TIMESTAMP""",
            (p["user_id"], g.get("game_name",""), g.get("process_name",""),
             g.get("start_time","")))
    else:
        db.execute("DELETE FROM active_sessions WHERE user_id=?", (p["user_id"],))
    db.commit()
    return jsonify({"status": "ok"}), 200

@app.route("/api/sync/upload", methods=["POST"])
def sync_upload():
    p = require_auth()
    if not p: return jsonify({"message": "Auth required"}), 401
    records = (request.get_json(silent=True) or {}).get("records", [])
    if not records: return jsonify({"message": "No records"}), 200
    db = get_db()
    cnt = 0
    for r in records:
        try:
            db.execute("""INSERT INTO game_records (user_id,game_name,process_name,start_time,end_time,duration_seconds,date)
                VALUES (?,?,?,?,?,?,?)
                ON CONFLICT(user_id,game_name,start_time)
                DO UPDATE SET end_time=excluded.end_time, duration_seconds=excluded.duration_seconds,
                              updated_at=CURRENT_TIMESTAMP""",
                (p["user_id"], r.get("game_name",""), r.get("process_name",""),
                 r.get("start_time",""), r.get("end_time",""), r.get("duration_seconds",0),
                 r.get("date","")))
            cnt += 1
        except Exception as e: logger.error("Upload err: %s", e)
    db.commit()
    return jsonify({"message":"OK","count":cnt}), 200

@app.route("/api/sync/download", methods=["GET"])
def sync_download():
    p = require_auth()
    if not p: return jsonify({"message": "Auth required"}), 401
    rows = get_db().execute(
        "SELECT game_name,process_name,start_time,end_time,duration_seconds,date "
        "FROM game_records WHERE user_id=? ORDER BY date DESC, start_time DESC",
        (p["user_id"],)).fetchall()
    return jsonify({"records":[dict(r) for r in rows]}), 200

@app.route("/api/health", methods=["GET"])
def health():
    return jsonify({"status":"ok","version":"1.0.0",
                    "timestamp":datetime.now().isoformat()}), 200

if __name__ == "__main__":
    init_db()
    logger.info("="*50)
    logger.info("GameTimeTracker Server v1.0.0")
    logger.info("Listening on http://%s:%s", HOST, PORT)
    logger.info("="*50)
    app.run(host=HOST, port=PORT, debug=False)
SERVEREOF

echo "server.py created"
```

### 1.4 Start (Development)

```bash
cd /opt/gametracker
source venv/bin/activate
python3 server.py
```

Verify:

```bash
curl http://localhost:8080/api/health
```

Expected:
```json
{"status":"ok","version":"1.0.0","timestamp":"..."}
```

Test register:

```bash
curl -X POST http://localhost:8080/api/auth/register \
  -H "Content-Type: application/json" \
  -d '{"username":"test","password":"test123","nickname":"Tester"}'
```

### 1.5 Production (systemd + gunicorn)

Create `/etc/systemd/system/gametracker.service`:

```ini
[Unit]
Description=GameTimeTracker API Server
After=network.target

[Service]
Type=simple
User=root
WorkingDirectory=/opt/gametracker
Environment="PATH=/opt/gametracker/venv/bin"
Environment="JWT_SECRET=REPLACE_WITH_A_RANDOM_SECRET_STRING"
Environment="PORT=8080"
Environment="LOG_LEVEL=INFO"
ExecStart=/opt/gametracker/venv/bin/gunicorn -w 4 -b 0.0.0.0:8080 server:app
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

```bash
systemctl daemon-reload
systemctl enable gametracker
systemctl start gametracker
systemctl status gametracker

# View logs
journalctl -u gametracker -f
```

### 1.6 Firewall

```bash
ufw allow 8080/tcp
# or
iptables -A INPUT -p tcp --dport 8080 -j ACCEPT
```

### 1.7 Final Verification

```bash
curl http://YOUR_SERVER_IP:8080/api/health
curl -X POST http://YOUR_SERVER_IP:8080/api/auth/register \
  -H "Content-Type: application/json" \
  -d '{"username":"test","password":"test123","nickname":"Tester"}'
```

---

## ========================================================
## PART 2: WINDOWS CLIENT BUILD
## ========================================================

### 2.1 Build

```powershell
cd E:\Documents\ABC\build
cmake -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="E:/Qt/app/6.11.0/mingw_64" ..
cmake --build .
```

### 2.2 Deploy Qt Runtime

```powershell
E:\Qt\app\6.11.0\mingw_64\bin\windeployqt.exe GameTimeTracker.exe
```

### 2.3 Connect to Server

1. Run `GameTimeTracker.exe`
2. Menu: **Account → Settings → API Server URL**
3. Enter: `http://YOUR_SERVER_IP:8080/api`
4. **Account → Login / Register** to create account

### 2.4 Sync Flow

| Method | Trigger | Freq | Endpoint |
|--------|---------|------|----------|
| Real-time event | Game start/stop | Immediate | `POST /sync/event` |
| Heartbeat | UI refresh | 3s | `POST /sync/heartbeat` |
| Batch upload | Timer | 5min | `POST /sync/upload` |
| Batch download | After upload | 5min | `GET /sync/download` |
| Manual sync | Button click | On demand | Full sync |

---

## ========================================================
## PART 3: API REFERENCE
## ========================================================

| Endpoint | Method | Auth | Description |
|----------|--------|------|-------------|
| `/api/health` | GET | No | Server health check |
| `/api/auth/register` | POST | No | Register `{username, password, nickname}` |
| `/api/auth/login` | POST | No | Login `{username, password}` → `{token, ...}` |
| `/api/sync/event` | POST | Bearer | Real-time game start/stop event |
| `/api/sync/heartbeat` | POST | Bearer | Report currently active games |
| `/api/sync/upload` | POST | Bearer | Batch upload records |
| `/api/sync/download` | GET | Bearer | Download all records |

---

## ========================================================
## PART 4: MAINTENANCE
## ========================================================

### Backup

```bash
cp /opt/gametracker/server.db /backup/server_$(date +%Y%m%d).db
```

### Logs

```bash
journalctl -u gametracker -f | grep -E "(START|STOP|uploaded)"
```

### Restart

```bash
systemctl restart gametracker
```

### Dedup Logic

Server deduplicates by `(user_id, game_name, start_time)` unique index. Same record re-uploaded = updated, not duplicated.

### DB Files

- Server: `/opt/gametracker/server.db`
- Client: `%LOCALAPPDATA%/GameTimeTracker/data.db`
