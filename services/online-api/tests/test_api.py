import json
import xml.etree.ElementTree as ET
from pathlib import Path

import fakeredis
from fastapi.testclient import TestClient
from sqlalchemy import create_engine, inspect, text
from alembic import command
from alembic.config import Config

from app.config import Settings
from app.main import create_app
from app.models import User
from app.security import hash_password
from app.directory import MemoryDirectory


def api_client(app):
    return TestClient(app, client=("127.0.0.1", 12345))


def parse(response):
    assert response.status_code == 200
    return ET.fromstring(response.content)


def login(client, username, password):
    root = parse(client.post("/api/v2/user/connect/", data={
        "username": username,
        "password": password,
        "save-session": "true",
    }))
    assert root.attrib["success"] == "yes"
    return {"userid": root.attrib["userid"], "token": root.attrib["token"]}


def test_invite_auth_host_listing_and_rendezvous(tmp_path):
    settings = Settings(database_url=f"sqlite:///{tmp_path / 'online.db'}")
    app = create_app(settings)
    with api_client(app) as client:
        with app.state.session_factory() as db:
            db.add_all([
                User(username="host", email="host@minkowskikart.internal",
                     password_hash=hash_password("host-pass-123")),
                User(username="driver", email="driver@minkowskikart.internal",
                     password_hash=hash_password("drive-pass-123")),
            ])
            db.commit()

        host = login(client, "host", "host-pass-123")
        driver = login(client, "driver", "drive-pass-123")
        social_stub = parse(client.post("/api/v2/user/get-achievements/", data={
            **driver, "visitingid": host["userid"],
        }))
        assert social_stub.attrib["visitingid"] == host["userid"]
        created = parse(client.post("/api/v2/server/create/", data={
            **host,
            "name": "Test Room",
            "address": "1234",
            "port": "2759",
            "private_port": "2759",
            "max_players": "8",
            "difficulty": "1",
            "game_mode": "3",
            "password": "0",
            "version": "7",
            "aes_gcm_128bit_tag": "1",
        }))
        server_id = created.find("./server/server-info").attrib["id"]

        listing = parse(client.post("/api/v2/server/get-all/"))
        server_info = listing.find("./servers/server/server-info")
        assert server_info.attrib["name"] == "Test Room"
        assert server_info.attrib["ip"] == "1234"
        assert server_info.attrib["port"] == "2759"
        assert listing.find("./servers/server/players") is not None

        joined = parse(client.post("/api/v2/server/join-server-key/", data={
            **driver, "server-id": server_id, "address": "5678", "port": "2758",
            "aes-key": "secret-key", "aes-iv": "secret-iv",
        }))
        assert joined.attrib["success"] == "yes"
        polled = parse(client.post("/api/v2/server/poll-connection-requests/", data={
            **host, "address": "1234", "port": "2759",
            "current-players": "1", "current-ai": "0", "game-started": "0",
        }))
        rendezvous = polled.find("./users/user")
        assert rendezvous.attrib["username"] == "driver"
        assert rendezvous.attrib["aes-key"] == "secret-key"


def test_registration_validates_input(tmp_path):
    app = create_app(Settings(database_url=f"sqlite:///{tmp_path / 'online.db'}"))
    with api_client(app) as client:
        root = parse(client.post("/api/v2/user/register/", data={}))
        assert root.attrib["success"] == "no"


def test_registration_is_invite_only(tmp_path):
    # Registration stays compatible with the client protocol but cannot create
    # accounts outside the operator-managed invite flow.
    app = create_app(Settings(database_url=f"sqlite:///{tmp_path / 'online.db'}"))
    with api_client(app) as client:
        for name in ("alice", "bob"):
            root = parse(client.post("/api/v2/user/register/", data={
                "username": name, "password": "password-123",
                "password_confirm": "password-123", "terms": "on",
            }))
            assert root.attrib["success"] == "no"
            assert "invite-only" in root.attrib["info"]
        # Neither unique nor distinct usernames can self-register.
        rejected = parse(client.post("/api/v2/user/register/", data={
            "username": "charlie", "password": "password-123",
            "password_confirm": "password-123", "terms": "on",
        }))
        assert rejected.attrib["success"] == "no"
        with app.state.session_factory() as db:
            assert db.query(User).count() == 0


