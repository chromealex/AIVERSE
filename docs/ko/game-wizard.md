# "게임 만들기" 마법사

창 하나(프로젝트 허브 → **게임 만들기**, 또는 **프로젝트 → 게임 만들기…**) 또는 MCP 호출 한 번(`game_make`)으로 `.aiverse`를 갖춘
완전한 게임 프로젝트가 만들어집니다. 아이디어에 맞게 고른 템플릿 기반의 게임, 사용자의 아트 / 모델 / 음악 / 효과음, 강제 튜토리얼 맵,
대전 중의 은근한 힌트, 생성된 레벨, 메타 게임(메뉴, 업그레이드, 상점, 일일 보상, 프로필, 설정, 결과 화면), 원하는 경우 결제와 광고,
언어, 브랜딩, 그리고 봇을 이용한 검증까지 포함됩니다. 작성되는 것은 모두 일반 게임 데이터이므로 이후 자유롭게 편집할 수 있습니다.

코드: `apps/editor/core/GameWizard*.cpp`(UI 없음), `apps/editor/core/WizardOps.cpp`(MCP), `apps/editor/EditorGameWizard.cpp`
(허브 페이지). 모든 게임에 복사되는 런타임 키트: `templates/_wizard/`(Lua + RmlUi, README 참고).

## 답변(`WizardSpec`, 모두 선택 사항)

| 그룹 | 키 |
|---|---|
| 콘텐츠 | `artDir`(폴더: png/jpg/tga, glb/gltf/obj/fbx, wav/ogg/mp3/flac, ttf/otf, md/txt), `artUrl`(파일 또는 .zip / .tar.gz, 시스템 curl로 `~/.aiverse/wizard/<시각>`에 다운로드) |
| 아이디어 | `idea`(아무 언어나 가능, 폴더의 .md/.txt도 읽음), `genre`(템플릿 ID 또는 `auto`), `features`(economy base army garrison lanes waves territory heroes spells survival planets 2d casual portrait scifi modern) |
| 프로젝트 | `title`, `location`, `studio`(bundle id `com.<studio>.<slug>`), `version`, `orientation`, `languages`, `platforms`, `seed` |
| 튜토리얼 | `tutorial`(`both` / `hard` / `soft` / `none`), `tutorialForced`, `tutorialSkippable`, `tutorialDim`, `hints` |
| 레벨 | `levels`(0..40), `mapPreset`(`auto`, `none` = 기본 맵 복사, 또는 생성기 프리셋), `mapSize`, `bossEvery`, `difficultyCurve`, `startResourcesPercent`, `keepTemplateMaps`, `verifyLevels` |
| 메타 | `metaIdea`(자유 서술: 메타에 넣고 싶은 것, 에이전트 작업에 전달됨), `currencies`(soft, hard), `upgrades`, `shop`, `daily`, `profileScreen`, `xpPerLevel` |
| 수익화 | `ads`, `interstitialEvery`, `purchases`, `stores` |
| 외관 | `brandingFromArt`, `paletteFromArt` |

`game_make_options`는 선택지를 나열하고, `game_make_plan`은 무엇이 만들어질지 보여 줍니다(파일을 쓰지 않음). 같은 답변 + seed = 같은 게임.

## 결정 방식

- **템플릿**: 아이디어의 단어를 각 템플릿의 키워드(어간, ru / en)와, 원하는 기능을 그 메커니즘과, 화면 방향을 그 화면과 대조합니다.
  동점이면 seed로 정합니다. 아무것도 지정하지 않으면 균형 잡힌 템플릿 중 하나를 고릅니다.
