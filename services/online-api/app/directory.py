from __future__ import annotations

import json
import threading
import time
from typing import Any

from redis import Redis


class MemoryDirectory:
    def __init__(self, listing_ttl: int, join_ttl: int):
        self.listing_ttl = listing_ttl
        self.join_ttl = join_ttl
        self._next_id = 1
        self._servers: dict[int, dict[str, Any]] = {}
        self._owner_server: dict[int, int] = {}
        self._joins: dict[int, list[dict[str, Any]]] = {}
        self._lock = threading.Lock()

    def _cleanup(self) -> None:
        now = time.time()
        expired = [
            sid for sid, server in self._servers.items()
            if server["_expires_at"] <= now
        ]
        for sid in expired:
            owner = int(self._servers[sid]["owner_id"])
            self._servers.pop(sid, None)
            self._joins.pop(sid, None)
            if self._owner_server.get(owner) == sid:
                self._owner_server.pop(owner, None)

    def publish(self, owner_id: int, values: dict[str, str]) -> dict[str, Any]:
        with self._lock:
            self._cleanup()
            old_id = self._owner_server.get(owner_id)
            if old_id:
                self._servers.pop(old_id, None)
                self._joins.pop(old_id, None)
            server_id = self._next_id
            self._next_id += 1
            record = dict(values, id=str(server_id), owner_id=str(owner_id),
                          _expires_at=time.time() + self.listing_ttl)
            self._servers[server_id] = record
            self._owner_server[owner_id] = server_id
            return dict(record)

    def servers(self) -> list[dict[str, Any]]:
        with self._lock:
            self._cleanup()
            return [dict(server) for server in self._servers.values()]

    def server(self, server_id: int) -> dict[str, Any] | None:
        with self._lock:
            self._cleanup()
            server = self._servers.get(server_id)
            return dict(server) if server else None

    def update_owned(self, owner_id: int,
                     values: dict[str, str]) -> dict[str, Any] | None:
        with self._lock:
            self._cleanup()
            server_id = self._owner_server.get(owner_id)
            if not server_id or server_id not in self._servers:
                return None
            self._servers[server_id].update(values)
            self._servers[server_id]["_expires_at"] = (
                time.time() + self.listing_ttl
            )
            return dict(self._servers[server_id])

    def stop_owned(self, owner_id: int) -> bool:
        with self._lock:
            server_id = self._owner_server.pop(owner_id, None)
            if not server_id:
                return False
            self._servers.pop(server_id, None)
            self._joins.pop(server_id, None)
            return True

    def add_join(self, server_id: int, user_id: int,
                 values: dict[str, str]) -> bool:
        with self._lock:
            self._cleanup()
            if server_id not in self._servers:
                return False
            expiry = time.time() + self.join_ttl
            pending = self._joins.setdefault(server_id, [])
            pending[:] = [
                request for request in pending
                if int(request["id"]) != user_id and request["_expires_at"] > time.time()
            ]
            pending.append(dict(values, id=str(user_id), _expires_at=expiry))
            return True

    def poll_joins(self, owner_id: int,
                   heartbeat: dict[str, str]) -> list[dict[str, Any]] | None:
        with self._lock:
            self._cleanup()
            server_id = self._owner_server.get(owner_id)
            if not server_id or server_id not in self._servers:
                return None
            self._servers[server_id].update(heartbeat)
            self._servers[server_id]["_expires_at"] = (
                time.time() + self.listing_ttl
            )
            now = time.time()
            joins = [
                request for request in self._joins.pop(server_id, [])
                if request["_expires_at"] > now
            ]
            return joins

    def clear_user_joins(self, user_id: int) -> None:
        with self._lock:
            for requests in self._joins.values():
                requests[:] = [
                    request for request in requests
                    if int(request["id"]) != user_id
                ]


class RedisDirectory:
    def __init__(self, redis_url: str, listing_ttl: int, join_ttl: int):
        self.redis = Redis.from_url(redis_url, decode_responses=True)
        self.listing_ttl = listing_ttl
        self.join_ttl = join_ttl

    @staticmethod
    def _server_key(server_id: int) -> str:
        return f"mk:server:{server_id}"

    @staticmethod
    def _owner_key(owner_id: int) -> str:
        return f"mk:owner:{owner_id}"

    @staticmethod
    def _join_key(server_id: int) -> str:
        return f"mk:join:{server_id}"

    def publish(self, owner_id: int, values: dict[str, str]) -> dict[str, Any]:
        old_id = self.redis.get(self._owner_key(owner_id))
        if old_id:
            self.redis.delete(self._server_key(int(old_id)),
                              self._join_key(int(old_id)))
        server_id = int(self.redis.incr("mk:next-server-id"))
        record = dict(values, id=str(server_id), owner_id=str(owner_id))
        with self.redis.pipeline() as pipe:
            pipe.setex(self._server_key(server_id), self.listing_ttl,
                       json.dumps(record))
            pipe.setex(self._owner_key(owner_id), self.listing_ttl,
                       str(server_id))
            pipe.execute()
        return record

    def servers(self) -> list[dict[str, Any]]:
        records = []
        for key in self.redis.scan_iter("mk:server:*"):
            raw = self.redis.get(key)
            if raw:
                records.append(json.loads(raw))
        return records

    def server(self, server_id: int) -> dict[str, Any] | None:
        raw = self.redis.get(self._server_key(server_id))
        return json.loads(raw) if raw else None

    def update_owned(self, owner_id: int,
                     values: dict[str, str]) -> dict[str, Any] | None:
        server_id = self.redis.get(self._owner_key(owner_id))
        if not server_id:
            return None
        record = self.server(int(server_id))
        if not record:
            return None
        record.update(values)
        with self.redis.pipeline() as pipe:
            pipe.setex(self._server_key(int(server_id)), self.listing_ttl,
                       json.dumps(record))
            pipe.expire(self._owner_key(owner_id), self.listing_ttl)
            pipe.execute()
        return record

    def stop_owned(self, owner_id: int) -> bool:
        server_id = self.redis.get(self._owner_key(owner_id))
        if not server_id:
            return False
        self.redis.delete(self._owner_key(owner_id),
                          self._server_key(int(server_id)),
                          self._join_key(int(server_id)))
        return True

    def add_join(self, server_id: int, user_id: int,
                 values: dict[str, str]) -> bool:
        if not self.server(server_id):
            return False
        record = dict(values, id=str(user_id))
        key = self._join_key(server_id)
        with self.redis.pipeline() as pipe:
            pipe.rpush(key, json.dumps(record))
            pipe.expire(key, self.join_ttl)
            pipe.execute()
        return True

    def poll_joins(self, owner_id: int,
                   heartbeat: dict[str, str]) -> list[dict[str, Any]] | None:
        record = self.update_owned(owner_id, heartbeat)
        if not record:
            return None
        key = self._join_key(int(record["id"]))
        with self.redis.pipeline() as pipe:
            pipe.lrange(key, 0, -1)
            pipe.delete(key)
            raw, _ = pipe.execute()
        return [json.loads(value) for value in raw]

    def clear_user_joins(self, user_id: int) -> None:
        for key in self.redis.scan_iter("mk:join:*"):
            raw = self.redis.lrange(key, 0, -1)
            remaining = [
                value for value in raw
                if int(json.loads(value)["id"]) != user_id
            ]
            with self.redis.pipeline() as pipe:
                pipe.delete(key)
                if remaining:
                    pipe.rpush(key, *remaining)
                    pipe.expire(key, self.join_ttl)
                pipe.execute()