def test_registration_rejects_html_email(tmp_path):
    app = create_app(Settings(database_url=f"sqlite:///{tmp_path / 'online.db'}"))
    with api_client(app) as client:
        root = parse(client.post("/api/v2/user/register/", data={
            "username": "alice", "password": "password-123",
            "password_confirm": "password-123", "terms": "on",
            "email": '<img src=x onerror="alert(1)">@example.com',
        }))
        assert root.attrib["success"] == "no"
        assert root.attrib["info"] == "Email address is invalid."


def test_admin_dashboard_escapes_stored_fields_and_admin_session_is_scoped(tmp_path):
    app = create_app(Settings(database_url=f"sqlite:///{tmp_path / 'online.db'}"))
    with api_client(app) as client:
        with app.state.session_factory() as db:
            db.add(User(username="<script>alert(1)</script>",
                        email="<img src=x>@example.test",
                        password_hash=hash_password("password-123"),
                        is_admin=True))
            db.commit()
        game = login(client, "<script>alert(1)</script>", "password-123")
        denied = client.get("/admin/dashboard", cookies={"admin_token": game["token"]},
                            follow_redirects=False)
        assert denied.status_code == 303

        with app.state.session_factory() as db:
            admin = db.query(User).first()
            from app.models import UserSession
            from app.security import new_session_token, token_hash, session_expiry
            admin_token = new_session_token()
            db.add(UserSession(token_hash=token_hash(admin_token), user_id=admin.id,
                               session_type="admin", expires_at=session_expiry(1)))
            db.commit()
        dashboard = client.get("/admin/dashboard", cookies={"admin_token": admin_token})
        assert dashboard.status_code == 200
        assert "&lt;script&gt;alert(1)&lt;/script&gt;" in dashboard.text
        assert "&lt;img src=x&gt;@example.test" in dashboard.text
        with app.state.session_factory() as db:
            db.query(User).filter_by(is_admin=True).one().active = False
            db.commit()
        inactive = client.get("/admin/dashboard",
                              cookies={"admin_token": admin_token},
                              follow_redirects=False)
        assert inactive.status_code == 303
        with app.state.session_factory() as db:
            db.query(User).filter_by(is_admin=True).one().active = True
            db.commit()

        login_response = client.post("/admin/login", data={
            "username": "<script>alert(1)</script>", "password": "password-123",
        }, follow_redirects=False)
        assert "secure" in login_response.headers["set-cookie"].lower()
        error = client.get("/admin/login?error=%3Cscript%3Ealert(1)%3C%2Fscript%3E")
        assert "&lt;script&gt;alert(1)&lt;/script&gt;" in error.text
        assert "<script>alert(1)</script>" not in error.text


def test_password_change_revokes_all_sessions(tmp_path):
    app = create_app(Settings(database_url=f"sqlite:///{tmp_path / 'online.db'}"))
    with api_client(app) as client:
        with app.state.session_factory() as db:
            db.add(User(username="alice", email="alice@example.test",
                        password_hash=hash_password("original-pass-123")))
            db.commit()
        first = login(client, "alice", "original-pass-123")
        second = login(client, "alice", "original-pass-123")
        changed = parse(client.post("/api/v2/user/change-password/", data={
            **first, "current": "original-pass-123", "new1": "changed-pass-123",
            "new2": "changed-pass-123",
        }))
        assert changed.attrib["success"] == "yes"
        for session in (first, second):
            rejected = parse(client.post("/api/v2/user/poll/", data=session))
            assert rejected.attrib["success"] == "no"
        assert login(client, "alice", "changed-pass-123")["userid"]


