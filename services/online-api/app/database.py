from sqlalchemy import create_engine, inspect, text
from sqlalchemy.orm import sessionmaker

from .models import Base


def create_session_factory(database_url: str):
    connect_args = {}
    if database_url.startswith("sqlite"):
        connect_args["check_same_thread"] = False
    engine = create_engine(database_url, connect_args=connect_args)
    return engine, sessionmaker(bind=engine, expire_on_commit=False)


def init_schema(engine) -> None:
    # create_all builds any missing tables (fresh deploys), but it never alters
    # an existing table. The ``users`` table gained ``email`` and ``is_admin``
    # after the first release, so on a database created before those columns
    # existed every login query would crash. Reconcile them idempotently here
    # instead of depending on a separate ``alembic upgrade`` step that the
    # deployment guide never runs.
    Base.metadata.create_all(engine)
    _ensure_user_columns(engine)


def _ensure_user_columns(engine) -> None:
    inspector = inspect(engine)
    if "users" not in inspector.get_table_names():
        return
    columns = {col["name"] for col in inspector.get_columns("users")}

    with engine.begin() as conn:
        if "email" not in columns:
            # Add nullable, backfill a unique placeholder per user, then index.
            conn.execute(text(
                "ALTER TABLE users ADD COLUMN email VARCHAR(255)"))
            conn.execute(text(
                "UPDATE users SET email = username || '@minkowskikart.internal' "
                "WHERE email IS NULL"))
            conn.execute(text(
                "CREATE UNIQUE INDEX IF NOT EXISTS ix_users_email "
                "ON users (email)"))
        if "is_admin" not in columns:
            conn.execute(text(
                "ALTER TABLE users ADD COLUMN is_admin BOOLEAN NOT NULL "
                "DEFAULT FALSE"))
