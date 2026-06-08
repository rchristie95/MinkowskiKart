from contextlib import asynccontextmanager
from typing import Any
import xml.etree.ElementTree as ET

from fastapi import FastAPI, Request
from fastapi.responses import Response
from sqlalchemy import select

from .config import Settings
from .database import create_session_factory, init_schema
from .directory import MemoryDirectory, RedisDirectory
from .models import User, UserSession, utc_now
from .security import (
    hash_password,
    new_session_token,
    session_expiry,
    token_hash,
    verify_password,
)


def xml_response(success: bool = True, info: str = "",
                 attrs: dict[str, str] | None = None,
                 children: list[ET.Element] | None = None) -> Response:
    attributes = {"success": "yes" if success else "no"}
    if info:
        attributes["info"] = info
    attributes.update(attrs or {})
    root = ET.Element("response", attributes)
    for child in children or []:
        root.append(child)
    return Response(ET.tostring(root, encoding="utf-8"),
                    media_type="application/xml")


async def get_form(request: Request) -> dict[str, str]:
    form = await request.form()
    return {str(key): str(value) for key, value in form.items()}


def server_info_element(record: dict[str, Any]) -> ET.Element:
    public_fields = {
        "id": str(record.get("id", "0")),
        "host_id": str(record.get("owner_id", "0")),
        "name": str(record.get("name", "MinkowskiKart Server")),
        "max_players": str(record.get("max_players", "8")),
        "current_players": str(record.get("current_players", "0")),
        "current_ai": str(record.get("current_ai", "0")),
        "current_track": str(record.get("current_track", "")),
        "difficulty": str(record.get("difficulty", "0")),
        "game_mode": str(record.get("game_mode", "3")),
        "ip": str(record.get("ip", "0")),
        "port": str(record.get("port", "0")),
        "ipv6": str(record.get("ipv6", "")),
        "private_port": str(record.get("private_port", "0")),
        "password": str(record.get("password", "0")),
        "game_started": str(record.get("game_started", "0")),
        "version": str(record.get("version", "7")),
        "aes_gcm_128bit_tag": str(record.get("aes_gcm_128bit_tag", "1")),
        "distance": "0",
        "country_code": "",
    }
    return ET.Element("server-info", public_fields)