def test_legacy_0003_session_schema_reconciles_before_migration(tmp_path, monkeypatch):
    database_url = f"sqlite:///{tmp_path / 'legacy.db'}"
    legacy_engine = create_engine(database_url)
    monkeypatch.setenv("MK_DATABASE_URL", database_url)
    alembic_config = Config(str(Path(__file__).parents[1] / "alembic.ini"))
    command.upgrade(alembic_config, "0003_add_is_admin")
    assert "session_type" not in {
        column["name"] for column in inspect(legacy_engine).get_columns("user_sessions")
    }

    app = create_app(Settings(database_url=database_url))
    with api_client(app):
        columns = {column["name"] for column in
                   inspect(legacy_engine).get_columns("user_sessions")}
        assert "session_type" in columns
    command.upgrade(alembic_config, "head")
    with legacy_engine.begin() as connection:
        connection.execute(text("""
            INSERT INTO user_sessions
                (token_hash, user_id, expires_at, created_at, last_seen_at)
            VALUES ('legacy', 1, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP,
                    CURRENT_TIMESTAMP)
        """))
        value = connection.execute(text(
            "SELECT session_type FROM user_sessions WHERE token_hash='legacy'"
        )).scalar_one()
    assert value == "game"
    assert connection_revision(legacy_engine) == "0004_scope_sessions"


def connection_revision(engine):
    with engine.connect() as connection:
        return connection.execute(text("SELECT version_num FROM alembic_version")).scalar_one()


def test_join_target_uses_request_address_and_limits_queue(tmp_path):
    app = create_app(Settings(database_url=f"sqlite:///{tmp_path / 'online.db'}",
                              join_max_entries=1, join_max_payload_bytes=1024))
    with api_client(app) as client:
        with app.state.session_factory() as db:
            db.add_all([
                User(username="host", email="host@example.test",
                     password_hash=hash_password("host-pass-123")),
                User(username="driver", email="driver@example.test",
                     password_hash=hash_password("drive-pass-123")),
            ])
            db.commit()
        host = login(client, "host", "host-pass-123")
        driver = login(client, "driver", "drive-pass-123")
        server = parse(client.post("/api/v2/server/create/", data={
            **host, "version": "7", "address": "192.0.2.10", "port": "2759",
        })).find("./server/server-info").attrib["id"]
        first = client.post("/api/v2/server/join-server-key/", data={
            **driver, "server-id": server, "address": "203.0.113.66",
            "port": "2759", "aes-key": "key", "aes-iv": "iv",
        }, headers={"x-forwarded-for": "198.51.100.99"})
        assert parse(first).attrib["success"] == "yes"
        second = parse(client.post("/api/v2/server/join-server-key/", data={
            **driver, "server-id": server, "port": "2759",
            "aes-key": "k" * 2000, "aes-iv": "iv",
        }))
        assert second.attrib["success"] == "no"
        polled = parse(client.post("/api/v2/server/poll-connection-requests/",
                                   data={**host, "port": "2759"}))
        assert polled.find("./users/user").attrib["ip"] == "2130706433"


def test_memory_join_queue_enforces_entry_and_payload_limits():
    directory = MemoryDirectory(20, 45, join_max_entries=1,
                                join_max_payload_bytes=128)
    directory.publish(1, {"name": "server"})
    assert directory.add_join(1, 2, {"aes-key": "key"})
    assert not directory.add_join(1, 3, {"aes-key": "key"})
    assert not directory.add_join(1, 4, {"aes-key": "x" * 256})


def test_redis_join_queue_limits_deduplicates_and_rejects_oversized_payloads():
    from app.directory import RedisDirectory

    directory = RedisDirectory("redis://localhost", 20, 45,
                               join_max_entries=2,
                               join_max_payload_bytes=128)
    directory.redis = fakeredis.FakeRedis(decode_responses=True)
    server = directory.publish(1, {"name": "server"})
    server_id = int(server["id"])

    assert directory.add_join(server_id, 10, {"aes-key": "old"})
    assert directory.add_join(server_id, 10, {"aes-key": "new"})
    assert directory.add_join(server_id, 11, {"aes-key": "key"})
    assert not directory.add_join(server_id, 12, {"aes-key": "key"})
    assert not directory.add_join(server_id, 13, {"aes-key": "x" * 256})

    queued = [json.loads(item) for item in directory.redis.lrange(
        directory._join_key(server_id), 0, -1)]
    assert [(item["id"], item["aes-key"]) for item in queued] == [
        ("10", "new"), ("11", "key")]


