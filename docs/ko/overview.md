# AIVERSE — 상세 개요

엔진의 모든 부분에 대한 참고 문서입니다: 구조, 빌드, 형식, 서브시스템, 플랫폼, 검증 상태.
짧은 소개(엔진의 목적과 해결하는 문제)는 [README](../../README.ko.md)에 있습니다. 엔진 자체를 수정하는 에이전트를 위한 규칙은
[AGENTS.md](../../AGENTS.md)에 있습니다.

## 구조

```
engine/
  core/      Fixed (Q47.16), Vec3, Random (PCG32), Hash, ECS
  data/      JSON without floats (numbers go straight to fixed-point)
  sim/       deterministic simulation, commands, replays
  net/authority/ authoritative network sessions for FPS
  world/     spatial structures and chunked world streaming
  gfx/       RHI + Metal and GL (ES3/WebGL2/GL3.3) backends, render to texture
  render/    3D rendering: lighting, shadows (shadow map + PCF), fog, tone mapping; Dear ImGui over the RHI;
             glTF/OBJ models, PBR materials, skeletal and procedural animation
  platform/  window/input/loop: macos, ios, android, web, windows, linux
  assets/    shared engine assets (Noto Sans font with Cyrillic, OFL)
modules/rts/ generic RTS layer: types from data, orders, A*, combat, economy, construction,
             production, garrisons (Mushroom Wars), roads, fog of war, AI
             view/ — draws any type from the "view" block in rules.json
modules/fps/ FPS module (in development): movement, combat and match state
apps/
  fps/       standalone FPS client and server
  client/    the game (menu, match with bots, HUD, minimap, mouse and touch)
  editor/    map editor (terrain, objects, roads, players, validation, bot test, Build menu)
  headless/  bot matches and replay checks without graphics
  modelview/ viewer for models, clips, materials and every type of a game
tools/mcp/      MCP server through which AI agents work with the editor
tools/aiversebuild/  packager: exe / app / Xcode project / Gradle project / web
templates/
  skirmish/  "Iron and Gold" — a Warcraft/Red Alert-style sample
  mushrooms/ "Mushroom Clash" — a Mushroom Wars-style sample
  showcase/  Iron and Gold rules with imported models (glTF, OBJ, animations)
platform/    packaging templates (Info.plist, Gradle project, HTML shell)
```

## 빠른 시작 (macOS)

```bash
cmake -S . -B build/mac -DCMAKE_BUILD_TYPE=Release      # requires Xcode and CMake
cmake --build build/mac -j 8
open build/mac/AiverseEditor.app                              # editor (project templates/skirmish)
open build/mac/AiverseGame.app                                # game
./build/mac/aiverse_tests
```

에디터에서 다른 게임 열기: `AiverseEditor.app/Contents/MacOS/AiverseEditor --project templates/mushrooms`.
게임을 다른 데이터 세트로 실행하려면 `-DAIVERSE_CONTENT_DIR=.../templates/mushrooms`를 지정합니다.

### 여러 에디터를 동시에 사용하기

각 에디터 프로세스는 `--project <폴더>`로 서로 다른 프로젝트를 열 수 있습니다. `--listen` 없이 실행하면 첫 인스턴스가 MCP 포트
`47321`을 사용하고, 사용 중이면 OS가 빈 포트를 할당합니다. 실제 포트는 상태 표시줄에 표시되며 **Agent** 창이 연결 설정
(`aiversemcp --port <포트>`)에 기록합니다. 자동으로 할당된 포트는 에디터를 다시 시작하면 바뀔 수 있습니다. `--listen <포트>`로 포트를
고정하고(충돌하면 MCP가 시작되지 않음), `--listen 0`으로 MCP를 끕니다. 이미 연결된 MCP 클라이언트가 자동으로 창을 바꾸지는 않습니다.

## 플랫폼별 빌드 — aiversebuild

```bash
./build/mac/aiversebuild --project templates/skirmish --target macos     # Builds/macos/<Name>.app (ad-hoc signed)
./build/mac/aiversebuild --project templates/skirmish --target ios       # Builds/ios/xcode/AIVERSE.xcodeproj + simulator build
./build/mac/aiversebuild --project templates/skirmish --target android   # Builds/android/<Name>-release.apk when SDK/NDK and gradle are present
./build/mac/aiversebuild --project templates/skirmish --target web       # Builds/web (requires emsdk)
./build/mac/aiversebuild --project templates/skirmish --target windows   # .exe via mingw-w64 (brew install mingw-w64) or sources + build.bat
./build/mac/aiversebuild --project templates/skirmish --target all
```

결과물은 `<프로젝트>/Builds/<플랫폼>/`에 놓이며, 완성된 빌드와 크기 보고서만 들어갑니다. 중간 산출물(CMake 트리, 팩, 재인코딩된 오디오,
Gradle 프로젝트, xcodebuild 데이터, 에디터의 크기 추정)은 숨김 폴더 `Builds/.temp/`에 있으며 언제든 지울 수 있습니다. `--generate-only`를
쓰면 Android Studio용 Gradle 프로젝트가 `Builds/android/<Name>`에 놓입니다. 예전에 보이던 폴더 `_build`, `_pack`, `_audio`,
`_estimate`는 패키저가 자동으로 정리합니다.

에디터에도 같은 기능이 있습니다: **빌드** 메뉴. 생성된 iOS / Android 프로젝트는 독립적이어서(엔진 소스와 콘텐츠가 복사됨) 그대로 다른
사람에게 넘길 수 있습니다. iOS 기기에서 실행하려면 Xcode에서 프로젝트를 열고 Signing에서 팀을 고릅니다(또는 `--team <TeamID>`, 혹은
game.json의 `"appleTeamId"`).

식별자, 이름, 버전, 화면 방향, 아이콘(`icon.png`, 1024×1024, 없으면 생성)은 `game.json`에 있습니다.

### 빌드 도구: 확인과 자동 설치

빌드 전에 `aiversebuild`는 플랫폼에 필요한 것을 확인하며 `PATH` 밖도 찾아봅니다(Finder에서 실행한 에디터는
`/usr/bin:/bin:/usr/sbin:/sbin`만 보기 때문): Homebrew(`/opt/homebrew/bin`, `/usr/local/bin`), `~/.local/bin`,
`~/Library/Python/*/bin`, `~/emsdk`, `$EMSDK`, `ANDROID_HOME` / `ANDROID_SDK_ROOT`, `~/Library/Android/sdk`, `~/Android/Sdk`,
`%LOCALAPPDATA%\Android\Sdk`, `JAVA_HOME`, `/usr/libexec/java_home`, Visual Studio(vswhere), 그리고 전용 폴더 `~/.aiverse/toolchains`.
찾은 폴더는 자식 프로세스의 `PATH`에 추가됩니다.

| 플랫폼 | 필요한 것 | 자동 설치되는 것 |
|---|---|---|
| web | CMake, Python 3, Emscripten(emsdk), Ninja는 선택 | emsdk(다운로드 약 400 MB, 디스크 약 1.5 GB), CMake + Ninja(약 50 MB) |
| android | JDK 17–23, Android SDK(platform 35, build-tools, platform-tools), NDK, SDK의 CMake, Gradle | 전부: Temurin JDK 17, command line tools + `sdkmanager`(라이선스 동의 처리), NDK 약 700 MB, Gradle 8.10.2 |
| macos | Xcode Command Line Tools, CMake | CLT는 Apple 설치 프로그램(`xcode-select --install`), CMake |
| ios | Xcode(App Store), CMake | CMake |
| windows(Windows에서) | Visual Studio 2022 / Build Tools(C++), CMake | CMake, VS는 `winget` 명령을 안내 |
| linux | C++ 컴파일러, CMake | CMake |

```bash
aiversebuild --check-toolchain web            # what is present and what is missing (--json for the editor and MCP)
aiversebuild --install-toolchain web          # install what is missing (or one id: emsdk, cmake, jdk, android-ndk, gradle, ...)
aiversebuild --project ... --target web --install-missing   # install and build right away
```

