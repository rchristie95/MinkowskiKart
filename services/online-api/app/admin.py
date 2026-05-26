import argparse
from getpass import getpass

from sqlalchemy import select

from .config import Settings
from .database import create_session_factory, init_schema
from .models import AdminAudit, User
from .security import hash_password


def main() -> None:
    parser = argparse.ArgumentParser(description="Manage invited MK accounts.")
    subparsers = parser.add_subparsers(dest="command", required=True)
    create = subparsers.add_parser("create-user")
    create.add_argument("--username", required=True)
    create.add_argument("--password")
    create.add_argument("--official-host", action="store_true")
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
                        official_host=args.official_host)
            db.add(user)
            db.flush()
            db.add(AdminAudit(action="create-user", target_user_id=user.id,
                              detail="official-host" if args.official_host else ""))
            db.commit()
            print(f"Created invited account '{username}' (id {user.id}).")


if __name__ == "__main__":
    main()
