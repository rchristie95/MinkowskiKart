"""Add is_admin to users.

Revision ID: 0003_add_is_admin
Revises: 0002_add_email
Create Date: 2026-06-08 20:50:00.000000

"""

from collections.abc import Sequence

from alembic import op
import sqlalchemy as sa


revision: str = "0003_add_is_admin"
down_revision: str | None = "0002_add_email"
branch_labels: Sequence[str] | None = None
depends_on: Sequence[str] | None = None


def upgrade() -> None:
    op.add_column("users", sa.Column("is_admin", sa.Boolean(), nullable=False, server_default=sa.text("false")))


def downgrade() -> None:
    op.drop_column("users", "is_admin")
