# Build and host the browser build of the Slayer3D editor.
#
# Preferred entry point (from the repository root):
#   make web        # build the image and start the container
#   make web-stop   # stop the container
#
# Manual usage:
#   docker build -f docker/emscripten-editor/Dockerfile -t slayer3d-editor-web .
#   docker run --rm -it -p 8080:8080 slayer3d-editor-web
#
# Then open:
#   http://localhost:8080/slayer3d_editor_web.html

FROM emscripten/emsdk:latest

WORKDIR /src
COPY . .

RUN emcmake cmake -S . -B build/emscripten-editor -DCMAKE_BUILD_TYPE=Release
RUN cmake --build build/emscripten-editor --target slayer3d_editor_web --parallel

EXPOSE 8080
CMD ["python3", "-m", "http.server", "8080", "--bind", "0.0.0.0", "--directory", "build/emscripten-editor"]
