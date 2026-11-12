# 다른 엔진의 프로젝트 가져오기

구현은 `agent-engine-import`에 있으며, 원래 명세는 [engine-import-spec.md](../engine-import-spec.md)에 있습니다. 시뮬레이션과 템플릿의
체크섬은 바뀌지 않았습니다. 이것은 동작하는 가져오기 기반이며, **명세의 모든 단계를 완료한 것은 아닙니다**. 아래의 지원 범위와 남은 작업은
MAIN에 넘기는 인수인계의 일부입니다.

## CLI와 중간 데이터

Python 3.8+, Python 패키지는 필요 없습니다. 실행 가능한 런처는 `tools/importers/port/aiverse_port`입니다(또는 아래의 명시적인 Python 형식):

```sh
python3 tools/importers/port/aiverse_port.py scan /path/to/UnityProject
python3 tools/importers/port/aiverse_port.py extract /path/to/UnityProject --out /tmp/port.json
python3 tools/importers/port/aiverse_port.py emit /tmp/port.json --out /path/to/Imported
python3 tools/importers/port/aiverse_port.py /path/to/UnityProject --out /path/to/Imported --scale 1 --mode auto
python3 tools/importers/port/aiverse_port.py report /path/to/Imported
python3 tools/importers/port/aiverse_port.py item /path/to/Imported --id ITEM_ID
python3 tools/importers/port/aiverse_port.py resolve /path/to/Imported --id ITEM_ID --how lua --reason 'Verified by scenario…'
```

모든 명령은 JSON을 출력하며, 실패하면 상태 코드 2로 종료합니다. `scan`은 아무 파일도 쓰지 않습니다. 입력 폴더와 출력 폴더는 겹치면 안 됩니다.
같은 대상에 여러 가져오기를 동시에 실행하지 마세요. `extract`는 원본 경로, 직렬화된 컴포넌트 전체, 필드, 스크립트 소스와 사용처를 기록합니다.
`emit`은 게임 매니페스트, 규칙, 맵, `.rtsproj`, 복사된 에셋, 선택적인 스프라이트/타일/오디오/이펙트, `port.json`, JSON/Markdown 보고서를
씁니다. 복사된 에셋을 쓸 때는 `sourceRoot`가 여전히 존재해야 합니다. 생성된 `.rpakignore`는 스크립트 중간 인덱스와 보고서를 런타임 팩에서
제외합니다. 매니페스트는 가져온 콘텐츠를 `external: true`로 표시하며, aiversebuild가 이를 팩에 전달합니다.

`--scale`은 미터당 셀 수입니다. `--mode auto|2d|3d`는 장면 평면과 카메라를 고릅니다. Unity 3D는 Z를 뒤집고, Unity 2D는 XY를 지면에
투영합니다. Godot의 Vector2는 XZ에 대응합니다. 원점은 맵 좌표가 양수가 되도록 이동합니다. 소유자를 임의로 추론하지 않습니다: Unity의 명시적인
`owner` 필드만 근사 경고와 함께 매핑됩니다. 원본 프로젝트의 플레이어 번호는 직접 확인해야 합니다.

`lib/port/mapping.json`은 필드 별칭을 정의합니다. `--mapping FILE`은 지원되는 대상에 대해 별칭을 추가하거나 덮어씁니다. `fireRate`는 초당
발사 횟수(쿨다운의 역수)로 해석되며 명시적으로 근사로 표시됩니다. 엔진 JSON의 쿨다운과 건설 시간 단위는 시뮬레이션 tick이 아니라 **초**입니다.
추론된 모든 필드에는 출처, 신뢰도, 대상 경로가 붙습니다.

### 반복 가져오기

