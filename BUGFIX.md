# GKHub 빌드 버그 수정 내역

## 수정된 버그 목록

---

### Bug 1: `RmlBackend.cpp` — `GL/gl.h` 파싱 오류 (100+ 에러)

**에러 메시지:**
```
gl.h(1157): error C2144: syntax error: 'void' should be preceded by ';'
gl.h(1157): error C4430: missing type specifier - int assumed
gl.h(1157): error C2182: 'APIENTRY': this use of 'void' is not valid
... (100개 이상 반복)
```

**원인:**  
`gl.h`는 `WINGDIAPI`와 `APIENTRY` 매크로가 미리 정의된 상태를 전제로 함.  
`<GL/gl.h>`를 직접 include하면 이 매크로들이 정의되지 않은 채로 파싱되어 모든 OpenGL 함수 선언이 깨짐.  
SDL2의 `<SDL_opengl.h>`를 쓰면 SDL2가 내부적으로 WINGDIAPI/APIENTRY를 보장하고 gl.h를 안전하게 포함함.

**수정 (`GKHub/src/RmlBackend.cpp`):**
```cpp
// Before
#include <SDL.h>
#include <GL/gl.h>

// After
#include <SDL.h>
#include <SDL_opengl.h>
```

---

### Bug 2 & 3: `VersionManager.cpp` / `TemplateManager.cpp` — `minizip/mz.h` not found

**에러 메시지:**
```
error C1083: Cannot open include file: 'minizip/mz.h': No such file or directory
```

**원인:**  
사용 중인 라이브러리는 **minizip-ng** (zlib-ng 포크).  
minizip-ng의 헤더는 `minizip/` 서브폴더 구조가 없고 소스 루트에 `mz.h`가 직접 위치함.  
CMakeLists에서 `_mz_parent` 트릭으로 `minizip/mz.h` 형태를 억지로 맞추려 했지만 FetchContent 환경에서 불안정함.

**수정 (VersionManager.cpp, TemplateManager.cpp):**
```cpp
// Before
#include <minizip/mz.h>
#include <minizip/mz_strm.h>
#include <minizip/mz_zip.h>
#include <minizip/mz_zip_rw.h>

// After
#include <mz.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>
```

**수정 (GKHub/CMakeLists.txt):**
```cmake
# Before
get_filename_component(_mz_parent "${minizip_SOURCE_DIR}" DIRECTORY)
target_include_directories(GKHub PRIVATE
    ...
    ${_mz_parent}
    ${minizip_SOURCE_DIR}
)

# After
target_include_directories(GKHub PRIVATE
    ...
    ${minizip_SOURCE_DIR}
)
```

---

## 수정된 파일 요약

| 파일 | 수정 내용 |
|------|-----------|
| `GKHub/src/RmlBackend.cpp` | `<GL/gl.h>` → `<SDL_opengl.h>` |
| `GKHub/src/VersionManager.cpp` | `minizip/mz*.h` → `mz*.h` |
| `GKHub/src/TemplateManager.cpp` | `minizip/mz*.h` → `mz*.h` |
| `GKHub/CMakeLists.txt` | `_mz_parent` 제거, include path 단순화 |

---

### Bug 4: `mz_zip_rw.h` — `mz_stream_write_cb` / `mz_stream_read_cb` 타입 미정의

**에러 메시지:**
```
mz_zip_rw.h(82): error C2061: syntax error: identifier 'mz_stream_write_cb'
mz_zip_rw.h(85): error C2061: syntax error: identifier 'mz_stream_write_cb'
mz_zip_rw.h(189): error C2061: syntax error: identifier 'mz_stream_read_cb'
```

**원인:**  
minizip-ng **4.0.5**에서 `mz_zip_rw.h`가 `mz_strm.h`를 자체적으로 include하지 않아  
`mz_stream_write_cb` / `mz_stream_read_cb` 타입 선언이 없는 상태로 헤더가 파싱됨.  
MSVC는 이 경우 엄격하게 에러를 냄 (GCC/Clang은 운 좋게 통과하기도 함).

**수정 1 — minizip-ng 버전 다운그레이드 (`GKHub/CMakeLists.txt`):**
```cmake
# Before
GIT_TAG  4.0.5 GIT_SHALLOW TRUE

# After
GIT_TAG  3.0.9 GIT_SHALLOW TRUE
```
→ 3.0.9은 헤더 의존성이 올바르게 정리되어 있어 이 문제 없음.

**수정 2 — include 순서 보강 (`VersionManager.cpp`, `TemplateManager.cpp`):**
```cpp
#include <mz.h>
#include <mz_strm.h>   // 반드시 mz_zip_rw.h 보다 먼저
#include <mz_zip.h>
#include <mz_zip_rw.h>
```

---

## 전체 수정 파일 요약 (누적)

| 파일 | 수정 내용 |
|------|-----------|
| `GKHub/src/RmlBackend.cpp` | `<GL/gl.h>` → `<SDL_opengl.h>` |
| `GKHub/src/VersionManager.cpp` | minizip include 경로 + 순서 수정 |
| `GKHub/src/TemplateManager.cpp` | minizip include 경로 + 순서 수정 |
| `GKHub/CMakeLists.txt` | minizip-ng `4.0.5` → `3.0.9`, include path 정리 |