`--install-missing` 없이 도구가 부족하면 빌드는 종료 코드 3과 `AIVERSE_MISSING_TOOLCHAIN web: emsdk` 줄로 멈춥니다(에디터가 이를
인식합니다). 에디터의 **빌드** 창은 선택한 플랫폼에 대해 찾은 것(경로, 버전)과 부족한 것을 보여 주며, **자동 설치** 버튼과 "부족한 것을
자동으로 설치" 체크박스가 있습니다. 진행 상황은 **콘솔**에 표시됩니다(텍스트 선택과 복사 가능, "로그 폴더 열기"로 `<엔진>/logs`를 엽니다).
에이전트는 MCP로 같은 일을 합니다: `toolchain_check`, `toolchain_install`, `build_start {"installMissing": true}`.

모든 것은 `~/.aiverse/toolchains`에 설치됩니다(Windows: `%USERPROFILE%\.aiverse\toolchains`, 다른 폴더는 변수 `AIVERSE_TOOLCHAINS_DIR`로
지정). `sudo`도 시스템 변경도 필요 없습니다: `emsdk/`, `py/`(cmake와 ninja가 든 venv), `cmake/`, `ninja/`, `jdk-17/`, `android-sdk/`,
`gradle-8.10.2/`. 다시 실행해도 안전합니다(이미 된 것은 건너뜀). 모두 지우려면 이 폴더를 삭제합니다. 예외: Command Line Tools는 Apple
설치 프로그램이 설치하며, 사용자의 Android SDK가 발견되면 `sdkmanager`가 거기에 부족한 패키지를 설치합니다.

### 리소스 팩(.rpak)과 빌드 크기

게임 콘텐츠(게임 폴더 + `engine/assets`)는 빌드에 하나의 파일 `content/content.rpak`로 들어갑니다. 각 파일은 개별적으로 zstd로 압축되고,
동일한 파일은 한 번만 저장되며, 이미 압축된 형식(png, ogg, mp3 등)은 그대로 들어갑니다. 엔진은 `content.rpak`를 자동으로 마운트합니다.
팩 안의 파일이 폴더 안의 파일보다 우선하므로 팩 없이(개발 중) 기존처럼 동작합니다. `platform::mountPack("patch.rpak")`으로 그 위에 팩을
겹칠 수 있습니다(패치, DLC). 각 빌드는 크기 보고서를 출력하고 `dist/<플랫폼>/size-report.txt`에 씁니다.

```bash
aiversebuild ... --no-pack          # ship folders as is (debugging)
aiversebuild ... --pack-level 9     # zstd level (default 19 for Release, 3 for Debug)

assetpipe pack -o content.rpak templates/skirmish engine/assets=engine   # build a pack by hand
assetpipe list content.rpak       # files, sizes, compression
assetpipe verify content.rpak     # unpack everything and check hashes
assetpipe extract content.rpak out/
assetpipe diff old.rpak new.rpak  # what changed between versions (for patches)
```

빌드에 필요 없는 것은 게임 폴더의 `.rpakignore`에 적습니다(마스크 `*`, `?`, `폴더/`는 디렉터리만). 숨김 파일(`.git`, `.DS_Store`),
리플레이 `*.rpl`, 중첩된 `*.rpak`는 항상 제외됩니다. 팩 생성은 재현 가능합니다: 같은 내용에서는 바이트 단위로 같은 파일이 나오고, 팩의
"콘텐츠 해시"가 데이터 세트를 식별합니다.

### 빌드 분할과 콘텐츠 다운로드

빌드는 선택적으로 분할할 수 있습니다. 앱에는 시작에 필요한 것만 담은 기본 팩을 넣고, 나머지는 지정한 크기의 조각(`content.001.rpkc`, …)으로
만들어 게임이 원하는 배포처에서 내려받습니다. 모든 것은 게임 제작자가 `game.json`(공통 설정과 플랫폼별 설정)이나 `aiversebuild` 플래그로
정합니다:

```json
"packaging": {
  "split": true,                 // whether to split
  "chunkSize": "8MB",            // size of one part
  "baseSize": "40MB",            // limit for what ships inside the app
  "totalSize": "1GB",            // limit for all content
  "fillBase": false,             // true: fill the base up to baseSize (less to download)
  "base": ["maps/intro.json"],   // what must be in the base (besides the default: game.json, rules, lang, ui, atlases, first map)
  "order": ["maps/scena00*"],    // download order
  "sources": ["https://cdn.example.com/mygame/{hash}/", "file:///Volumes/DLC/"],   // mirrors in order
  "autoDownload": true,          // download everything in the background right after start
  "targets": {"web": {"baseSize": "15MB"}, "windows": {"split": false}}
}
```

```bash
aiversebuild --project templates/x --target web --split --chunk-size 4MB --base-size 15MB   # flags override game.json
```

조각은 빌드 옆(`dist/<플랫폼>/chunks/`)에 놓이며 `sources`의 주소로 공개합니다(`{hash}`는 빌드의 콘텐츠 해시라서 버전이 섞이지 않습니다).
게임에서는 아직 내려받지 않은 조각에 있는 파일이 자동으로 요청됩니다(데스크톱과 모바일에서는 `readContentFile`이 기다리고, Web에서는 false를
반환). 진행 상황은 `platform::contentProgress()`, 명시적 요청은 `platform::requestContent(paths)`입니다. 각 조각은 SHA-256으로 검증되며(미러에서
변조된 조각은 거부하고 다음 미러에서 받음), 캐시되어 다음 실행 때 다시 내려받지 않습니다. 프로토콜: https/http(Linux는 libcurl, WinHTTP,
NSURLSession, Android는 HttpURLConnection, Web은 fetch), `file://`와 폴더. 게임은 자체 스킴(Steam, Play Asset Delivery, P2P)도 등록할 수
있습니다. `assetpipe pack --chunk-size 8MB ...`로 같은 작업을 직접 할 수 있고, `assetpipe verify`는 조각도 검증합니다.

### 외부 아카이브 가져오기(언패커)

엔진이 모르는 형식(zip, tar, dmg, 다른 게임의 아카이브)은 엔진을 다시 빌드하지 않고 언패커 플러그인으로 추가할 수 있습니다: `unpacker.json`과
아무 언어로 작성한 프로그램이 든 폴더입니다(프로토콜: [`tools/unpackers/PROTOCOL.md`](../../tools/unpackers/PROTOCOL.md)). 언패커는 빌드
도구 안에서만 실행됩니다. 외부 아카이브는 `.rpak` 안의 파일이 되고, 게임은 계속 `.rpak`만 읽습니다.

기본 지원: zip(과 `.oramap`), tar, dmg, Warcraft I/II 아카이브(`.WAR/.SUD/.INS`), Westwood `.PAK`(Dune II, Lands of Lore, Kyrandia),
Bitmap Brothers의 Z(`.PAC`, `.RSC`).

```bash
assetpipe formats                                   # which formats are known
assetpipe probe ~/Downloads/Warcraft2/war2/DATA/*   # what these files are
assetpipe list MAINDAT.WAR                          # what is inside
assetpipe import -o war2.rpak MAINDAT.WAR=imported/war2 SFXDAT.SUD=imported/war2/sfx
assetpipe unpackers new tools/unpackers myformat    # scaffold for a new unpacker
assetpipe unpackers check                           # check all plugins
```

모든 명령에는 `--json`이 있으며 MCP 서버가 이를 통해 호출합니다. `game.json`에 `"imports"`를 나열하면 `aiversebuild`가 빌드 시 팩에
풀어 넣습니다(`"optional": true` — 이 컴퓨터에 파일이 없으면 그것 없이 빌드를 계속). 가져온 서드파티 콘텐츠가 든 팩에는 "external"
플래그가 붙습니다. 그런 빌드는 개인 용도로만 쓸 수 있으며 커밋하거나 배포할 수 없습니다.

### 게임 가져오기(임포터)

임포터는 사용자가 가진 고전 게임 사본을 엔진용 게임 폴더로 변환합니다. 현재 Dune II를 지원합니다:

```bash
python3 tools/importers/dune2/import.py --source "~/Downloads/Dune II" --out ~/AiverseGames/dune2
aiversebuild --project ~/AiverseGames/dune2 --target macos
```

