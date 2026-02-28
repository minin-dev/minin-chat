# ============================================================
# MININ-CHAT Docker Build
# Multi-stage: Build with full toolchain, run with minimal libs
# ============================================================

# ---- STAGE 1: BUILD ----
FROM debian:bookworm-slim AS builder

# Install build toolchain: C, Fortran, COBOL
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc \
    gfortran \
    gnucobol4 \
    make \
    libc6-dev \
    && rm -rf /var/lib/apt/lists/*

# Copy source
COPY backend/ /src/
COPY frontend/index.html /src/static/index.html

WORKDIR /src

# Build everything
RUN make all

# Strip binaries for minimal size
RUN strip server chat 2>/dev/null || true

# Compress compiled binaries with UPX (reduces final image size)
# Installed only in the builder stage so runtime stays minimal.
RUN apt-get update && apt-get install -y --no-install-recommends upx-ucl && \
    upx --best --lzma server chat || true && \
    rm -rf /var/lib/apt/lists/* || true

# Show build artifacts
RUN echo "=== BUILD ARTIFACTS ===" && \
    ls -lah server chat static/index.html && \
    echo "=== BINARY SIZES ===" && \
    size server || true

# ---- STAGE 2: RUNTIME (MINIMAL) ----
FROM debian:bookworm-slim

# Labels
LABEL maintainer="minin-chat" \
      description="MININ-CHAT: COBOL+Fortran+C Chat Server" \
      version="1.0"

# Install ONLY runtime libraries (no compilers, no dev packages)
RUN apt-get update && apt-get install -y --no-install-recommends \
    libgfortran5 \
    libncursesw6 \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/* \
              /usr/share/doc \
              /usr/share/man \
              /usr/share/locale \
              /usr/share/info \
              /var/cache/* \
              /var/log/* \
              /tmp/*

# Create app directory structure
RUN mkdir -p /app/static

# Copy COBOL runtime library from builder (matches compiler version exactly)
COPY --from=builder /usr/lib/*/libcob*.so* /usr/lib/
RUN ldconfig

# Copy built artifacts from builder
COPY --from=builder /src/server /app/server
COPY --from=builder /src/chat   /app/chat
COPY --from=builder /src/static/index.html /app/static/index.html

WORKDIR /app

# Server port
EXPOSE 3000

# Health check
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s \
    CMD echo -e "GET / HTTP/1.0\r\n\r\n" | \
        timeout 2 bash -c 'cat > /dev/tcp/localhost/3000' || exit 1

# Run the server
CMD ["./server"]
