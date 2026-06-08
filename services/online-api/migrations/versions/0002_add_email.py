"""Add email to users.

Revision ID: 0002_add_email
Revises: 0001_initial
Create Date: 2026-06-08 10:00:00.000000

"""

from collections.abc import Sequence

from alembic import op
import sqlalchemy as sa


revision: str = "0002_add_email"
down_revision: str | None = "0001_initial"
branch_labels: Sequence[str] | None = None
depends_on: Sequence[str] | None = None


def upgrade() -> None:
    # Add email column as nullable first to allow existing rows (if any)
    op.add_column("users", sa.Column("email", sa.String(length=255), nullable=True))
    
    # In a real app with data, we would populate email here.
    # Since it's a test game, we just set it to a dummy value if needed or keep it nullable.
    # But wait, the user wants a reset anyway.
    
    op.create_index("ix_users_email", "users", ["email"], unique=True)


def downgrade() -> None:
    op.drop_index("ix_users_email", table_name="users")
    op.drop_column("users", "email")
