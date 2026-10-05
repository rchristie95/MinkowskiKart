"""Distinguish browser admin sessions from game client sessions."""

from collections.abc import Sequence

from alembic import op
import sqlalchemy as sa


revision: str = "0004_scope_sessions"
down_revision: str | None = "0003_add_is_admin"
branch_labels: Sequence[str] | None = None
depends_on: Sequence[str] | None = None


def upgrade() -> None:
    columns = {column["name"] for column in
               sa.inspect(op.get_bind()).get_columns("user_sessions")}
    if "session_type" not in columns:
        op.add_column(
            "user_sessions",
            sa.Column("session_type", sa.String(length=16), nullable=False,
                      server_default="game"),
        )


def downgrade() -> None:
    columns = {column["name"] for column in
               sa.inspect(op.get_bind()).get_columns("user_sessions")}
    if "session_type" in columns:
        op.drop_column("user_sessions", "session_type")
