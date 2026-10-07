# HeapLens container image - only needed for platforms that run containers
# (Render, Hugging Face Spaces, Fly.io, Cloud Run ...). A plain VM does not need it.
#
#   docker build -t heaplens .
#   docker run --rm -p 8080:7860 heaplens        ->  http://localhost:8080/

# ---- build stage: compile the C++ with GCC, fully static so the runtime image needs no libraries
FROM gcc:14 AS build
WORKDIR /src
COPY include include
COPY src src
RUN g++ -std=c++17 -O2 -fno-strict-aliasing -static -Iinclude \
    src/core/*.cpp src/main.cpp src/server.cpp -o heaplens

# ---- runtime stage: the binary + the UI files
FROM debian:stable-slim
WORKDIR /app
COPY --from=build /src/heaplens /app/heaplens
COPY ui /app/ui
# The server listens on $PORT (platforms override this) on all interfaces.
ENV PORT=7860
EXPOSE 7860
USER 65534
CMD ["/app/heaplens", "--serve"]
