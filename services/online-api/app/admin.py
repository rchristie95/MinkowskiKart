import argparse
from getpass import getpass

import sqlalchemy as sa
from sqlalchemy import select

from .config import Settings
from .database import create_session_factory, init_schema
from .models import AdminAudit, User, UserSession
from .security import hash_password


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


if __name__ == "__main__":
    main()
