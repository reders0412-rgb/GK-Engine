# BUGFIX: LNK1104 cannot open file 'RmlUi::RmlCore.lib'

## 에러 메시지

```
LINK : fatal error LNK1104: cannot open file 'RmlUi::RmlCore.lib'
[D:\a\GK-Engine\GK-Engine\build\hub\GKHub.vcxproj]
```

## 원인

GitHub Actions `build.yml`의 Hub configure 단계에서 vcpkg toolchain을 넘기고 있었음:

```yaml
"-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake",
"-DVCPKG_TARGET_TRIPLET=x64-windows",
```

`GKHub/CMakeLists.txt` 내에서 `BUILD_SHARED_LIBS OFF`로 static 빌드를 강제하더라도,
vcpkg toolchain이 로드되면서 CMake cache 우선순위가 뒤섞임.

결과적으로 RmlUi가 static `.lib`를 예상 경로에 생성하지 못하고,
링커가 `RmlUi::RmlCore.lib`라는 이름을 그대로 파일명으로 해석해서 실패.

### LNK1181 vs LNK1104 차이

| 에러 | 의미 |
|------|------|
| `LNK1181: cannot open input file 'RmlCore.lib'` | target 이름 오류 (namespace 누락) |
| `LNK1104: cannot open file 'RmlUi::RmlCore.lib'` | vcpkg toolchain 충돌로 .lib 경로/생성 실패 |

---

## 수정 내용

### `.github/workflows/build.yml` — Configure & Build Hub 단계

#### Before

```yaml
- name: Configure & Build Hub
  shell: pwsh
  run: |
    $cmakeArgs = @(
      "-B", "build/hub",
      "-S", "GKHub",
      "-G", "Visual Studio 17 2022",
      "-A", "x64",
      "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake",
      "-DVCPKG_TARGET_TRIPLET=x64-windows",
      "-DCMAKE_BUILD_TYPE=Release"
    )
    cmake @cmakeArgs
    cmake --build build/hub --config Release -j 4
```

#### After

```yaml
- name: Configure & Build Hub
  shell: pwsh
  run: |
    # Hub는 FetchContent로 모든 의존성을 직접 빌드하므로
    # vcpkg toolchain 불필요 — 넘기면 BUILD_SHARED_LIBS 등 캐시가 충돌함
    $cmakeArgs = @(
      "-B", "build/hub",
      "-S", "GKHub",
      "-G", "Visual Studio 17 2022",
      "-A", "x64",
      "-DCMAKE_BUILD_TYPE=Release",
      "-DBUILD_SHARED_LIBS=OFF"   # RmlUi static .lib 보장
    )
    cmake @cmakeArgs
    cmake --build build/hub --config Release -j 4
```

**핵심 변경:**
- `CMAKE_TOOLCHAIN_FILE` (vcpkg) 제거 → Hub는 FetchContent 전용이라 vcpkg 불필요
- `VCPKG_TARGET_TRIPLET` 제거 → 동일 이유
- `-DBUILD_SHARED_LIBS=OFF` CLI 인자로 명시 → CMakeLists.txt 내 설정이 toolchain에 의해 덮이는 것 방지

---

## 요약

```
LNK1104: cannot open file 'RmlUi::RmlCore.lib'
→ Hub configure에서 vcpkg toolchain 제거
→ -DBUILD_SHARED_LIBS=OFF 를 cmake CLI 인자로 명시
```
