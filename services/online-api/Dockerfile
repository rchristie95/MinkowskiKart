FROM python:3.12-slim

WORKDIR /app
COPY pyproject.toml /app/
COPY app /app/app
COPY alembic.ini /app/
COPY migrations /app/migrations
RUN pip install --no-cache-dir .

EXPOSE 8000
CMD ["sh", "-c", "alembic upgrade head && (python -m app.admin ensure-official-host || true) && uvicorn app.main:app --host 0.0.0.0 --port 8000"]
