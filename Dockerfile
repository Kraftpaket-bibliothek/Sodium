# ============================================================================
# Sodium - Production Docker Container for Cloud Deployment (Render.com)
# Multi-stage build: C11 CDCL Solver Core + Python Classical NLP Engine
# ============================================================================

# Stage 1: Build Environment
FROM python:3.12-slim-bookworm AS builder

# Install build essentials for C11 compilation
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    clang \
    make \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Install Python requirements
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copy source tree and compile server + shared solver library
COPY . .
RUN make server

# Stage 2: Minimal Production Runtime
FROM python:3.12-slim-bookworm AS runner

WORKDIR /app

# Copy Python site-packages from builder stage
COPY --from=builder /usr/local/lib/python3.12/site-packages /usr/local/lib/python3.12/site-packages
COPY --from=builder /usr/local/bin /usr/local/bin

# Copy compiled native binaries and shared library
COPY --from=builder /app/sodium /app/sodium
COPY --from=builder /app/libsodium_sat.* /app/

# Copy Python NLP modules and source files
COPY --from=builder /app/src /app/src
COPY --from=builder /app/requirements.txt /app/requirements.txt

# Render passes $PORT at runtime (defaults to 10000 on Render)
ENV PORT=10000
ENV PYTHONUNBUFFERED=1

# Expose Render default port
EXPOSE 10000

# Run Sodium in web server mode (automatically binds to 0.0.0.0:$PORT)
CMD ["./sodium", "--serve"]