- **콘텐츠**: 모델과 2D 스프라이트는 파일 이름에 ID / 이름(ru / en)이 포함된 타입에 할당됩니다. 나머지는 복사되어 `WIZARD.md`에
  나열됩니다. 키 아트(이름과 크기로 판단)는 스플래시 화면, 메뉴 배경, UI 강조 색이 되고, 가장 정사각형에 가까운 이미지(또는 "icon"/"logo")는
  1024 아이콘이 됩니다. 음악은 이름에 따라 `menu` / `match` / `battle` / `victory` / `defeat`로, 짧은 효과음은 이벤트(`click`, `select`,
  `move`, `died`, `underAttack` 등)에 할당됩니다. 첫 번째 TrueType 글꼴은 `fonts/title.ttf`가 됩니다.
- **강제 튜토리얼**(`maps/wiz_tutorial.json` + `scripts/wizard/tutorial_steps.lua`)은 규칙에서 자동으로 작성됩니다:
  - *경제*(일꾼, 사이드바 조선소, 전개형 MCV, TA 건설자): 전개, 채집, 필요하면 워프 필드 / 보급, 병영 같은 생산 건물의 선행 조건 체인,
    유닛 2기 훈련, 반격하지 않는 적 건물 파괴;
  - *주둔*(Mushroom Wars, State.io): 중립 건물로 보내기, 두 개 더 점령, 적 건물 점령;
  - *자동 건설*(레인 배틀): "건설"을 두 번 누르기, 업그레이드, 버티기;
  - *범용*: 군대 이동, 공격.
  해당 단계의 명령만 통과하고(`before_command`) 적은 대기합니다(단, 유닛은 방어합니다). 패널과 화살표는 뷰 스크립트가 그리며, 막히지 않도록
  단계에 필요한 자원은 보충됩니다. Knights and Merchants식 도로 경제에는 맞지 않습니다(그 경우 범용 형식).
- **은근한 힌트**: `scripts/wizard/hints_config.lua`의 일회성 힌트(카메라, 놀고 있는 일꾼, 자원 부족, 인구 한도, 군대, 주둔, 공격받는 중).
  이미 본 힌트는 프로필에 기록되며 설정에서 끌 수 있습니다.
- **레벨**: `maps/wiz_level_N.json` = 기본 맵 + `generate`(템플릿 프리셋, 레벨별 seed, 기본 맵을 틀로 사용). 난이도는 곡선을 따라 0..2,
  N 레벨마다 보스. 보상, 첫 승리 보상, 별 획득 시간은 `ui/wizard.json`에 들어갑니다.
- **메타**: `ui/wizard.json`(설정), `ui/profile.json`, `ui/shop.json`, `ui/screens.json`(`start: home`, `afterMatch: result`), 키트의
  화면. 업그레이드는 `initialVars` `wiz_up_<id>`로 대전에 전달되고 `scripts/wizard/meta_bonus.lua`가 버프 / 시작 자원으로 바꿉니다.
- **수익화**: `game.json`에 플랫폼별 `plugins` / `store` / `ads`(Yandex 데모 광고 단위와 데스크톱용 모의 구현, ID는 교체하세요).

## 에이전트 단계: 설명한 그대로의 게임을 1:1로

템플릿은 출발점이지 게임 자체가 아닙니다. `agent`가 켜져 있으면(기본값) 마법사는 프로젝트에 `WIZARD_TASK.md`를 씁니다. 여기에는 설명 /
GDD 원문과, 마법사가 만든 부분(튜토리얼 단계, 힌트, 메타 설정, 언어 파일)의 일관성을 유지하는 방법이 들어 있습니다. 이를 연결된 AI
에이전트에 넘기면 에이전트가 에디터의 MCP 도구를 통해(에디터에서 실시간으로) 프로젝트를 다시 만듭니다. 타입, 능력치, 진영, 메커니즘(규칙
또는 Lua), 맵, 승리 조건, 텍스트. 그 다음 `rules_lint`, `map_validate`, `match_run`으로 확인하고 WIZARD.md에 "## Агент: что сделано"를
기록합니다.

