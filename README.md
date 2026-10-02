# ASIO OBS Splitter

> **ASIO 오디오를 OBS Studio로 다이렉트 복제·송출하는 프록시 드라이버 및 플러그인**

[![Language](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Platform](https://img.shields.io/badge/Platform-Windows%2064--bit-lightgrey.svg)](https://www.microsoft.com/windows)
[![OBS Studio](https://img.shields.io/badge/OBS%20Studio-Plugin-purple.svg)](https://obsproject.com)

## 📌 개요 (Overview)

일반적으로 게임(특히 EZ2ON, DJMAX 등 리듬게임)이나 DAW(Cubase, Ableton, FL Studio 등)에서 ASIO 독점 모드(Exclusive Mode)를 사용할 경우, Windows 오디오 믹서를 거치지 않고 오디오 인터페이스로 오디오를 직접 출력하기 때문에 일반적인 데스크톱 오디오 캡처나 스테레오 믹스 방식으로는 **OBS Studio에서 게임 소리를 캡처할 수 없는 문제**가 발생합니다.

**ASIO OBS Splitter**는 이 문제를 해결하기 위해 고안되었습니다.
1. 게임/DAW에는 일반 ASIO 드라이버인 `Proxy ASIO (OBS Splitter)`로 인식됩니다.
2. 프록시 드라이버가 실제 하드웨어 오디오 인터페이스(Audient, Focusrite, Yamaha, RME 등)로 오디오를 그대로 전달합니다.
3. 동시에 Windows 공유 메모리(IPC 링 버퍼)를 통해 오디오 스트림(Planar Float32)을 **OBS Studio 전용 플러그인으로 실시간 복제**하여, 추가적인 가상 오디오 케이블(Voicemeeter 등) 설정이나 복잡한 라우팅 없이 OBS에서 원음 그대로 캡처 및 방송/녹화할 수 있습니다.

## 🚀 주요 특징 (Key Features)

- **⚡ 초저지연 무손실 IPC 오디오 복제**
  - Windows 공유 메모리 기반의 Lock-Free/Atomic 슬롯 링 버퍼와 커널 동기화 이벤트(`DataReady`)를 활용하여 버퍼 지연이나 음질 손실 없이 실시간 전송합니다.
- **🎧 투명한 하드웨어 패스스루**
  - 실제 하드웨어 ASIO 드라이버의 샘플 레이트, 버퍼 크기, 채널 구성을 그대로 전달하여 리듬게임의 판정이나 연주 반응성에 영향을 주지 않습니다.
- **🖥️ 스마트 트레이 컨트롤러 (`AsioSplitterTray`)**
  - ASIO 호스트(게임/DAW)가 프록시를 로드하면 시스템 트레이 아이콘이 자동으로 실행됩니다.
  - 트레이 아이콘 우클릭으로 현재 PC에 설치된 실제 ASIO 드라이버 목록 중 원하는 하드웨어를 선택하거나, 해당 드라이버의 전용 제어판(Control Panel)을 즉시 열 수 있습니다.
  - 호스트 프로그램이 종료되면 트레이 프로세스도 리소스를 남기지 않고 자동으로 종료됩니다.
- **🔌 전용 OBS Studio 플러그인 (`obs-asio-splitter`)**
  - OBS 소스 목록에서 `ASIO Proxy Capture`를 추가하기만 하면 별도의 오디오 설정 없이 게임 소리가 OBS 오디오 믹서로 입력됩니다.
  - Planar Float32 오디오 버스를 통해 OBS 내부 오디오 파이프라인과 완벽하게 호환됩니다.

## 📂 프로젝트 구조 (Project Structure)

```text
asio-obs-splitter/
├── common/                     # 공통 헤더 및 라이브러리
│   └── include/
│       ├── asio.h              # Steinberg ASIO SDK 명세
│       ├── asio_driver_config.h# 타깃 드라이버 레지스트리 검색 및 설정
│       ├── asio_ipc_protocol.h # 공유 메모리 링 버퍼 레이아웃 및 IPC 프로토콜
│       └── ...
├── proxy-asio/                 # 프록시 ASIO COM DLL (ProxyAsio64.dll)
│   ├── src/
│   │   ├── dllmain.cpp         # COM 클래스 팩토리 및 레지스트리 등록
│   │   ├── proxy_asio.cpp      # IASIO 인터페이스 프록시 구현
│   │   ├── ipc_sender.cpp      # 공유 메모리 버퍼 송신 구현
│   │   └── asiolist.cpp        # 설치된 ASIO 드라이버 열거
│   └── proxy_asio.def
├── obs-plugin/                 # OBS Studio 전용 플러그인 (obs-asio-splitter.dll)
│   └── src/
│       ├── plugin_main.cpp     # OBS 소스 모듈 등록 ('ASIO Proxy Capture')
│       └── ipc_receiver.cpp    # 공유 메모리 오디오 수신 워커
├── tray/                       # 시스템 트레이 선택기 UI (AsioSplitterTray.exe)
│   └── main.cpp                # 트레이 아이콘 메뉴 및 드라이버 제어판 호출
├── deps/                       # 빌드 의존성
│   └── obs-studio/lib/obs.lib  # OBS Studio 링크용 라이브러리 (obs.dll에서 사전 생성)
├── install.cmd / install.ps1   # 간편 설치 스크립트 (관리자 권한)
├── uninstall.cmd / uninstall.ps1 # 제거 스크립트
├── CMakeLists.txt              # 전체 빌드 구성
└── INSTALL.md                  # 설치 상세 가이드
```

## ⚙️ 요구 사양 (Prerequisites)

- **OS**: Windows 10 / 11 (64비트 전용)
- **컴파일러**: Visual Studio 2022 (MSVC v143 이상, C++20 표준 지원)
- **빌드 도구**: CMake 3.20 이상
- **OBS Studio**: 64비트 버전 (30.x 이상 권장)
- **오디오 인터페이스**: 공식 ASIO 드라이버를 지원하는 오디오 인터페이스

## 🔨 직접 빌드하기 (Build Guide)

1. 저장소를 클론합니다.
   ```powershell
   git clone https://github.com/bin3635/asio-obs-splitter.git
   cd asio-obs-splitter
   ```

2. CMake 구성 생성 (Visual Studio 2022 x64):
   ```powershell
   cmake -B build -A x64
   ```
   > ※ `obs-plugin` 빌드 시 CMake의 `FetchContent`를 통해 OBS Studio 헤더가 다운로드됩니다. 링크에 필요한 64비트 `obs.lib`은 공식 소스 아카이브에 포함되어 있지 않아 `obs.dll`로부터 직접 임포트 라이브러리를 생성하여 저장소(`deps/obs-studio/lib/obs.lib`)에 포함해 두었습니다. 따라서 별도의 OBS 전체 소스 빌드 없이 바로 컴파일할 수 있습니다.

3. Release 구성으로 빌드:
   ```powershell
   cmake --build build --config Release
   ```

빌드가 완료되면 다음 위치에 바이너리가 생성됩니다:
- `build/proxy-asio/Release/ProxyAsio64.dll`
- `build/obs-plugin/Release/obs-asio-splitter.dll`
- `build/tray/Release/AsioSplitterTray.exe`

## 📦 설치 및 사용 방법 (Installation & Usage)

> [!IMPORTANT]
> 설치나 제거를 진행하기 전에 **OBS Studio**와 **ASIO를 사용하는 게임/DAW**를 완전히 종료하세요.

### 1. 설치

#### 방법 A. 배포 패키지 사용 (권장)
1. [Releases](https://github.com/bin3635/asio-obs-splitter/releases) 페이지에서 최신 `asio-obs-splitter-v*.zip`을 다운로드하고 압축을 풉니다.
2. 압축 해제한 폴더 내의 **`install.cmd`**를 실행합니다 (UAC 관리자 권한 승인).

#### 방법 B. 소스 코드 직접 빌드 후 설치
1. 위의 **🔨 직접 빌드하기** 가이드를 따라 빌드를 완료합니다.
2. 저장소 루트에서 **`install.cmd`**를 실행합니다.

> 스크립트가 다음 작업을 자동으로 처리합니다:
> 1. `ProxyAsio64.dll`을 Windows 64비트 COM 및 ASIO 드라이버로 등록 (`regsvr32`)
> 2. `obs-asio-splitter.dll`을 OBS 플러그인 폴더(`C:\Program Files\obs-studio\obs-plugins\64bit`)에 복사
> 3. `AsioSplitterTray.exe`를 `C:\Program Files\ASIO OBS Splitter\`에 설치
>
> **OBS를 기본 경로 외 다른 경로에 설치한 경우:**
> ```powershell
> powershell.exe -ExecutionPolicy Bypass -File .\install.ps1 -ObsPath "D:\Apps\obs-studio"
> ```

### 2. OBS Studio 설정
1. OBS Studio를 실행합니다.
2. 오디오를 입력받을 씬(Scene)의 **소스 목록**에서 `+` 버튼을 누릅니다.
3. 목록에서 `ASIO Proxy Capture`를 선택하여 추가합니다.
4. 추가 후 OBS 오디오 믹서에 새 소스가 등록되었는지 확인합니다.

### 3. 게임 / DAW 설정
1. 게임(예: EZ2ON) 또는 DAW의 오디오 설정 메뉴로 이동합니다.
2. 출력 드라이버로 `Proxy ASIO (OBS Splitter)`를 선택합니다.
3. 드라이버가 초기화되면 작업 표시줄 트레이 영역에 **ASIO Splitter 트레이 아이콘**이 나타납니다.
4. 트레이 아이콘을 **우클릭**하여 실제 소리를 재생할 오디오 인터페이스 드라이버(예: Audient USB Audio ASIO Driver 등)를 체크합니다.
5. 이제 헤드폰으로는 지연 없는 소리가 재생되고, 동시에 OBS Studio의 `ASIO Proxy Capture` 볼륨 미터가 움직이는 것을 확인할 수 있습니다.

## 🗑️ 제거 방법 (Uninstallation)

압축을 푼 폴더(또는 저장소 루트)의 `uninstall.cmd`를 관리자 권한으로 실행합니다.

```powershell
.\uninstall.cmd
```
- 등록된 Proxy ASIO COM 드라이버 레지스트리가 해제됩니다.
- OBS 플러그인 DLL 및 트레이 프로그램 파일이 깔끔하게 삭제됩니다.
- 기존에 설치되어 있던 하드웨어 오디오 인터페이스 드라이버 정보는 전혀 손상되지 않습니다.

> **OBS를 기본 경로 외 다른 경로에 설치했던 경우:**
> ```powershell
> powershell.exe -ExecutionPolicy Bypass -File .\uninstall.ps1 -ObsPath "D:\Apps\obs-studio"
> ```

## 💡 주의 사항 및 팁 (Notes & Troubleshooting)

- **64비트 전용**: 이 프로젝트는 64비트 전용으로 설계되었습니다. 32비트 전용 구형 게임이나 호스트는 지원하지 않습니다.
- **샘플 레이트 일치**: 최상의 품질과 잡음 방지를 위해 하드웨어 ASIO 드라이버와 OBS Studio의 오디오 설정(기본 48kHz 권장)의 샘플 레이트를 일치시키는 것이 좋습니다.
- **트레이 아이콘 미표시**: 트레이 프로그램은 상시 켜져 있는 백그라운드 상주 프로그램이 아니며, **Proxy ASIO를 사용하는 게임/호스트가 실행될 때만 나타나고 호스트 종료 시 같이 종료**됩니다.

## 📄 라이선스 (License)

- ASIO는 Steinberg Media Technologies GmbH의 등록 상표 및 소프트웨어 기술입니다.
- OBS Studio 관련 코드는 GNU General Public License (GPL) v2 이상을 준수합니다.