`.port-state.json`은 생성된 파일/타입의 해시를 저장합니다. 편집된 파일이나 원래 있던 파일은, 에디터가 `ported` 표시를 유지했더라도
보존됩니다. 규칙은 타입 단위로 병합되며, 편집된 타입은 그 표시를 잃습니다. 새 타입은 계속 추가할 수 있습니다. 규칙 수준 설정에 대한 사용자의
변경은 보존됩니다. 맵과 바이너리 파일은 파일 전체 단위로 보호됩니다. 오래된 생성 파일은 삭제되지 않습니다. 상태 매니페스트는 가져오기 도구가
소유하므로 직접 편집하지 마세요.

해결 표시는 항목과 관련 원본 엔티티/스크립트의 지문이 바뀌지 않았을 때만 반복 가져오기 후에도 유지됩니다. 원본이 바뀌면 해당 항목은 검토를
위해 다시 열립니다.

## 에디터와 MCP

**프로젝트 → 엔진 프로젝트 가져오기**를 열거나 `editor_window {id:"project_import", open:true}`를 호출합니다. 원본 프로젝트를 스캔하고,
대상/스케일/2D/3D를 고른 뒤 가져옵니다. UI는 렌더링 스레드 밖에서 가져오기 도구를 실행하며 결과를 열 수 있습니다. **AI 인수인계 작업
복사**는 명시적인 작업을 복사하고 Agent 창을 엽니다. 이것이 에이전트를 실행했다는 뜻은 아닙니다.

현재 기반 브랜치에는 "게임 만들기" 마법사가 없습니다. 별도 브랜치 `agent-gamewizard`와의 통합은 MAIN에 맡깁니다. 관련 없는 다른 브랜치는
병합하지 않았습니다.

등록된 작업(가져오기 설정은 `options` 안에 중첩해도 됩니다):

- `port_scan {path}`: 원본 프로젝트를 식별하고, 직렬화 방식을 확인하고, 에셋/장면/스크립트를 나열합니다.
- `port_import {path,out,scale?,mode?,mapping?}`: 추출과 출력을 수행합니다.
- `port_report {path?}`: 대상의 보고서를 읽습니다(기본값은 현재 열린 프로젝트).
- `port_item {path?,id}`: 원본 항목, 컴포넌트, 연결된 스크립트, 생성된 타입.
- `port_resolve {path?,id,how,reason}`: 검증된 작업을 표시합니다(`rules`, `lua`, `native`, `dropped`).

MCP를 통한 가져오기는 기존 VFX 가져오기 도구와 마찬가지로 동기식이며, 큰 프로젝트는 시간이 걸릴 수 있습니다. `AIVERSE_PYTHON`이 인터프리터를
고릅니다. 명령 인수의 Windows 셸 메타 문자 `%`, `!`, 따옴표, 줄바꿈은 확장되지 않고 거부됩니다.

에이전트 작업 흐름: 누락된 항목을 검토하고, `rules_schema`를 참고하고, 맞는 메커니즘을 먼저 데이터로, 그다음 Lua로, 필요할 때만 네이티브
모듈로 구현합니다. `scenario_run`과 봇 대전으로 DPS, 생산 시간, 이동 속도를 비교합니다. 원본 화면이 있으면 스크린샷을 비교합니다. 검증된
작업만 해결로 표시하고, 의도적으로 버린 동작에는 설명을 붙입니다. C#/GDScript/Blueprint는 기계적으로 번역하거나 실행하지 않습니다.

## 지원 범위와 명세의 남은 작업