def test_redis_clear_is_atomic_preserves_other_users_and_expiry():
    from threading import Barrier, Thread
    from app.directory import RedisDirectory

    directory = RedisDirectory("redis://localhost", 20, 45,
                               join_max_entries=4)
    directory.redis = fakeredis.FakeRedis(decode_responses=True)
    server = directory.publish(1, {"name": "server"})
    server_id = int(server["id"])
    key = directory._join_key(server_id)

    directory.add_join(server_id, 10, {"aes-key": "disconnecting"})
    directory.add_join(server_id, 11, {"aes-key": "keep"})
    before = directory.redis.pttl(key)
    directory.clear_user_joins(10)
    assert [json.loads(item)["id"] for item in directory.redis.lrange(key, 0, -1)] == ["11"]
    assert 0 < directory.redis.pttl(key) <= before

    for _ in range(20):
        directory.add_join(server_id, 10, {"aes-key": "disconnecting"})
        before = directory.redis.pttl(key)
        barrier = Barrier(2)

        def clear():
            barrier.wait()
            directory.clear_user_joins(10)

        def add_concurrent():
            barrier.wait()
            assert directory.add_join(server_id, 11, {"aes-key": "concurrent"})

        clear_thread = Thread(target=clear)
        add_thread = Thread(target=add_concurrent)
        clear_thread.start()
        add_thread.start()
        clear_thread.join()
        add_thread.join()

        queued = [json.loads(item) for item in directory.redis.lrange(key, 0, -1)]
        assert [item["id"] for item in queued] == ["11"]


def test_redis_join_scripts_drop_malformed_entries():
    from app.directory import RedisDirectory

    directory = RedisDirectory("redis://localhost", 20, 45,
                               join_max_entries=1)
    directory.redis = fakeredis.FakeRedis(decode_responses=True)
    server = directory.publish(1, {"name": "server"})
    key = directory._join_key(int(server["id"]))
    directory.redis.rpush(key, "not-json")
    assert directory.add_join(int(server["id"]), 10, {"aes-key": "valid"})
    assert directory.redis.llen(key) == 1
    directory.redis.rpush(key, "not-json")
    directory.redis.rpush(key, "{}")
    directory.clear_user_joins(99)
    assert [json.loads(value)["id"] for value in directory.redis.lrange(key, 0, -1)] == ["10"]


def test_rendezvous_request_body_is_limited_before_form_parsing(tmp_path):
    app = create_app(Settings(database_url=f"sqlite:///{tmp_path / 'online.db'}",
                              join_max_request_bytes=128))
    with api_client(app) as client:
        response = client.post("/api/v2/server/join-server-key/",
                               content="x" * 129)
        assert response.status_code == 413
        assert "too large" in response.text
        chunked = client.post("/api/v2/server/join-server-key/",
                              content=iter([b"x" * 64, b"x" * 64, b"x"]))
        assert chunked.status_code == 413


def test_recovery_is_disabled_and_does_not_reset_password(tmp_path):
    app = create_app(Settings(database_url=f"sqlite:///{tmp_path / 'online.db'}"))
    with TestClient(app) as client:
        with app.state.session_factory() as db:
            db.add(User(username="victim",
                        email="victim@minkowskikart.internal",
                        password_hash=hash_password("original-pass-123")))
            db.commit()
        recover = parse(client.post("/api/v2/user/recover/", data={
            "username": "victim", "email": "victim@minkowskikart.internal",
        }))
        assert recover.attrib["success"] == "no"
        # No new password is leaked in the response...
        assert "password is:" not in recover.attrib.get("info", "")
        # ...and the original password still works.
        assert login(client, "victim", "original-pass-123")["userid"]
