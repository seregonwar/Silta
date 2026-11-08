# Third-party software

The MIT license in this repository covers Silta's original code. Dependencies
and bundled assets retain their respective copyright notices and licenses.

| Dependency | Version | License | Source |
| --- | --- | --- | --- |
| Dear ImGui | 1.92.9b | MIT | [ocornut/imgui](https://github.com/ocornut/imgui/tree/v1.92.9b) |
| GLFW | 3.4 | zlib/libpng | [glfw/glfw](https://github.com/glfw/glfw/tree/3.4) |
| FreeType | 2.13.3 | FreeType License or GPLv2; packages include the FreeType License | [freetype/freetype](https://github.com/freetype/freetype/tree/VER-2-13-3) |
| HarfBuzz | 14.6.0 | Old MIT | [harfbuzz/harfbuzz](https://github.com/harfbuzz/harfbuzz/tree/14.6.0) |
| SheenBidi | 2.9.0 | Apache 2.0 | [Tehreer/SheenBidi](https://github.com/Tehreer/SheenBidi/tree/v2.9.0) |
| Inter | 4.1 | SIL Open Font License 1.1 | [rsms/inter](https://github.com/rsms/inter) |
| miniaudio | 0.11.23 | Public domain or MIT No Attribution | [mackron/miniaudio](https://github.com/mackron/miniaudio/tree/0.11.23) |
| FFmpeg shared distribution | Locked in `.github/dependencies/ffmpeg.json` | LGPLv3; bundled components retain their own licenses | [FFmpeg](https://ffmpeg.org), [build recipes](https://github.com/FFmpeg-Builds/FFmpeg-Builds) |

FreeType credits: portions of this software are copyright © 2024 The FreeType
Project (https://freetype.org). All rights reserved.

The Windows package includes dependency notices in `licenses/`, FFmpeg's
documentation in `ffmpeg-doc/`, and a `BUILD.json` manifest containing the source
commit, dependency versions and FFmpeg source/build links. The FFmpeg archive
is downloaded from a dated release and checked against its recorded SHA-256.
Its command-line configuration and linked libraries are documented by the
upstream distribution; changing that distribution may change its license terms.

Silta takes design inspiration from [shadcn/ui](https://ui.shadcn.com) and uses
Lucide-style vector icons. It is an independent C++ implementation.