| 영역 | 구현됨 | 남은 작업 |
|---|---|---|
| Unity 공통 코드 | YAML, GUID 조회, 중첩 프리팹과 변형이 `lib/unity`로 이동, 호환 import로 VFX가 계속 동작 | 공통 파서의 기존 중첩 제한은 여전히 적용됨 |
| Unity 장면/타입 | 월드 변환, 장면 객체, 프리팹 오버라이드, 스크립트 사용처와 직렬화 필드, ScriptableObject 후보, 빌드 장면 순서 | 변환 스케일을 모델 경계 상자에 반영, 견고한 의미 기반 타입 그룹화, 플레이어/스폰 지점 추론 |
| 에셋/머티리얼 | 모델/텍스처/오디오와 인접 의존 파일을 경로 유지, PBR 계수와 첫 머티리얼 오버라이드, 모델 스케일 메타데이터, 기존 ImageMagick/ffmpeg 어댑터로 PSD/TIFF를 PNG로 선택 변환 | 머티리얼 슬롯 매칭, 패킹된 맵, 가져오기 축 설정, 애니메이션 프레임 분할. 이미지 변환기가 없으면 보고서에 기록 |
| Unity 2D | 아틀라스에서 스프라이트 잘라내기, 명시적인 Tilemap 셀을 아틀라스/고정 레이어로 굽기, 직교 카메라 | RuleTile, 변환/색이 입혀진 타일, 임의의 Grid 레이아웃, 애니메이션 타일, 스프라이트 피벗/정렬 |
| 오디오/이펙트 | 카메라/매니저의 반복 오디오 → 대전 재생 목록, 기존 VFX 변환기가 보호된 스테이징 영역을 통해 출력 | 믹서 라우팅과 이펙트-이벤트의 의미 연결 |
| 지형 | 불투명한 구운 데이터와 Terrain 컴포넌트를 보고서에 기록 | TerrainData 하이트필드/splat/나무/디테일 변환은 아직 미구현 |
| Godot | 텍스트 장면/리소스, 외부 리소스, 단순 이동, 내보낸 숫자 오버라이드, 스크립트 인덱스, 메인 장면 순서 식별 | 장면 상속, 완전한 변환 합성, 여러 줄의 복잡한 Variant 디코딩, TileMap/GridMap 변환, 애니메이션 클립 |
| Unreal | JSON 수신부와 에디터 내 Python 내보내기 도구: 로드된 Actor, 리플렉션으로 얻은 후보 속성, AssetExportTask로 메시/텍스처/오디오 내보내기 | 내보내기 도구는 **아직 Unreal에서 실행된 적이 없음**. 지형, Blueprint 변수의 완전한 열거와 그래프 내보내기는 미구현 |
| 에디터 | 독립된 현지화 가져오기 창, 메뉴 항목, MCP, 복사 가능한 AI 인수인계 작업 | 마법사와 통합, 에셋 범주 선택, 에이전트 직접 실행 |

Force Text를 켜도 알 수 없는 Unity 네이티브 바이너리 `.asset`(구운 라이팅/NavMesh)이 있을 수 있습니다. 이들은 하나씩 보고서에 기록됩니다.
바이너리로 직렬화된 장면/프리팹/머티리얼은 거부되며 Force Text를 켜라는 안내가 나옵니다. `.uasset`은 Unreal 밖에서 절대 파싱하지 않습니다.

Unreal 도우미 스크립트는 `tools/importers/unreal_export/export.py`입니다. Python 플러그인이 켜진 Unreal Editor에서 새 출력 폴더로
`export_project(destination)`을 호출하세요. API 참고:
[AssetExportTask](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/AssetExportTask?application_version=5.6),
[EditorActorSubsystem](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/EditorActorSubsystem?application_version=5.6).
현재 로드된 레벨과 `/Game` 에셋을 내보내며, 로드되지 않은 레벨은 순회하지 않습니다.

## 검증, 2026-11-03

`tests/data/port`의 합성 프로젝트는 직접 만든 테스트 데이터입니다. Unreal FBX는 기존의 손으로 쓴 CC0 파일 `tests/data/models/cube.fbx`입니다.
다운로드한 게임 에셋은 커밋하지 않았습니다.

- 공통 코드를 옮긴 뒤 기존 VFX 가져오기 테스트 34개가 모두 통과합니다.
- 프로젝트 가져오기 테스트 15개가 다루는 것: 부적합한 직렬화 거부, 프리팹 오버라이드, 단위, 보호된 편집, 멱등성(`port.json` 재출력 포함),
  원본 변경으로 인한 무효화, 경로 안전성, 스프라이트/타일, CLI를 통한 해결. `AIVERSE_PORT_HEADLESS`를 설정하면 다섯 프로젝트 모두 실제
  엔진에서 로드되고, Unity 전투 프로젝트는 플레이어 1의 승리로 끝납니다.