def create_app(settings: Settings | None = None) -> FastAPI:
    settings = settings or Settings.from_env()
    engine, session_factory = create_session_factory(settings.database_url)
    directory = (
        RedisDirectory(settings.redis_url, settings.listing_ttl_seconds,
                       settings.join_ttl_seconds)
        if settings.redis_url else
        MemoryDirectory(settings.listing_ttl_seconds, settings.join_ttl_seconds)
    )

    @asynccontextmanager
    async def lifespan(_: FastAPI):
        init_schema(engine)
        yield

    app = FastAPI(title="MinkowskiKart Online API", version="0.1.0",
                  lifespan=lifespan)
    app.state.engine = engine
    app.state.session_factory = session_factory
    app.state.directory = directory

    def authenticate(values: dict[str, str], db) -> User | None:
        try:
            user_id = int(values.get("userid", "0"))
        except ValueError:
            return None
        token = values.get("token", "")
        if not token:
            return None
        session = db.get(UserSession, token_hash(token))
        user = db.get(User, user_id)
        if (not session or session.user_id != user_id or
                session.expires_at <= utc_now() or
                not user or not user.active):
            return None
        session.last_seen_at = utc_now()
        db.commit()
        return user

    @app.get("/healthz")
    def healthz() -> dict[str, str]:
        return {"status": "ok"}

    @app.post("/api/v2/user/connect/")
    async def connect(request: Request) -> Response:
        values = await get_form(request)
        username = values.get("username", "").strip().lower()
        password = values.get("password", "")
        with session_factory() as db:
            user = db.scalar(select(User).where(User.username == username))
            if not user or not user.active or not verify_password(
                    user.password_hash, password):
                return xml_response(False, "Username or password is invalid")
            token = new_session_token()
            db.add(UserSession(token_hash=token_hash(token), user_id=user.id,
                               expires_at=session_expiry(settings.session_days)))
            db.commit()
            return xml_response(attrs={
                "token": token,
                "userid": str(user.id),
                "username": user.username,
                "achieved": "",
            })

    @app.post("/api/v2/user/saved-session/")
    async def saved_session(request: Request) -> Response:
        values = await get_form(request)
        with session_factory() as db:
            user = authenticate(values, db)
            if not user:
                return xml_response(False, "Session not valid. Please sign in.")
            return xml_response(attrs={
                "token": values["token"],
                "userid": str(user.id),
                "username": user.username,
                "achieved": "",
            })

    @app.post("/api/v2/user/client-quit/")
    async def client_quit(request: Request) -> Response:
        values = await get_form(request)
        with session_factory() as db:
            return (xml_response() if authenticate(values, db) else
                    xml_response(False, "Session not valid. Please sign in."))

    @app.post("/api/v2/user/disconnect/")
    async def disconnect(request: Request) -> Response:
        values = await get_form(request)
        with session_factory() as db:
            user = authenticate(values, db)
            if not user:
                return xml_response(False, "Session not valid. Please sign in.")
            session = db.get(UserSession, token_hash(values["token"]))
            if session:
                db.delete(session)
                db.commit()
            directory.clear_user_joins(user.id)
            return xml_response()

    @app.post("/api/v2/user/poll/")
    async def user_poll(request: Request) -> Response:
        values = await get_form(request)
        with session_factory() as db:
            return (xml_response() if authenticate(values, db) else
                    xml_response(False, "Session not valid. Please sign in."))

    @app.post("/api/v2/user/change-password/")
    async def change_password(request: Request) -> Response:
        values = await get_form(request)
        with session_factory() as db:
            user = authenticate(values, db)
            if not user:
                return xml_response(False, "Session not valid. Please sign in.")
            new_password = values.get("new1", "")
            if not verify_password(user.password_hash, values.get("current", "")):
                return xml_response(False, "Current password is invalid")
            if new_password != values.get("new2", "") or not 8 <= len(new_password) <= 60:
                return xml_response(False,
                    "The password must be between 8 and 60 characters long")
            user.password_hash = hash_password(new_password)
            db.commit()
            return xml_response()

    @app.post("/api/v2/user/get-friends-list/")
    async def friends(request: Request) -> Response:
        values = await get_form(request)
        with session_factory() as db:
            if not authenticate(values, db):
                return xml_response(False, "Session not valid. Please sign in.")
        return xml_response(attrs={"visitingid": values.get("visitingid", "0")},
                            children=[ET.Element("friends")])

    @app.post("/api/v2/user/get-achievements/")
    @app.post("/api/v2/user/achieving/")
    async def achievements(request: Request) -> Response:
        values = await get_form(request)
        with session_factory() as db:
            if not authenticate(values, db):
                return xml_response(False, "Session not valid. Please sign in.")
        return xml_response(attrs={
            "achieved": "",
            "visitingid": values.get("visitingid", "0"),
        })

    @app.post("/api/v2/user/register/")
    async def register(request: Request) -> Response:
        import re
        from sqlalchemy.exc import IntegrityError
        
        values = await get_form(request)
        username = values.get("username", "").strip().lower()
        password = values.get("password", "")
        password_confirm = values.get("password_confirm", "")
        email = values.get("email", "").strip().lower()
        terms = values.get("terms", "")

        if terms != "on":
            return xml_response(False, "You must accept the terms and conditions.")
        if password != password_confirm:
            return xml_response(False, "Passwords do not match.")
        if not (8 <= len(password) <= 60):
            return xml_response(False, "Password must be between 8 and 60 characters.")
        if not (3 <= len(username) <= 30):
            return xml_response(False, "Username must be between 3 and 30 characters.")
        if not re.match(r"^[a-z][a-z0-9._-]*$", username):
            return xml_response(False, "Username is invalid.")
        if "@" not in email or "." not in email:
            return xml_response(False, "Email address is invalid.")

        with session_factory() as db:
            try:
                user = User(
                    username=username,
                    email=email,
                    password_hash=hash_password(password),
                    active=True
                )
                db.add(user)
                db.commit()
            except IntegrityError:
                return xml_response(False, "Username or email is already taken.")
            
            return xml_response(True, "Account created successfully! You can now sign in.")

    @app.post("/api/v2/user/recover/")
    async def invite_only(_: Request) -> Response:
        return xml_response(False, "Account recovery is not available.")

    @app.post("/api/v2/user/change-email/")
    @app.post("/api/v2/user/friend-request/")
    @app.post("/api/v2/user/accept-friend-request/")
    @app.post("/api/v2/user/decline-friend-request/")
    @app.post("/api/v2/user/remove-friend/")
    @app.post("/api/v2/user/cancel-friend-request/")
    @app.post("/api/v2/user/top-players/")
    @app.post("/api/v2/user/get-ranking/")
    @app.post("/api/v2/user/submit-ranking/")
    async def unavailable_social_services(request: Request) -> Response:
        values = await get_form(request)
        with session_factory() as db:
            if not authenticate(values, db):
                return xml_response(False, "Session not valid. Please sign in.")
        return xml_response(False, "This online feature is not available.")

    @app.post("/api/v2/server/create/")
    async def create_server(request: Request) -> Response:
        values = await get_form(request)
        with session_factory() as db:
            user = authenticate(values, db)
            if not user:
                return xml_response(False, "Session not valid. Please sign in.")
            if values.get("version") != str(settings.allowed_game_version):
                return xml_response(False, "Incompatible game protocol version")
            server = directory.publish(user.id, {
                "name": values.get("name", "MinkowskiKart Server"),
                "max_players": values.get("max_players", "8"),
                "difficulty": values.get("difficulty", "0"),
                "game_mode": values.get("game_mode", "3"),
                "ip": values.get("address", "0"),
                "port": values.get("port", "0"),
                "private_port": values.get("private_port", "0"),
                "ipv6": values.get("address_ipv6", ""),
                "password": values.get("password", "0"),
                "version": values["version"],
                "aes_gcm_128bit_tag": values.get("aes_gcm_128bit_tag", "1"),
                "current_players": "0",
                "current_ai": "0",
                "current_track": "",
                "game_started": "0",
            })
            element = ET.Element("server")
            ET.SubElement(element, "server-info", {
                "id": str(server["id"]),
                "official": "true" if user.official_host else "false",
            })
            return xml_response(children=[element])

    @app.post("/api/v2/server/get-all/")
    async def get_all(_: Request) -> Response:
        servers = ET.Element("servers")
        for record in directory.servers():
            server = ET.SubElement(servers, "server")
            server.append(server_info_element(record))
            ET.SubElement(server, "players")
        return xml_response(children=[servers])

    @app.post("/api/v2/server/join-server-key/")
    async def join_server(request: Request) -> Response:
        values = await get_form(request)
        with session_factory() as db:
            user = authenticate(values, db)
            if not user:
                return xml_response(False, "Session not valid. Please sign in.")
            try:
                server_id = int(values.get("server-id", "0"))
            except ValueError:
                return xml_response(False, "Invalid server id")
            accepted = directory.add_join(server_id, user.id, {
                "ip": values.get("address", "0"),
                "ipv6": values.get("address-ipv6", ""),
                "port": values.get("port", "0"),
                "aes-key": values.get("aes-key", ""),
                "aes-iv": values.get("aes-iv", ""),
                "username": user.username,
                "country-code": "",
            })
            return (xml_response() if accepted else
                    xml_response(False, "Server is no longer online"))

    @app.post("/api/v2/server/poll-connection-requests/")
    async def poll_server(request: Request) -> Response:
        values = await get_form(request)
        with session_factory() as db:
            user = authenticate(values, db)
            if not user:
                return xml_response(False, "Session not valid. Please sign in.")
            joins = directory.poll_joins(user.id, {
                "ip": values.get("address", "0"),
                "port": values.get("port", "0"),
                "current_players": values.get("current-players", "0"),
                "current_ai": values.get("current-ai", "0"),
                "current_track": values.get("current-track", ""),
                "game_started": values.get("game-started", "0"),
            })
            if joins is None:
                return xml_response(False, "Server listing expired")
            users = ET.Element("users")
            for join in joins:
                ET.SubElement(users, "user", {
                    key: str(value) for key, value in join.items()
                    if not key.startswith("_")
                })
            return xml_response(children=[users])

    @app.post("/api/v2/server/clear-user-joined-server/")
    async def clear_join(request: Request) -> Response:
        values = await get_form(request)
        with session_factory() as db:
            user = authenticate(values, db)
            if not user:
                return xml_response(False, "Session not valid. Please sign in.")
            directory.clear_user_joins(user.id)
            return xml_response()

    @app.post("/api/v2/server/update-config/")
    async def update_server(request: Request) -> Response:
        values = await get_form(request)
        with session_factory() as db:
            user = authenticate(values, db)
            if not user:
                return xml_response(False, "Session not valid. Please sign in.")
            server = directory.update_owned(user.id, {
                "difficulty": values.get("new-difficulty", "0"),
                "game_mode": values.get("new-game-mode", "3"),
            })
            return (xml_response() if server else
                    xml_response(False, "Server listing expired"))

    @app.post("/api/v2/server/stop/")
    async def stop_server(request: Request) -> Response:
        values = await get_form(request)
        with session_factory() as db:
            user = authenticate(values, db)
            if not user:
                return xml_response(False, "Session not valid. Please sign in.")
            directory.stop_owned(user.id)
            return xml_response()

    return app


app = create_app()