- 연결: 마법사에는 Agent 창과 같은 "AI 에이전트 연결" 패널이 표시됩니다(`EditorMcpConnect.*`, `core/McpInstall.h`). 한 번의 클릭으로
  aiversemcp가 클라이언트 설정에 기록되며, 그 하나의 연결을 양쪽에서 사용합니다.
- 에디터에서 실행(`core/WizardAgent.*`, CLI를 찾을 수 있도록 사용자의 로그인 셸을 통해): Claude Code
  `claude -p --output-format stream-json --allowedTools mcp__aiverse`와 Codex CLI `codex exec --full-auto`. 작업은 stdin으로 전달되고
  작업 디렉터리는 프로젝트 폴더입니다. CLI는 PATH, 일반적인 설치 위치, 사용자의 셸, 데스크톱 앱 내부(Claude Code는
  `~/Library/Application Support/Claude/claude-code/<version>/…/claude.app`, Codex는 `ChatGPT.app/Contents/Resources/codex-cli`)에서
  찾습니다. 진행 상황은 활동 피드(Agent 창), 상태 표시줄, 마법사 페이지(중지 버튼)에 표시되고, 전체 로그는
  `logs/wizard-agent-<시각>.log`에 남습니다.
- Cursor, Windsurf, Claude Desktop: "에이전트용 작업 복사"로 작업을 클립보드에 넣고 클라이언트에 붙여 넣습니다.
- 에디터에 API 키가 필요 없습니다: 에이전트는 사용자 자신의 클라이언트와 계정으로 동작합니다.
- 음성 입력: 아이디어 칸("아이디어" 페이지)과 메타 아이디어 칸("메타" 페이지) 아래의 "말로 입력" — macOS 음성 인식
  (`apps/editor/EditorSpeech_mac.mm`: Speech + AVFoundation, 게임의 첫 번째 언어를 로캘로 사용. 마이크와 음성 인식 권한은 처음 한 번만
  요청됩니다). 텍스트는 칸에 덧붙여집니다. 다른 플랫폼에서는 버튼이 비활성화됩니다.
- 스펙의 `ai`(언어별 텍스트: 캐치프레이즈, 도입문, 타입, 자원, 통화, 업그레이드, 레벨)를 사용하면, 직접 `game_make`를 호출하는 에이전트가
  게임의 이름과 텍스트를 전달할 수 있습니다.

에디터 Game 뷰에서의 튜토리얼: 에디터에는 단계 패널을 표시하는 게임 UI가 없으므로 뷰 스크립트가 `wiz_tut_off`를 보내고, 튜토리얼 맵은 일반
대전으로 플레이됩니다. 패널, 화살표, 명령 필터링은 게임 클라이언트(AiverseGame, 빌드)에서 동작합니다. 에디터가 여는 것은 튜토리얼이 아니라
첫 번째 레벨입니다.

## 검증

`verifyLevels`를 켜면 마법사가 튜토리얼을 직접 통과하고(`initialVars {wiz_tutorial_auto: 1}`: 각 단계의 `auto` 명령은 플레이어와 같은
필터를 거칩니다), 봇으로 몇몇 레벨을 표본 플레이하여 로딩 오류, 스크립트 오류, 끝나지 않은 튜토리얼을 보고합니다. 모든 템플릿에서 수행됨:
ambersands(바위 위에만 건설 가능), planets와 underlord(특수 지형에서의 이동 / 굴착)를 제외하고 모두 끝까지 통과합니다. 마법사는 경고로 이를
알려 줍니다.

테스트: `game_wizard_plan_picks_a_template_by_the_idea`, `game_wizard_makes_a_playable_project`,
`script_command_card_false_hides_the_button`.

## 엔진 변경

스크립트 명령은 `"card": false`를 받습니다(버튼 / 단축키 없음, 뷰 스크립트, HUD 문서, MCP에서 전송). 튜토리얼의 "다음 / 건너뛰기"에
사용됩니다(`modules/rts/RtsScriptCommands.cpp`, 클라이언트와 에디터의 명령 카드).
