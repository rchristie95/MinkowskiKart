"""Lightweight fixed-window rate limiting for authentication endpoints.

Uses Redis when configured (so limits are shared across API workers and
container restarts), and falls back to an in-process counter otherwise. This is
intentionally simple: it protects the login/register/recover endpoints from
brute-force and abuse without adding a heavy dependency.
"""
from __future__ import annotations

import threading
import time


class RateLimiter:
    def hit(self, bucket: str, identity: str, limit: int, window: int) -> bool:
        """Record one event and return True if it is within the limit.

        ``bucket`` names the endpoint, ``identity`` is usually the client IP,
        ``limit`` is the maximum number of events allowed per ``window`` seconds.
        Returns False once the limit is exceeded for the current window.
        """
        raise NotImplementedError


class MemoryRateLimiter(RateLimiter):
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._hits: dict[str, tuple[int, float]] = {}

    def hit(self, bucket: str, identity: str, limit: int, window: int) -> bool:
        key = f"{bucket}:{identity}"
        now = time.time()
        with self._lock:
            count, reset_at = self._hits.get(key, (0, now + window))
            if now >= reset_at:
                count, reset_at = 0, now + window
            count += 1
            self._hits[key] = (count, reset_at)
            # Opportunistic cleanup so the dict cannot grow without bound.
            if len(self._hits) > 10000:
                self._hits = {
                    k: v for k, v in self._hits.items() if v[1] > now
                }
            return count <= limit


class RedisRateLimiter(RateLimiter):
    def __init__(self, redis_url: str) -> None:
        from redis import Redis
        self._redis = Redis.from_url(redis_url, decode_responses=True)

    def hit(self, bucket: str, identity: str, limit: int, window: int) -> bool:
        key = f"mk:rl:{bucket}:{identity}"
        try:
            with self._redis.pipeline() as pipe:
                pipe.incr(key)
                pipe.expire(key, window, nx=True)
                count, _ = pipe.execute()
        except Exception:
            # Never let a rate-limiter backend outage take down auth.
            return True
        return int(count) <= limit


def create_rate_limiter(redis_url: str) -> RateLimiter:
    return RedisRateLimiter(redis_url) if redis_url else MemoryRateLimiter()
