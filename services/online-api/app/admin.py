import argparse
import os
from getpass import getpass

import sqlalchemy as sa
from sqlalchemy import select

from .config import Settings
from .database import create_session_factory, init_schema
from .models import AdminAudit, User, UserSession
from .security import hash_password, verify_password


def main() -> None:
    parser = argparse.ArgumentParser(description="Manage invited MK accounts.")
    subparsers = parser.add_subparsers(dest="command", required=True)
    create = subparsers.add_parser("create-user")
    create.add_argument("--username", required=True)
    create.add_argument("--password")
    create.add_argument("--official-host", action="store_true")
    create.add_argument("--is-admin", action="store_true")

    delete = subparsers.add_parser("delete-user")
    delete.add_argument("--username", required=True)

    reset = subparsers.add_parser("reset-password")
    reset.add_argument("--username", required=True)
    reset.add_argument("--password")

    # Idempotent boot-time provisioning for the official host account. Reads
    # MK_OFFICIAL_USERNAME / MK_OFFICIAL_PASSWORD from the environment so the
    # API can self-heal the account the dedicated server logs in with.
    subparsers.add_parser("ensure-official-host")

    args = parser.parse_args()

    settings = Settings.from_env()
    engine, session_factory = create_session_factory(settings.database_url)
    init_schema(engine)

    if args.command == "create-user":
        username = args.username.strip().lower()
        password = args.password or getpass("Password: ")
        if not 8 <= len(password) <= 60:
            raise SystemExit("Password must contain between 8 and 60 characters.")
        with session_factory() as db:
            if db.scalar(select(User).where(User.username == username)):
                raise SystemExit("That username already exists.")
            user = User(username=username, password_hash=hash_password(password),
                        official_host=args.official_host,
                        is_admin=args.is_admin,
                        email=f"{username}@minkowskikart.internal")
            db.add(user)
            db.flush()
            detail = []
            if args.official_host: detail.append("official-host")
            if args.is_admin: detail.append("admin")
            db.add(AdminAudit(action="create-user", target_user_id=user.id,
                              detail=",".join(detail)))
            db.commit()
            print(f"Created invited account '{username}' (id {user.id}).")

    elif args.command == "delete-user":
        username = args.username.strip().lower()
        with session_factory() as db:
            user = db.scalar(select(User).where(User.username == username))
            if not user:
                raise SystemExit(f"User '{username}' not found.")
            
            # Delete sessions first
            db.execute(sa.delete(UserSession).where(UserSession.user_id == user.id))
            db.delete(user)
            db.add(AdminAudit(action="delete-user", target_user_id=user.id, detail=username))
            db.commit()
            print(f"Deleted account '{username}' (id {user.id}).")

    elif args.command == "reset-password":
        username = args.username.strip().lower()
        password = args.password or getpass("New password: ")
        if not 8 <= len(password) <= 60:
            raise SystemExit("Password must contain between 8 and 60 characters.")
        with session_factory() as db:
            user = db.scalar(select(User).where(User.username == username))
            if not user:
                raise SystemExit(f"User '{username}' not found.")
            user.password_hash = hash_password(password)
            # Invalidate existing sessions so the old password cannot linger.
            db.execute(sa.delete(UserSession).where(UserSession.user_id == user.id))
            db.add(AdminAudit(action="reset-password", target_user_id=user.id,
                              detail=username))
            db.commit()
            print(f"Reset password for '{username}' (id {user.id}).")

    elif args.command == "ensure-official-host":
        # Best-effort: this runs on every API boot, so it must never raise
        # (a non-zero exit would block uvicorn from starting). The caller is
        # expected to guard with `|| true` as well.
        username = (os.environ.get("MK_OFFICIAL_USERNAME")
                    or "official-host").strip().lower()
        password = os.environ.get("MK_OFFICIAL_PASSWORD") or ""
        if not 8 <= len(password) <= 60:
            print("ensure-official-host: MK_OFFICIAL_PASSWORD is unset or not "
                  "8-60 chars; skipping.")
            return
        try:
            with session_factory() as db:
                user = db.scalar(select(User).where(User.username == username))
                if user is None:
                    user = User(username=username,
                                password_hash=hash_password(password),
                                official_host=True, is_admin=False,
                                email=f"{username}@minkowskikart.internal")
                    db.add(user)
                    db.flush()
                    db.add(AdminAudit(action="ensure-official-host",
                                      target_user_id=user.id, detail="created"))
                    db.commit()
                    print(f"ensure-official-host: created '{username}' "
                          f"(id {user.id}).")
                    return
                changed = []
                if not user.official_host:
                    user.official_host = True
                    changed.append("official_host")
                if not user.active:
                    user.active = True
                    changed.append("active")
                if not verify_password(user.password_hash, password):
                    user.password_hash = hash_password(password)
                    # Drop sessions tied to the old password.
                    db.execute(sa.delete(UserSession)
                               .where(UserSession.user_id == user.id))
                    changed.append("password")
                if changed:
                    db.add(AdminAudit(action="ensure-official-host",
                                      target_user_id=user.id,
                                      detail=",".join(changed)))
                    db.commit()
                    print(f"ensure-official-host: repaired '{username}' "
                          f"({', '.join(changed)}).")
                else:
                    print(f"ensure-official-host: '{username}' already correct.")
        except Exception as exc:  # noqa: BLE001 - never block API startup
            print(f"ensure-official-host: skipped due to error: {exc}")


if __name__ == "__main__":
    main()
