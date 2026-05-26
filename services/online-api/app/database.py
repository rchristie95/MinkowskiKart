from sqlalchemy import create_engine
from sqlalchemy.orm import sessionmaker

from .models import Base


def create_session_factory(database_url: str):
    connect_args = {}
    if database_url.startswith("sqlite"):
        connect_args["check_same_thread"] = False
    engine = create_engine(database_url, connect_args=connect_args)
    return engine, sessionmaker(bind=engine, expire_on_commit=False)


def init_schema(engine) -> None:
    Base.metadata.create_all(engine)
