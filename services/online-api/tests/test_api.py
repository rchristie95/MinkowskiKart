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
                User(username="host", password_hash=hash_password("host-pass-123")),
                User(username="driver", password_hash=hash_password("drive-pass-123")),
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
        assert listing.find("./servers/server/server-info").attrib["name"] == "Test Room"
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


def test_registration_is_invite_only(tmp_path):
    app = create_app(Settings(database_url=f"sqlite:///{tmp_path / 'online.db'}"))
    with TestClient(app) as client:
        root = parse(client.post("/api/v2/user/register/", data={}))
        assert root.attrib["success"] == "no"
        assert "invite-only" in root.attrib["info"]
