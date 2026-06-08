from contextlib import asynccontextmanager
from typing import Any
import xml.etree.ElementTree as ET

import sqlalchemy as sa
from fastapi import FastAPI, Request
from fastapi.responses import Response
from sqlalchemy import select

from .config import Settings
from .database import create_session_factory, init_schema
from .directory import MemoryDirectory, RedisDirectory
from .models import AdminAudit, User, UserSession, utc_now
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

    @app.get("/terms")
    def terms() -> Response:
        return Response("<html><body style='font-family:sans-serif;max-width:600px;margin:40px auto;line-height:1.6'><h1>Terms and Conditions</h1><p>Relativistic racing is a privilege, not a right. Drive responsibly and respect the speed of light.</p></body></html>", media_type="text/html")

    @app.get("/account-help")
    def account_help() -> Response:
        return Response("<html><body style='font-family:sans-serif;max-width:600px;margin:40px auto;line-height:1.6'><h1>Account Help</h1><p>If you've lost your password, use the in-game 'Recover' button. <strong>Note:</strong> Email delivery is disabled. If your details match, your new password will be displayed directly on the recovery screen.</p></body></html>", media_type="text/html")

    @app.post("/telemetry")
    async def telemetry() -> Response:
        return Response(status_code=204)

    # --- Admin Dashboard ---
    from fastapi.responses import HTMLResponse, RedirectResponse
    from fastapi import Form, Cookie

    @app.get("/admin/login", response_class=HTMLResponse)
    async def admin_login_get(error: str | None = None):
        return f"""
        <html><body style='font-family:sans-serif;max-width:400px;margin:100px auto;background:#090b15;color:#f4f7ff'>
            <h1 style='text-align:center'>Admin Login</h1>
            {f'<p style="color:#ff6b6b;text-align:center">{error}</p>' if error else ''}
            <form action="/admin/login" method="post" style='display:grid;gap:10px;background:#1a1d2e;padding:20px;border-radius:8px'>
                <label>Username (Email)</label>
                <input type="text" name="username" style='padding:8px;border-radius:4px;border:1px solid #333;background:#090b15;color:white'>
                <label>Password</label>
                <input type="password" name="password" style='padding:8px;border-radius:4px;border:1px solid #333;background:#090b15;color:white'>
                <button type="submit" style='padding:10px;background:#4a90e2;color:white;border:none;border-radius:4px;cursor:pointer;margin-top:10px'>Login</button>
            </form>
        </body></html>
        """

    @app.post("/admin/login")
    async def admin_login_post(username: str = Form(...), password: str = Form(...)):
        username = username.strip().lower()
        with session_factory() as db:
            user = db.scalar(select(User).where((User.username == username) | (User.email == username)))
            if not user or not user.is_admin or not verify_password(user.password_hash, password):
                return RedirectResponse(url="/admin/login?error=Invalid+credentials", status_code=303)
            
            token = new_session_token()
            db.add(UserSession(token_hash=token_hash(token), user_id=user.id,
                               expires_at=session_expiry(1))) # 1 day admin session
            db.commit()
            
            response = RedirectResponse(url="/admin/dashboard", status_code=303)
            response.set_cookie(key="admin_token", value=token, httponly=True, samesite="lax")
            return response

    def get_admin(admin_token: str | None, db) -> User | None:
        if not admin_token: return None
        session = db.get(UserSession, token_hash(admin_token))
        if not session or session.expires_at <= utc_now(): return None
        user = db.get(User, session.user_id)
        return user if user and user.is_admin else None

    @app.get("/admin/dashboard", response_class=HTMLResponse)
    async def admin_dashboard(admin_token: str | None = Cookie(None)):
        with session_factory() as db:
            admin = get_admin(admin_token, db)
            if not admin: return RedirectResponse(url="/admin/login", status_code=303)
            
            users = db.scalars(select(User).order_by(User.id)).all()
            user_rows = ""
            for u in users:
                user_rows += f"""
                <tr style='border-bottom:1px solid #333'>
                    <td style='padding:10px'>{u.id}</td>
                    <td style='padding:10px'>{u.username}</td>
                    <td style='padding:10px'>{u.email}</td>
                    <td style='padding:10px'>{'✅' if u.is_admin else '❌'}</td>
                    <td style='padding:10px'>
                        <form action="/admin/delete-user" method="post" style='margin:0' onsubmit="return confirm('Delete {u.username}?')">
                            <input type="hidden" name="user_id" value="{u.id}">
                            <button type="submit" style='color:#ff6b6b;background:none;border:none;cursor:pointer;text-decoration:underline'>Delete</button>
                        </form>
                    </td>
                </tr>
                """

            return f"""
            <html><body style='font-family:sans-serif;max-width:1000px;margin:40px auto;background:#090b15;color:#f4f7ff'>
                <div style='display:flex;justify-content:space-between;align-items:center;margin-bottom:20px'>
                    <h1>MinkowskiKart Admin</h1>
                    <div>
                        <span>Logged in as <b>{admin.username}</b></span> | 
                        <a href="/admin/logout" style='color:#4a90e2'>Logout</a>
                    </div>
                </div>
                <table style='width:100%;border-collapse:collapse;background:#1a1d2e;border-radius:8px;overflow:hidden'>
                    <thead style='background:#2a2e45'>
                        <tr>
                            <th style='padding:15px;text-align:left'>ID</th>
                            <th style='padding:15px;text-align:left'>Username</th>
                            <th style='padding:15px;text-align:left'>Email</th>
                            <th style='padding:15px;text-align:left'>Admin</th>
                            <th style='padding:15px;text-align:left'>Actions</th>
                        </tr>
                    </thead>
                    <tbody>{user_rows}</tbody>
                </table>
            </body></html>
            """

    @app.post("/admin/delete-user")
    async def admin_delete_user(user_id: int = Form(...), admin_token: str | None = Cookie(None)):
        with session_factory() as db:
            admin = get_admin(admin_token, db)
            if not admin: return RedirectResponse(url="/admin/login", status_code=303)
            
            user = db.get(User, user_id)
            if user:
                if user.id == admin.id:
                    return HTMLResponse("Cannot delete yourself", status_code=400)
                db.execute(sa.delete(UserSession).where(UserSession.user_id == user.id))
                db.delete(user)
                db.add(AdminAudit(action="delete-user-web", target_user_id=user.id, detail=f"by {admin.username}"))
                db.commit()
            return RedirectResponse(url="/admin/dashboard", status_code=303)

    @app.get("/admin/logout")
    async def admin_logout(admin_token: str | None = Cookie(None)):
        if admin_token:
            with session_factory() as db:
                session = db.get(UserSession, token_hash(admin_token))
                if session:
                    db.delete(session)
                    db.commit()
        response = RedirectResponse(url="/admin/login")
        response.delete_cookie("admin_token")
        return response

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
        email = values.get("email", "no-email@minkowskikart.internal").strip().lower()
        terms = values.get("terms", "on")

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
                return xml_response(False, "Username is already taken.")
            
            return xml_response(True, "Account created! You can now sign in with your username and password.")

    @app.post("/api/v2/user/recover/")
    async def recover(request: Request) -> Response:
        import secrets
        values = await get_form(request)
        username = values.get("username", "").strip().lower()
        email = values.get("email", "").strip().lower()

        with session_factory() as db:
            user = db.scalar(select(User).where(
                (User.username == username) & (User.email == email)
            ))
            if not user:
                return xml_response(False, "No account found with that username and email.")
            
            new_password = secrets.token_urlsafe(8)
            user.password_hash = hash_password(new_password)
            db.commit()
            
            # We return success=False so that the C++ client displays this text immediately
            # on the recovery screen, since we don't have an email server configured.
            return xml_response(False, f"Password reset! Your new password is: {new_password}")

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