결과는 엔진 형식의 2D 게임입니다(`docs/game-format.md`, "2D games"): 위에서 내려다보는 카메라, 타일 레이어가 있는 캠페인 미션 66개
(ICON.ICN에서 만든 `tilesets/dune.json`: 모래, 바위, 사구, 산, 자동 타일이 적용된 스파이스), SHP에서 만든 유닛 스프라이트(미러 포함 5방향,
전차 포탑은 차체 위에 겹침), ICN 타일로 만든 건물(건설 중, 폐허, 애니메이션), 마젠타 계열 색조로 만든 진영 색. 그리고 `rules.json`(엔진
블록으로 구성한 Dune II 밸런스), CPS 이미지와 WSA 애니메이션은 PNG로, VOC 음성 → WAV + `audio.json`, 영어와 러시아어 이름. 지형은
근사치입니다: 원래의 지형 생성은 쓰지 않으며, 기지는 바위 위에, 스파이스는 `Field=`에서, 미션 목표는 트리거로 구현합니다. 보병,
오니솝터, 캐리올은 아직 3D 도형으로 그려집니다. 폴더에는 `"external": true`가 붙어 있어 커밋하거나 배포할 수 없습니다.

## 프로젝트 파일 .rtsproj

프로젝트는 디스크의 아무 곳에나 있는 폴더입니다(`game.json`, `rules.json`, `maps/` 등). 그 루트에 프로젝트 파일 **`<이름>.rtsproj`**가 있어
Unreal의 `.uproject`나 `project.godot`처럼 더블 클릭으로 열 수 있습니다. `game.json`은 계속 게임 매니페스트이며(클라이언트, aiversebuild,
팩이 읽음), `.rtsproj`는 폴더를 프로젝트로 표시하고 에디터 정보를 저장할 뿐입니다:

```json
{"format": 1, "name": "My Strategy", "engine": "0.2.0", "manifest": "game.json", "created": "2026-10-28"}
```

- 프로젝트와 함께 만들어집니다(허브의 "새 프로젝트", MCP `create_project`). 이것이 없는 예전 프로젝트는 에디터에서 처음 열 때 만들어집니다.
  `templates/*/`의 템플릿에는 이미 있습니다(새 프로젝트로 복사되지 않으며, 새 프로젝트는 자기 것을 가집니다).
- 에디터가 프로젝트 경로를 받는 곳이면 어디서나 `.rtsproj`와 폴더 모두 쓸 수 있습니다: `aiverse_editor "My Strategy.rtsproj"`,
  `--project`, 허브의 "열기…"(`.rtsproj`와 프로젝트 폴더를 표시), 최근 항목, MCP `open_game`, `aiversebuild --project x.rtsproj`.
  `.rtsproj`는 빌드에 포함되지 않습니다.
- **더블 클릭.** 시작할 때 연결이 없거나 다른 에디터 인스턴스에 연결되어 있으면, 에디터는 `.rtsproj`를 자신과 연결합니다(사용자 단위,
  관리자 권한 불필요). 사용자가 고른 다른 프로그램은 덮어쓰지 않습니다. 수동으로는 "프로젝트 → 에디터를 .rtsproj 기본 앱으로". 상태는
  허브의 왼쪽 아래에 표시됩니다. 끄려면 `AIVERSE_FILE_ASSOC=off`.
  - **macOS:** 타입 `com.aiverse.project`는 `AiverseEditor.app`의 Info.plist에 선언되어 있습니다(`platform/macos/EditorInfo.plist.in`,
    문서 아이콘 `rtsproj.icns`는 `aiverse_projicon`이 에디터 로고 `apps/editor/branding/icon_1024.png`로 그림). 에디터는 번들을
    LaunchServices에 등록하고(`lsregister -f`) 기본 핸들러가 됩니다. 더블 클릭이나 `open -a AiverseEditor x.rtsproj`로 실행 중인 에디터에
    프로젝트가 열립니다(맵이 저장되지 않았으면 저장할지 묻습니다).
  - **Windows:** `HKCU\Software\Classes\.rtsproj` → `AIVERSE.Project`(아이콘 `%USERPROFILE%\.aiverse\icons\rtsproj.ico`, 명령
    `"<aiverse_editor.exe>" "%1"`). Windows에서 이미 `.rtsproj`에 다른 프로그램이 선택되어 있으면 허브가 "연결 프로그램 → 항상"을 안내합니다.
  - **Linux:** 타입 `application/x-aiverse-project`(`~/.local/share/mime/packages/aiverse-project.xml`),
    `~/.local/share/applications/aiverse-editor.desktop`(`Exec=<에디터> %f`), 아이콘은 `~/.local/share/icons/hicolor`, 기본 앱은
    `~/.config/mimeapps.list`. `update-mime-database`, `update-desktop-database`, `xdg-mime`이 설치되어 있으면 호출합니다.

## 게임 만들기(마법사)

프로젝트 창의 **게임 만들기** 버튼(및 **프로젝트 → 게임 만들기…**): 아트, 음악, 텍스트가 든 폴더나 링크를 주고 아이디어를 적으면(아무것도
적지 않아도 마법사가 직접 고릅니다) 완성된 `.rtsproj` 프로젝트가 만들어집니다. 알맞은 템플릿 기반의 게임, 단계별 튜토리얼(강제)과 전투 중
힌트(은근한), 별과 보상이 있는 레벨, 메타(메뉴, 업그레이드, 상점, 일일 보상, 프로필, 설정), 필요하면 광고와 결제, 언어, 아트로 만든 아이콘과
스플래시 화면. 마법사는 봇으로 튜토리얼을 직접 통과하고 레벨을 검증합니다. 무엇이 할당되었고 무엇을 확인해야 하는지는 프로젝트의
`WIZARD.md`에 있습니다. 에이전트용: MCP `game_make_options` / `game_make_plan` / `game_make`. 자세히: [game-wizard.md](game-wizard.md).

## 게임을 기술하는 방법

객체 타입은 선택적 블록의 집합입니다. 설정한 블록만큼 객체는 그 능력을 가집니다:

| 블록 | 의미 |
|---|---|
| `health` | 체력, 방어력, 방어 타입, 재생 |
| `move` | 속도, 레이어(`ground`/`air`/`naval`), 반경 |
| `structure` | 그리드 위에서 `footprint` 크기를 가진 건물 |
| `weapons` | 사거리, 피해, 피해 타입, 쿨다운, 투사체, 범위 피해, 공격 가능한 레이어 |
| `cost` | 자원, 시간, 인구 |
| `produces` / `builds` | 훈련하는 것 / 건설하는 것 |
| `gather` / `resourceNode` / `dropoff` | 자원 채집(Warcraft/StarCraft) |
| `income` / `storage` + 자원 `"kind": "flow"` | 흐름형 경제(Total Annihilation) |
| `supply` | 인구 제공 |
| `requires` | 기술 트리 |
| `garrison` + `entersGarrison` | 주둔 건물, 병력의 일정 비율 보내기, 점령(Mushroom Wars) |
| `view` | 외관: `shape`, `scale`, `color` |

전역: `resources`, `damageTable`(피해 타입 × 방어 타입), `supplyCap`, `victory`.
맵: 크기, 지형(`rows`: `.` 땅, `~` 물, `#` 바위, `,` 건설 불가, `=` 여울, `heights`), 플레이어와 팀, 시작 자원, 객체, 도로, `roadsOnly`.

## 현지화

게임은 `rules.json`을 바꾸지 않고 번역할 수 있습니다. 데이터 안의 이름은 게임의 원래 언어로 된 텍스트이고, 번역은 `<프로젝트>/lang/<언어>.json`에
`type.footman`, `resource.gold`, `map.valley.title` 같은 키로 둡니다. 메뉴와 HUD 문자열은 `engine/assets/lang/`에 있으며(현재 영어와
러시아어), 게임은 어느 것이든 덮어쓸 수 있습니다.

```jsonc
// game.json
"sourceLanguage": "en",
"languages": ["en", "ru"]
```

```bash
./build/mac/aiverse_i18n extract --project templates/skirmish --lang de --write   # lang/de.json skeleton, new strings marked // TODO
./build/mac/aiverse_i18n check   --project templates/skirmish                     # placeholders, plural forms, untranslated strings
```

