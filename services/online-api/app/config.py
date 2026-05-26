from dataclasses import dataclass
import os


@dataclass(frozen=True)
class Settings:
    database_url: str = "sqlite:///./minkowski_online.db"
    redis_url: str = ""
    session_days: int = 30
    listing_ttl_seconds: int = 20
    join_ttl_seconds: int = 45
    allowed_game_version: int = 7

    @classmethod
    def from_env(cls) -> "Settings":
        return cls(
            database_url=os.getenv("MK_DATABASE_URL", cls.database_url),
            redis_url=os.getenv("MK_REDIS_URL", cls.redis_url),
            session_days=int(os.getenv("MK_SESSION_DAYS", cls.session_days)),
            listing_ttl_seconds=int(
                os.getenv("MK_LISTING_TTL_SECONDS", cls.listing_ttl_seconds)
            ),
            join_ttl_seconds=int(
                os.getenv("MK_JOIN_TTL_SECONDS", cls.join_ttl_seconds)
            ),
            allowed_game_version=int(
                os.getenv("MK_ALLOWED_GAME_VERSION", cls.allowed_game_version)
            ),
        )
