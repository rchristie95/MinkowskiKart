import xml.etree.ElementTree as ET

from fastapi.testclient import TestClient

from app.config import Settings
from app.main import create_app
from app.models import User
from app.security import hash_password


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
    with TestClient(app) as client:
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
    with TestClient(app) as client:
        root = parse(client.post("/api/v2/user/register/", data={}))
        assert root.attrib["success"] == "no"


def test_open_registration_allows_multiple_emailless_accounts(tmp_path):
    # The in-game client never sends an email; each account must still get a
    # distinct placeholder so more than one player can register.
    app = create_app(Settings(database_url=f"sqlite:///{tmp_path / 'online.db'}"))
    with TestClient(app) as client:
        for name in ("alice", "bob"):
            root = parse(client.post("/api/v2/user/register/", data={
                "username": name, "password": "password-123",
                "password_confirm": "password-123", "terms": "on",
            }))
            assert root.attrib["success"] == "yes", root.attrib.get("info")
        # Re-using a username is still rejected.
        dupe = parse(client.post("/api/v2/user/register/", data={
            "username": "alice", "password": "password-123",
            "password_confirm": "password-123", "terms": "on",
        }))
        assert dupe.attrib["success"] == "no"


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