- C++ 테스트 스위트: 5857개 검사, 샌드박스에서만 발생하는 localhost 수신 실패 1건. 해당 테스트는 네트워크 샌드박스 밖에서 단독 실행 시
  통과합니다(27개 검사). 추가 MCP 브리지 테스트 통과(5개 검사).
- 이번 변경과 무관한 기존 컴파일 경고: `engine/fx/FxEffect.cpp:1058`의 사용되지 않는 `fb`.

공개 RTS 저장소 두 개를 `/tmp`에만 내려받아 가져왔습니다. 첫 번째/기본 맵만이 아니라 **생성된 모든 맵을 `aiverse_headless --max-seconds 0`으로
하나씩 로드했습니다**:

| 출처 | 리비전 | 결과 | 보고서 |
|---|---|---|---|
| [Unity FPS RTS](https://github.com/aydenspringer/Unity-FPS-RTS-System) | `df468f644e6aff145d2eb219dca87f99742ca81d` | 에셋 509개 복사, 타입 후보 375개, 맵 30개, 30/30 로드 가능 | 근사 486건, 누락 1913건. 스크립트, 렌더링/물리 컴포넌트, 불투명한 구운 데이터, 지원되지 않는 에셋은 검토 필요 |
| [Godot Open RTS](https://github.com/lampe-games/godot-open-rts) | `a628ad3bb6a9a903587a181a7ee3300dbf735e81` | 에셋 388개 복사, 타입 후보 552개, 맵 84개, 84/84 로드 가능 | 근사 638건, 누락 2017건. 스크립트 로직, 장면 상속, 내장 리소스, 타일/애니메이션 시스템은 검토 필요 |

이것은 가져오기/로드 검사이며 **플레이 가능한 1:1 이식이 아닙니다**. 후보 수를 성공적으로 재구성된 게임 메커니즘처럼 홍보해서는 안 됩니다.
Unreal에서의 실행과 Windows, Android, Web에서의 실행은 아직 검증되지 않았습니다.

독립된 가져오기 양식은 macOS 네이티브 에디터에서 확인했습니다. 클립보드/빠른 키 입력을 이용한 자동 입력은 ImGui 텍스트에 안정적으로 들어가지
않아, 클릭 방식의 전체 가져오기를 완료했다고 주장하지 않습니다. 같은 작업은 C++ MCP 브리지와 CLI로 검증했습니다. macOS 에디터/Metal은
컴파일됩니다.

현재 master(`82c47120`)를 병합한 뒤 AppleClang에서 컴파일되도록 `NativeBuild.cpp`의 `std::filesystem::path` 루프 두 곳을 `const auto&`로
바꿨습니다. 공유 파일에 대한 최소한의 호환성 수정입니다. MAIN은 이 브랜치를 통합하고 검증한 뒤 에디터 패키지를 다시 배포해야 하며, 이
브랜치는 빌드를 배포하지 않습니다.

master 병합과 호환성 수정 후의 최종 검증:

- macOS 에디터(Metal), aiversemcp, aiverse_headless, aiverse_tests 모두 다시 빌드에 성공.
- 소켓 제한이 없는 샌드박스 밖에서 전체 C++ 테스트 스위트 실행: **5930개 검사, 실패 0**.
- CTest: 기존 VFX와 프로젝트 가져오기 대상 통과(Python 테스트 34 + 15개).
- 대상 MCP 브리지: 5개 검사, 네이티브 에디터 빌드 도우미: 42개 검사, 모두 통과.
- `aiversemcp --no-editor`를 통한 종단 간 가져오기가 중첩된 `options`를 받아들이고, scale=2를 적용하며, 공백과 아포스트로피가 포함된 출력
  경로를 처리합니다.
- `skirmish --seed 3`: 파랑이 게임 시간 470초에 승리, `mushrooms --seed 3`: 빨강이 92초에 승리.