언어는 다음 순서로 정해집니다: 메뉴에서 플레이어가 고른 것(기억됨) → 시스템 언어 → 게임의 원래 언어. 확인용: `--lang ru`, `AIVERSE_LANG=ru`,
의사 언어 `--lang qps`(모든 문자열이 변형되고 길어져서 번역 누락과 넘침이 바로 보입니다). 복수형은 CLDR을 따릅니다("1 second / 2 seconds /
5 seconds"). 자세히: `engine/i18n/README.md`.

## 게임 UI(RmlUi, HTML/CSS)

클라이언트의 메뉴, HUD, 명령 카드, 미니맵, 일시 정지는 코드가 아니라 RmlUi 문서(마크업 `.rml`, 스타일 `.rcss`)입니다. 엔진은 자체 RHI(GL/Metal)로
이를 그리고, 글꼴은 stb_truetype(라틴 문자 + 키릴 문자)로 그립니다.

- 기본 문서: `engine/assets/ui/`(`menu.rml`, `hud.rml`, `theme.rcss`).
- 게임은 자기 폴더에 `ui/<같은 이름>`을 두어 어떤 파일이든 덮어쓸 수 있습니다. 예를 들어 `ui/theme.rcss`만 두면 기본 마크업에 자체 색
  구성을 씁니다. 게임 글꼴은 `fonts/ui.ttf`.
- 데이터는 `game` 모델에서 오고(필드 목록은 `hud.rml`의 주석에 있음), 버튼 누름은 시뮬레이션 명령이 됩니다(`apps/client/ClientUi.cpp`).
- 크기는 `dp` 단위이며 UI는 화면 밀도에 맞게 확대/축소됩니다. 터치 기기에서는 루트에 `touch` 클래스가 붙습니다(버튼이 커짐).
- 클라이언트에서 F8을 누르면 RmlUi 디버거(요소 트리, 스타일, 로그)가 열립니다.
- 게임 스킨은 `ui/skin.rcss`(각 문서에서 `theme.rcss` 다음에 로드: 색, 테두리, 버튼, 레이아웃)와 제목 글꼴 `fonts/title.ttf`(패밀리
  `title`)입니다. `templates/`의 각 프로토타입에는 자체 스킨이 있습니다. 세로 레이아웃(`@media (orientation: portrait)`)은 엔진 테마에
  있으며 PC에서는 `--size 540x960`으로 확인할 수 있습니다.
- 문서 안의 텍스트는 키 `@menu.play`로 씁니다. 번역은 문서를 불러올 때 `engine/assets/lang`(과 게임의 `lang/`)에서 가져오며, 메뉴에서
  언어를 바꾸면 문서가 다시 로드됩니다.
- 화면 키보드(`engine/ui/UiKeyboard.*`): RmlUi로 만들어졌으며 터치 기기에서 입력 칸에 포커스하면 열립니다(PC에서는 `AIVERSE_OSK=1`).
  배열(en, ru, 기호, Shift, 두 번 탭하면 Caps, 언어 전환)은 `keyboard.json`, 외관은 `keyboard.rml` / `keyboard.rcss`에 있으며, 게임은
  자기 `ui/`에서 어느 것이든 바꿀 수 있습니다. 커서가 화면 아래쪽에 있으면 키보드는 위에 열립니다.
- 멀티플레이(`lobby.rml`, `apps/client/Lobby.cpp`): 서버 주소와 이름, 서버 대기열을 통한 빠른 대전, 방 목록(참가 / 관전), 내 방(슬롯, 봇,
  준비, 시작, 채팅). 대전은 `NetClient`가 진행합니다: `attach(sim)`, `sim.advance(dt)` 대신 `advance(dt)`, 명령은 슬롯
  `match().localSlot`에서(0은 관전자). 서버가 이 클라이언트에 할당한 봇도 여기서 계산됩니다.

## 결정론

시뮬레이션은 모든 플랫폼에서 비트 단위로 동일합니다: 고정 소수점만 사용, 자체 난수 생성기, 정해진 순서의 명령, 리플레이 안의 체크섬. 검증됨:
Linux x86-64(GCC, Clang, MinGW), Linux ARM64(GCC), macOS ARM64(AppleClang) — 대전 체크섬이 일치.

## 그래픽과 모바일

한 번의 포워드 패스: 햇빛(Lambert + 스페큘러), 맵의 보이는 부분에 맞추고 텍셀에 스냅한 섀도 맵, 하드웨어 PCF(1/4/9 샘플), 반구 앰비언트,
거리 안개, ACES. 그림자 품질은 GPU에 따라 정해지며(구형 Mali/Adreno/PowerVR에서는 낮음) 일시 정지 메뉴에서 바꿀 수 있습니다.

## 멀티플레이

`engine/net`은 게임 코드 없이 엔진 위의 모든 게임에 멀티플레이를 제공합니다. 네트워크로는 명령만 오갑니다. 시뮬레이션이 결정론적이므로 모두의
상태가 같아집니다.

- **전송:** 자체 신뢰성 계층을 갖춘 UDP(iOS, Android, PC)와 WebSocket(HTML5, 네이티브에서 UDP가 막혔을 때의 대안). 주소:
  `host[:27015]`, `udp://…`, `ws://host:27016`.
- **롤백이 있는 락스텝.** 서버는 각 tick의 입력을 모든 플레이어의 명령이 도착했을 때 또는 마감(`lagToleranceMs`)이 지났을 때 확정합니다.
  늦은 명령은 다음 tick으로 넘어가므로 지연이 큰 플레이어가 다른 사람을 멈추지 않습니다. 클라이언트는 확정을 기다리지 않고 자기 명령으로
  먼저 진행하며, 어긋나면 스냅숏을 복원하고 다시 계산합니다(스냅숏은 약 10 µs). `rollbackWindow: 0`은 저사양 기기용 고전적 락스텝입니다.
- **`aiverse_server`**는 게임 콘텐츠가 필요 없습니다: 방(슬롯, 봇, 관전자, 준비, 데이터 버전 확인), `server.json`의 대기열을 통한 자동
  매치메이킹(범위가 넓어지는 Elo 레이팅, 공통 맵, 팀 균형, 봇으로 채우기), 체크섬 대조(비동기 감지), 리플레이(`aiverse_headless --replay`로
  재생), 재접속, 지연 관전.
- **퇴장 = 항복.** 나간 플레이어(또는 `reconnectGraceSec` 안에 돌아오지 않은 플레이어)는 공통 입력 스트림의 명령으로 항복합니다. 결과는
  시뮬레이션이 모두에게 똑같이 정하며 리플레이에도 남습니다. 레이팅이 차감됩니다.
- **서버 판정.** 아무도 결과를 확정하지 않으면(모두 퇴장, 결과 불일치) 서버가 백그라운드 스레드에서 자체 게임 데이터(`server.json`의
  `"games"`)로 대전을 재생해 승자를 정합니다.
- **재접속:** 네트워크가 끊기면 클라이언트는 자동으로 재접속하고(`NetClient(address, …)` 또는 `setReconnect`) 시뮬레이션은 계속 돌아갑니다.
  서버는 마지막으로 확정된 tick 이후의 입력만 보냅니다. 게임을 다시 시작한 경우에는 클라이언트가 전체 기록을 받아 대전을 처음부터 다시
  계산합니다.
- **게임에서:** `NetClient`(`engine/net/NetClient.h`)가 로비와 대전을 맡습니다. 게임은 `match()`로 `Simulation`을 만들고
  `attach(sim)`을 호출하며, `sim.advance(dt)` 대신 `advance(dt)`를 호출합니다. UI는 계속 `sim.submit()`으로 명령을 보냅니다.
- **텍스트:** 서버는 완성된 문장을 보내지 않고 인수가 있는 현지화 키(`NetText`)만 보냅니다. 같은 대전의 플레이어가 서로 다른 언어를 쓸 수
  있기 때문입니다. 클라이언트는 `engine/net/NetLocalize.h`의 `net::translate(loc, event.text)`와 `net::displayName(...)`으로 번역하며,
  문자열은 `engine/assets/lang/*.json`의 `net.*`에 있습니다. `server.json`의 대기열 이름도 키입니다(`"@net.queue.1v1"`).

```bash
./build/mac/aiverse_server --config apps/server/server.json            # UDP 27015, WebSocket 27016
./build/mac/aiverse_netbot --server 127.0.0.1 --project templates/mushrooms --queue 1v1   # a bot in the queue
./build/mac/aiverse_netbot --server ws://127.0.0.1:27016 --project templates/mushrooms --queue 1v1
./build/mac/aiverse_net_tests                                          # network tests
docker build -f apps/server/Dockerfile -t aiverse-server .             # server in a container
```

`wss://`(HTTPS로 제공되는 게임 페이지)에는 서버 앞에 TLS 프록시(nginx, Caddy)를 두세요.

## 사운드

`engine/audio`는 믹서, 데이터 기반 사운드 뱅크, 절차적 합성, 음악입니다. 출력은 [miniaudio](../../third_party/miniaudio)(CoreAudio,
AAudio/OpenSL ES, Web Audio, WASAPI, ALSA/PulseAudio)를 통합니다. `modules/rts/audio`는 시뮬레이션 이벤트를 소리로 바꾸며 시뮬레이션에는
아무것도 쓰지 않습니다. 장치가 없으면 게임은 소리 없이 돌아가고, `audio.json`이 없으면 그냥 소리 없는 게임입니다.

게임의 사운드는 `game.json` 옆의 `audio.json` 파일입니다(다른 이름은 game.json의 `"audio"`). 전체 형식은 `docs/game-format.md`의
audio.json 절에 있습니다. 개요:

```jsonc
{
  "volume": { "sfx": 0.9, "music": 0.6, "voice": 1, "ui": 0.7, "ambient": 0.7 },
  "sounds": {
    "sword": { "files": ["sfx/sword1.ogg", "sfx/sword2.ogg"], "pitch": [0.9, 1.1], "maxInstances": 3, "cooldown": 0.06 },
    "alarm": { "file": "voice/under_attack.ogg", "bus": "voice", "priority": 10 },
    "blip":  { "synth": { "wave": "square", "freq": [880, 440], "duration": 0.08 }, "bus": "ui" }
  },
  "music":     { "menu": "music/menu.ogg", "match": { "files": ["music/a.ogg", "music/b.ogg"], "shuffle": true } },
  "events":    { "died": "death", "underAttack": "alarm", "notEnough.gold": "need_gold" },
  "weapons":   { "bow": { "attack": "bow_release", "hit": "arrow_hit" } },
  "abilities": { "heal": "heal_cast" },
  "types":     { "footman": { "select": "footman_what", "move": "footman_yes", "died": "death_human" } }
}
```

| 항목 | 의미 |
|---|---|
| 형식 | WAV, OGG Vorbis, MP3, FLAC. `"synth"`는 매개변수로 만드는 소리(`sine/square/saw/triangle/noise`, 주파수 스윕, 엔벨로프, 비브라토, 필터, 레이어) |
| 버스 | `sfx`와 `ambient`는 위치 기반(3D: 감쇠, 패닝, 높이). `voice`, `ui`, `music`은 위치 없음 |
| 거리 | `spatial`: 감쇠 모델, 범위, "공기"(멀수록 고음이 사라짐), 거리에 따라 늘어나는 잔향, 음속. 소리마다 범위와 `far`(크로스페이드되는 원거리 버전)를 가질 수 있음 |
| `maxInstances`, `cooldown`, `priority` | 대규모 전투에서의 솎아내기: 궁수 40명이 같은 사격음을 40번 내지 않음 |
| `groups` | 여러 소리가 공유하는 제한(근접전 전체, 폭발 전체, 버스 전체): `max`, `cooldown`, 무엇을 멈출지 — `farthest`(기본: 먼 전투가 화면 안의 소리를 끊지 않음), `oldest`, `quietest`, `none` |
| 이벤트(큐) | `attack hit miss died spawned trainStarted ready buildPlaced buildDone gathered captured lost playerDefeated victory defeat notEnough notEnoughSupply noRoom researched levelUp ability readyToPlace sold regionClaimed wave lowPower upgraded message underAttack baseUnderAttack select move attackOrder buildOrder click error` |
| 소리 검색 | 타입 → ID별 무기/능력 → `events` |
| 음악 | 재생 목록 `menu`, `match`, `battle`, `victory`, `defeat`(와 `match.<맵>`). 크로스페이드, 인트로 + 이음매 없는 루프, 전투 강도에 따른 레이어, 박자/마디 단위 전환, 스팅어, 음성 중 음악 더킹 |
| 빌드 시 압축 | `aiversebuild`는 플랫폼별로 오디오를 재인코딩: 효과음은 ADPCM, 음악은 Vorbis, 샘플 레이트 낮춤, 모노 변환. `audio.json` → `build`의 `desktop`/`mobile`/`web` 프로필, `--audio off`면 재인코딩 없음 |

전투 소리는 전장의 안개가 없는 곳에서만 들립니다. "공격받는 중"은 간격을 두고, 카메라가 다른 곳을 보고 있을 때만 울립니다. 음량(전체와 음악)은
일시 정지 메뉴에 있으며 `userDataDir/audio.cfg`에 저장됩니다. 스마트폰에서는 앱이 백그라운드로 가면 소리가 일시 정지됩니다.

사운드 카드 없이 디버깅: `AIVERSE_AUDIO=null`(하드웨어 없이 실제 오디오 스레드), `AIVERSE_AUDIO=off`,
`AIVERSE_AUDIO_CAPTURE=/tmp/mix.wav`(재생된 것을 모두 WAV로 녹음).

## 모델, 머티리얼, 애니메이션

타입은 `"view"` 블록에 따라 그려집니다: 내장 도형(`"shape"`) 또는 모델 파일(`"model"`) — **glTF 2.0**(`.glb`, `.gltf` + `.bin` + 이미지)과
**OBJ + MTL**. 모두 하나의 파이프라인을 거칩니다:

- **PBR 머티리얼**(metallic-roughness): 기본 색, 노멀, 메탈릭-러프니스, 오클루전, 이미시브. 투명 모드 opaque / mask / blend, 양면, unlit,
  머티리얼 변형(`KHR_materials_variants`), `"materials"`로 덮어쓰기. 플레이어 색: 머티리얼 전체, 마스크 텍스처, 텍스처 알파(Warcraft III와
  같은 방식), 버텍스 알파 중 하나.
- **스켈레탈 애니메이션**을 GPU에서(4개 영향, 본 수 제한 없음). 유닛 상태별 클립(`idle`, `move`, `attack`, `gather`, `build`, `die`, `spawn`,
  `hit`, `cast`…), 부드러운 전환, 본 마스크가 있는 레이어(걸으면서 상체로 공격), 실제 속도에 맞춘 보폭.
- **절차적 애니메이션**을 모델의 어떤 노드나 도형에도: 흔들림, 회전(풍차, 프로펠러), 이동 거리에 따른 바퀴, 목표를 조준하는 포탑, 반동,
  선회 시 기울기, 죽을 때 쓰러지거나 가라앉기 등. 상태나 전투 이벤트로 발동합니다.
- 머티리얼 **품질 단계**(`low` / `medium` / `high`)는 그림자와 마찬가지로 GPU에 따라 정해집니다. 테스트용으로 `AIVERSE_MATERIALS=low|medium|high`로
  덮어쓸 수 있습니다.
- **군중**: 클립을 텍스처에 굽고 같은 유닛은 인스턴싱으로 그립니다(애니메이션 중인 유닛 300개에서 1801 대신 61 드로 콜). 거리에 따른 모델 LOD.
  화면 밖이나 먼 곳의 애니메이션은 덜 자주 갱신합니다.
- **고급 애니메이션**: 걷기/달리기 블렌드 스페이스, 가산 클립(호흡, 반동), 지형에 맞춘 발 IK, 목표를 향한 머리, 경사에서의 차량 기울기, GPU
  모프 타깃(블렌드 셰이프).
- **소켓**: 손에 든 무기, 등에 진 짐(운반 중인 자원에 따라), 투사체 발사 지점, 이펙트 위치(연기, 불, 배기). 클립 이벤트와
  `RtsView::events()/effects()`를 소리와 파티클에 사용합니다.
- **단계**: 비계와 건설 단계, 체력에 따른 손상 버전, 등장과 사망 시 디졸브, 선택 시 외곽선, 건물을 놓을 때의 반투명 고스트, 초상화.
- **형식**: 추가로 **FBX**(ufbx)와 압축 텍스처 **KTX2**(ETC2/BC/ASTC, GPU에서도 압축된 상태). **Collada**, **3DS**, **PLY**, **STL**.
  복셀 **MagicaVoxel .vox**와 **.vxl/.hva**(Tiberian Sun, Red Alert 2). 사용자가 가진 게임 사본의 고전 RTS 모델: **Warcraft III .mdx/.mdl**
  (+ BLP), **Total Annihilation .3do**, **C&C Generals .w3d**(+ DDS). assimp가 읽을 수 있는 그 밖의 형식(.blend, .lwo, .x, .ms3d,
  .md2/.md3, .smd, .b3d…)은 유틸리티 **modelconv**가 glTF로 변환합니다. 에디터는 열 때와 저장할 때 자동으로 하고, 패키저는 변환 결과만
  패키지에 넣습니다.
- **필요에 따른 머티리얼**: 공유 라이브러리 `materials.json`(`base`로 상속), 모델 노드에 머티리얼 할당(`"assign": {"Turret": "steel"}`),
  노멀 맵과 플레이어 색 마스크가 있는 절차적 이음매 없는 텍스처. 미리 보기가 있는 **머티리얼** 창과 에이전트용 MCP 작업 `material_*` /
  `texture_generate`.

망가진 모델이 있어도 게임은 깨지지 않습니다: 그 타입은 도형으로 그려지고 이유는 에디터 콘솔에 나옵니다. 보기:
`aiverse_modelview model.glb@Walk` 또는 `aiverse_modelview --game templates/showcase`(게임의 모든 타입을 게임이 그리는 그대로 표시). 에디터에서는
**Models** 창(클립, 노드, 머티리얼, 초상화, 모델 파일 저장 시 다시 불러오기). 예: `templates/showcase`. 전체 형식: [models.md](../models.md).

## 라이팅

동적 조명은 구형 Android(Mali-T, Adreno 3xx/4xx)를 전제로 합니다: 모든 광원을 보이는 지면 위의 작은 라이트 맵에 그리고, 셰이더는 광원이 몇
개든 텍스처 샘플 한 번으로 빛을 얻습니다. 단계: `off`, `low`(128² 맵, 한 프레임 걸러 갱신), `medium`(256²), `high`(+ 스페큘러가 있는 픽셀 단위
광원 8개). 그림자와 마찬가지로 GPU에 따라 정해지며 수동으로는 `AIVERSE_LIGHTS=…`. 대전 중 프레임이 계속 예산을 넘으면 클라이언트는 조명과
그림자를 단계적으로 낮추고(가장 무겁고 덜 눈에 띄는 것부터), 격렬한 전투가 끝나면 한 단계 되돌립니다. 메뉴에서 그림자를 직접 고르면 이 기능은
꺼집니다(`AIVERSE_ADAPTIVE=off`).

- **타입과 전투의 빛** — `view.light`(모닥불, 대장간, 빛나는 버섯), 사격·명중·폭발 섬광, 투사체 빛(`attackLight`, `projectileLight`,
  `hitLight`, `deathLight`), 능력의 빛(`abilityLights`, 기본적으로 색이 효과를 따름: 피해는 주황, 회복은 초록), 무거운 bloom 대신 `halo`
  광륜. 전장의 안개가 섬광을 가립니다.
- **낮과 밤, 날씨** — `game.json` 또는 맵의 `lighting` 블록: `time`, `dayLength`, `nightLevel`, `clouds`, `wind`, `storm`(구름 그림자와 번개,
  리플레이에서도 동일), `preset`: `desert`, `snow`, `volcanic`, `overcast`, `cave`(지하: 영원한 밤, 광원의 빛만). 밤에는 이미시브 머티리얼이
  밝게 빛나고 불빛이 수면에 비칩니다.
- **맵 광원** — 맵의 `lights` 배열(랜턴, 모닥불, 용암).
- **접지 그림자와 AO** — 유닛 아래와 건물 주변의 부드러운 그림자. 모든 단계에서 켜짐(타입의 `contactShadow`, `contactShape`).
- **에디터** — "Lighting" 창: 시각과 뇌우 미리 보기, 조명 품질 단계, 광원 목록. 에이전트용: `light_add`, `light_update`, `light_delete`,
  `light_list`, `lighting_set`, `view_lighting`.

필드 형식은 `modules/rts/view/RtsLights.h` 헤더에 있습니다. 테스트 변수: `AIVERSE_TIME_OF_DAY`, `AIVERSE_NIGHT`, `AIVERSE_CLOUDS`,
`AIVERSE_STORM`.

## MCP를 통한 메커니즘 검증

코드를 쓰지 않고 게임을 만드는 에이전트는 봇 대전뿐 아니라 메커니즘을 정밀하게 검증합니다.
- **`scenario_run`** — 열린 맵의 사본에서 시나리오를 실행(아무것도 저장하지 않음): `clear`(객체, 웨이브, 시작 유닛이 없는 빈 맵), 이름 있는
  `spawn`(`"id": "foot", "count": 3` → `foot#1..3`, `"foot"`은 전부), 초 단위 명령(`play_command`와 같은 키), 일부 슬롯의 `bots`, 합격/
  불합격 결과를 내는 `checks`(`alive`, `dead`, `count`, `hp`, `resource`, `event`, `trigger`, `variable`, `distance`, `status`,
  `result` + 비교), `watch` + `every`(객체 상태의 시간 변화), `events`(이벤트 로그).
- **`play_units` / `play_command`** — Game 창의 라이브 테스트에 대한 명령(이전에는 에디터 내부 전용, 이제 MCP 클라이언트에 보임). 객체 참조:
  `play_units`의 ID, 맵의 `id` 이름, 또는 필터 `{type, owner, tags, kind, area, near + radius}`. `queue: true`는 대기열(Shift),
  `ability`는 ID로 지정.
- **`match_run`** — 추가로 발동한 트리거 로그와 `events`(이름별 이벤트 로그).
- **`rules_lint`** — `rules.json`과 맵에서 로더가 읽지 않는 키(오타, 잘못된 위치의 블록)를 힌트("did you mean 'armor'?")와 함께 보여 줍니다.
  실제 로더(`engine/data/Json.h`의 `JsonAccessLog`) 위에서 동작하며, `rules_set`, `rules_merge`, `map_validate`도 그런 키를 즉시 보고합니다.
- **`rules_schema`** — `part: "keys"`: 각 블록(`types.*.weapons[]`)에서 로더가 읽는 모든 키를 모든 게임에서 모은 것과, `docs/game-format.md`에
  없는 키. `part: "doc"`: 문서 자체.
- 시뮬레이션 이벤트 `kEvTriggerFired`(도구와 표시 전용, 체크섬에 포함되지 않음).
- 브랜치 `agent-mech-tools`.

## 포스트 이펙트

3D 장면 위에 얹는 전체 화면 효과는 데이터로 설정합니다: `game.json`의 `"postfx"` 블록(맵에서 덮어쓰기 가능). bloom, 색 보정, LUT, 비네트,
틸트 시프트, 햇살, FXAA, 그레인, 색수차, 샤프닝, 픽셀화, 포스터화, 색조, 레터박스에 더해 기성 세트(시네마, 비비드, 누아르, 레트로, 미니어처,
노을, 겨울, 야간 투시)가 있습니다. 각 효과에는 `minQuality`가 있습니다. 단계는 GPU에 따라 정해집니다: 약한 Android는 "낮음", 스마트폰은
"중간", PC는 "높음". `AIVERSE_POSTFX`로 덮어쓸 수 있습니다. 효과가 없으면 후처리 비용은 0입니다. 가벼운 효과는 한 패스로 합치고, 무거운
효과(bloom, FXAA, 광선)는 각각 패스를 추가합니다.

에디터: **포스트 이펙트** 창(단계 미리 보기, 기기 등급별 프레임 비용 표).
MCP: `postfx_catalog/get/add/set/remove/preset/enable/estimate/lut_neutral`, `view_postfx`. 변경 응답에는 ms/프레임 추정치와 무거운 효과에
대한 경고가 들어 있습니다. 자세히: `docs/postfx.md`.

## 검증됨 / 미검증

- ✅ Linux: 클라이언트, 에디터, 테스트. Windows .exe(MinGW로 크로스 빌드하고 Wine에서 실행).
- ✅ macOS: 빌드, 테스트, Metal에서 게임 실행. aiversebuild → .app과 iOS 프로젝트(+ 시뮬레이터용 빌드).
- ✅ Web: 빌드 가능(Emscripten 3.1.50), 헤드리스 Chromium(WebGL2/SwiftShader)에서 대전이 동작.
- ⏳ Android: Gradle 프로젝트가 생성됨. APK와 에뮬레이터 실행은 CI(작업 `android`)에 있으며 로컬에서는 빌드하지 않음.
- ⏳ 실제 하드웨어의 Windows와 iOS — 아직 실행하지 않음(CI에서는 MSVC 빌드와 iOS 시뮬레이터용 빌드).

## 플랫폼과 CI

`.github/workflows/ci.yml`은 `master`/`agent-*`에 푸시할 때마다, 그리고 풀 리퀘스트에서 실행됩니다:

| 작업 | 내용 |
|---|---|
| `linux` | gcc와 clang(x64) + gcc(ARM64), 경고를 오류로 처리, 테스트, 봇 대전, Xvfb에서 스크린숏 |
| `windows-msvc` | Visual Studio 2022, 테스트, 대전, `aiversebuild --target windows` |
| `windows-mingw` | MinGW-w64 크로스 빌드, Wine에서 테스트와 대전 |
| `macos` | 유니버설 Xcode 빌드, 테스트, 대전, `aiversebuild` → .app과 시뮬레이터용 빌드가 있는 iOS 프로젝트 |
| `android` | NDK로 `.so`, `aiversebuild`로 APK. 에뮬레이터에서: 시작 → 탭 → 백그라운드 → 복귀 → "뒤로" → 재시작, logcat으로 크래시 확인 |
| `web` | Emscripten, 헤드리스 Chromium에서 대전, 콘솔과 프레임 확인 |
| `determinism` | 모든 플랫폼의 대전 체크섬(게임 2개 × seed 3개)을 비교 — 비트 단위로 일치해야 함 |

스크린숏, APK, Web 빌드, .app은 실행 산출물로 보관됩니다.

로컬에서:

```bash
# Windows .exe from Linux/macOS
cmake -S . -B build/mingw -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=platform/cmake/mingw-w64.cmake
# bot matches on every game → a checksum file (compare between machines with diff)
.github/scripts/headless-checksums.sh build/linux-release/aiverse_headless sums.txt
# smoke test of the web build
node platform/web/smoke.mjs build/web
```

Web 빌드에서는 명령줄 인수를 URL로 전달합니다: `index.html?map=0&spectate` = `--map 0 --spectate`.

화면 키보드: `platform::startTextInput(area)` / `stopTextInput()`은 iOS, Android, 모바일 브라우저에서 키보드를 열고 닫습니다(데스크톱에서는
아무것도 하지 않음). 텍스트는 어떤 언어든 `Char` 이벤트로 오고, Backspace/Enter는 `KeyDown`/`KeyUp`으로 옵니다. `softKeyboardHeight()`와
`KeyboardChanged` 이벤트는 화면 아래가 얼마나 가려졌는지를, `TextInputEnded`는 사용자가 직접 키보드를 닫았음을 알려 줍니다. UI(`UiLayer`)는
`<input type="text|password|search|email|number">`와 `<textarea>`에 포커스할 때 이를 자동으로 호출합니다.

`Platform.h`에 있는 모바일과 브라우저용 기타 기능:
- `safeAreaInsets()` — 카메라 노치, 둥근 모서리, 홈 인디케이터, 시스템 바를 위한 여백(iOS, Android, 모바일 Safari/Chrome, 데스크톱에서는 0,
  노치가 있는 MacBook에서는 전체 화면일 때).
- `setFullscreen()` / `isFullscreen()`. 내장 전환 — F11(Windows, Linux, Web), Alt+Enter(Windows). 브라우저에서는 전체 화면일 때 화면 방향도
  고정됩니다(`AppConfig::orientation`).
- Android: 뒤로 버튼은 `Key::Escape`로 게임에 전달됩니다. 자동으로 숨는 바가 있는 전체 화면. 물리 키보드와 마우스(Chromebook, DeX)는 일반 키와
  마우스 이벤트가 됩니다.
- iPad: Magic Keyboard / 블루투스 키보드, 마우스와 트랙패드(오른쪽 클릭, 호버, 스크롤).
- 클립보드는 어디서나 동작합니다. 브라우저에서는 브라우저가 텍스트를 넘겨준 뒤 Ctrl/Cmd+V가 게임에 전달됩니다.
- Web: `userDataDir()`은 IndexedDB에 저장되어 설정과 저장 데이터가 페이지를 새로 고쳐도 남습니다.

### 네이티브 플러그인: 결제와 광고

어떤 플랫폼 SDK든 그 네이티브 언어의 플러그인으로 연결합니다 — Java(Android), Objective-C(iOS/macOS), JavaScript(Web), C++ — 공통 비동기
브리지 `engine/platform/Plugins.h`를 통해서입니다. 게임은 `engine/services`의 공통 API `aiverse::store`(인앱 결제)와 `aiverse::ads`(광고)를
쓰고, 제공자는 `game.json`에서 플랫폼별로 고릅니다. `plugins/`에 있는 기성 플러그인: **App Store**, **Google Play**, **RuStore**,
**Yandex Games**(Web: 결제와 광고), **Steam**(Inventory Service와 DLC를 통한 결제, Windows/Linux/macOS), **Yandex Mobile Ads**, **AdMob**,
**AppLovin MAX**, **ironSource (Unity LevelPlay)**. 빌드에는 게임이 고른 플러그인만 들어갑니다(game.json의 `"plugins"` 또는 `--plugins`).
관련 없는 SDK, 의존성, 매니페스트 항목, Info.plist 키는 들어가지 않으며 CI가 이를 확인합니다. 데스크톱 디버깅용으로 상점과 광고의 모의
구현(`mock-store`, `mock-ads`, `AIVERSE_MOCK_STORE`, `AIVERSE_MOCK_ADS`)이 있어 결제 흐름 전체를 스마트폰 없이 시험할 수 있습니다. 모의 구현은
릴리스 빌드에 컴파일되지 않습니다. aiversebuild는 플러그인을 Gradle/매니페스트, Info.plist/Podfile, HTML에 자동으로 기록합니다. Google Play와
RuStore는 따로 빌드하세요: `--plugins googleplay,admob`과 `--plugins rustore,yandex-ads`. 자세히: [plugins.md](../plugins.md).

### 릴리스

`.github/workflows/release.yml`: 태그 `v1.2.3`(또는 원하는 게임 폴더를 지정한 수동 실행)으로 `aiversebuild`가 모든 플랫폼을 빌드하고 GitHub
Release를 게시합니다:

| 플랫폼 | 산출물 | 서명(저장소 시크릿) |
|---|---|---|
| Windows | 설치 프로그램(Inno Setup, 관리자 권한 불필요) + 포터블 .zip | `WINDOWS_CERT_PFX_BASE64`, `WINDOWS_CERT_PASSWORD` |
| Android | Google Play용 .apk와 .aab | `ANDROID_KEYSTORE_BASE64`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`, `ANDROID_KEY_PASSWORD` |
| macOS | .zip에 담긴 .app, 공증 완료 | `MACOS_CERT_P12_BASE64`, `MACOS_CERT_PASSWORD`(Developer ID) + App Store Connect 키 |
| iOS | Xcode 프로젝트, 키가 있으면 TestFlight용 빌드 | `APPLE_TEAM_ID`, `ASC_KEY_ID`, `ASC_ISSUER_ID`, `ASC_KEY_P8_BASE64` |
| Linux | .tar.gz | — |
| Web | .zip, `DEPLOY_WEB_PAGES=true`(저장소 변수)면 GitHub Pages에 게시 | — |

시크릿이 없어도 릴리스는 나옵니다: APK는 디버그 키로 서명, .app은 ad-hoc, Windows는 서명 없음.

### 미리 빌드된 엔진(`builds` 브랜치)과 컴파일러 없는 aiversemcp

미리 빌드된 에디터와 도구는 **`builds`** 브랜치에 게시됩니다: `<platform>/AIVERSE-<version>-<commit>-<platform>.zip`(현재 `windows` x86_64와
`macos` arm64, `linux` x86_64는 통합 담당자가 게시할 때). 안에는 해당 커밋의 엔진 소스(템플릿 `templates/`, `engine/assets`, `docs/` — MCP
서버가 읽음)가 든 `AIVERSE/` 폴더와 프로그램이 든 `AIVERSE/bin/`이 있습니다. 패키징은 `tools/release/package_engine.py`, 게시는
`tools/release/publish_builds.py`(AGENTS.md §8). Linux 패키지는 에디터 없이 `aiversemcp`와 `aiverse_headless`만 있을 수도 있습니다.

압축을 푼 폴더에서 MCP 서버 실행(봇 대전과 `balance_*`는 aiversemcp 안에서 돌아가므로 별도의 `aiverse_headless`가 필요 없음. 에디터는 선택이며,
없으면 `--no-editor`):

```bash
# ARCHIVE is the path to the AIVERSE archive downloaded from index.json.
unzip "$ARCHIVE" -d ~/aiverse      # unzip keeps the executable bit
~/aiverse/AIVERSE/bin/aiversemcp --no-editor --engine ~/aiverse/AIVERSE
```

aiversemcp가 자기 옆에서 엔진을 찾게 된 커밋(`engine/platform/EngineDir.h`, 에디터와 동일) 이후의 빌드에서는 `--engine`을 생략할 수 있습니다.
그보다 먼저 게시된 빌드에서는 필수입니다. 권한을 복원하지 않는 압축 해제 도구(Python의 `zipfile`)를 썼다면 `bin/`의 파일에 `chmod +x`를
실행하세요. macOS에서는 브라우저로 내려받은 파일이 격리될 수 있습니다: `xattr -dr com.apple.quarantine ~/aiverse/AIVERSE`.

엔진을 직접 내려받는 프로그램은 README가 아니라 브랜치 루트의 **`index.json`**
(`https://raw.githubusercontent.com/chromealex/AIVERSE/builds/index.json`)을 읽습니다. 아래는 과거 RTSEngine 항목의 예입니다. 새 빌드는 경로에
AIVERSE를 쓰고 키 `rtsmcp`는 유지됩니다:

```json
{
  "schema": 1,
  "repository": "chromealex/AIVERSE",
  "branch": "builds",
  "packages": [
    {
      "platform": "macos",
      "arch": "arm64",
      "version": "0.2.0",
      "commit": "77126fa9",
      "file": "macos/RTSEngine-0.2.0-77126fa9-macos.zip",
      "url": "https://raw.githubusercontent.com/chromealex/AIVERSE/builds/macos/RTSEngine-0.2.0-77126fa9-macos.zip",
      "size": 43055202,
      "sha256": "c08044fd5a13648016f4d0f2184edb6054d338a814c2b2de2caab09b4cf4513d",
      "root": "RTSEngine",
      "rtsmcp": "RTSEngine/bin/rtsmcp",
      "editor": "RTSEngine/bin/RTSEditor.app",
      "headless": "RTSEngine/bin/rts_headless"
    }
  ]
}
```

- `packages`는 최신순입니다. 필요한 `platform` + `arch`에 맞는 첫 항목이 최신 빌드입니다.
- `platform`: `windows` | `macos` | `linux`. `arch`: `x86_64` | `arm64` | `universal`(macOS 유니버설) | `null`(판별 불가) — zip 안
  `aiversemcp`의 헤더로 판정.
- `rtsmcp`, `editor`, `headless`는 아카이브 안의 경로이며, 패키지에 해당 프로그램이 없으면 `null`. `--engine`은 `root` 폴더입니다. macOS에서
  `editor`는 `.app` 번들입니다.
- `url`은 게시할 때마다 다시 쓰이는 브랜치를 가리킵니다(플랫폼별 5개 버전 유지). 오래된 항목은 사라지므로 최신 `index.json`에서 내려받고
  `sha256`을 확인하세요.
- 새 필드가 추가될 수 있습니다. 호환되지 않는 형식 변경이 있으면 `schema`가 올라갑니다.

확인: `python3 tools/release/test_release.py [build/linux-release/aiversemcp]`(CTest `release_tools`) — `index.json` 형식과, aiversemcp
경로를 주면 Linux 패키지 생성, 임시 폴더에 압축 해제, 그곳에서 `--engine` 없이 MCP 호출(`tools/list`, `create_project`, `map_generate`,
`map_validate`, `map_ascii`).

스크린숏 모드(`AIVERSE_SCREENSHOT`, `AIVERSE_EXIT_AFTER_FRAMES`)는 프레임마다 정확히 1/60초씩 진행하므로 N번째 프레임은 어느 컴퓨터에서나
같습니다.

## 앞으로

glTF 모델과 애니메이션, 사운드, 네트워크(락스텝), 데이터로 정의하는 능력과 버프, 대군을 위한 플로 필드와 RVO, AI 개선, C# 스크립트(iOS에서는 AOT).

## C++ 게임 모듈

Lua 외에도 프로젝트에 `native/CMakeLists.txt`와 C++20 모듈을 넣을 수 있습니다. 빌드할 때 `aiverse_native_module(...)`로 추가하고 `rules.json`의
`nativeModules` 배열로 켭니다. tick, 명령, 롤백, 상태 저장을 지원하며 Lua와 C++를 함께 쓸 수 있습니다. C++를 바꾸면 게임과 에디터를 다시
빌드해야 합니다. [API, 예제, 빌드 방법](../native-modules.md).

## 재사용 가능한 C++ 메타 라이브러리

`aiverse::meta` — 설정 가능한 상점/오퍼/제작, 통화와 아이템, 퀘스트와 패스, 일일 보상, 에너지, 우편함, 순위표, 채팅, 세그먼트. Lua/GPU 없이
프로젝트의 네이티브 코드에서 쓸 수 있고, 정의는 JSON이므로 알고리즘을 다시 생성할 필요가 없습니다. 시작:
`python3 tools/meta/scaffold.py --preset mobile --out <project>`. 문서: [meta-game.md](../meta-game.md). 에이전트용은
`skills/meta-game/SKILL.md`. 로컬 정책은 서버 없이 동작하지만, 기기 간 통신이나 전역 순위표에는 실제 백엔드가 필요합니다. 완성된 클라우드
서비스는 패키지에 포함되지 않습니다.

프리셋 `full --with-ui`는 친구/클랜, 인스턴스 컬렉션, 장비, 합성, 구독, 토너먼트 리그를 추가합니다. `aiverse::meta::Service`는 작업을
원자적으로 실행하고, `tools/meta/server.py`는 세션 검증이 있는 로컬 HTTP/SQLite 호스트입니다. 브라우저용 메타 화면과 에이전트를 위한 짧은
레시피 카탈로그가 함께 제공됩니다. [빌드, 실행, 제한](../meta-service.md).

`full`에는 LiveOps 15개 그룹도 들어 있습니다: 천장이 있는 상자, 앨범/교환, 작업 대기열, 연구/프레스티지, 복합 퀘스트, 이벤트, 클랜전, 비동기
PvP, 파티 매치메이킹, Elo/Glicko-2, 추천/프로모 코드, 보상형 광고, 오퍼 연쇄, 알림, 오프라인 수익. 모든 매개변수는 `extensions.liveops`에
있으며, 이 프리셋은 별도의 새 UI 화면이나 외부 SDK를 설치하지 않습니다. `python3 tools/meta/catalog.py describe matchmaking`은 필요한
스키마와 예제만 출력합니다. [API, 정책, 제한](../meta-liveops.md).

### 선택적 3D 물리

Box3D는 플래그 `AIVERSE_ENABLE_PHYSICS=ON`으로 켭니다. 일반 RTS 프로젝트에는 필요 없습니다. 독립 예제 `aiverse_physics_demo`에서 강체, 충돌,
진자, 상태 저장을 확인할 수 있습니다. [빌드, API, 통합 제한](../physics.md).

### 시각 효과용 XPBD 천

선택 모듈 `AIVERSE_ENABLE_CLOTH=ON`: 자체 XPBD 천 솔버를 CPU 또는 Metal compute에서 실행하며, 공유 배치, 시간 방향 LOD, 자동 휴면, 고밀도
메시를 인스턴스 드로 한 번으로 그리는 기능을 갖췄습니다. GPU에서는 위치가 비디오 메모리에 머물고, 지원하지 않는 백엔드는 CPU를 씁니다.
예제에서 GPU와 CPU 기준 결과 비교, 휴면/깨어남 확인을 할 수 있습니다. [독립 예제, 제한, 측정값](../cloth.md).
