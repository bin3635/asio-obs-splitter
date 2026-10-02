# 설치 안내

## 빌드 (직접 빌드하는 경우)

> ※ GitHub Releases에서 사전 빌드된 ZIP 패키지를 다운로드한 경우, 빌드 과정을 건너뛰고 바로 아래 **설치** 단계로 진행하세요.

Release 구성으로 빌드합니다.

```powershell
cmake -B build -A x64
cmake --build build --config Release
```

설치 또는 제거 전에 OBS와 ASIO를 사용하는 게임을 완전히 종료하세요.

## 설치

압축을 푼 폴더(또는 저장소 루트)에서 `install.cmd`를 실행합니다. Windows가 관리자 권한을
요청하면 승인하세요. 스크립트가 다음 작업을 자동으로 처리합니다.

1. 64비트 `regsvr32`로 `ProxyAsio64.dll`을 등록합니다.
2. `DllRegisterServer`를 통해 `HKLM\SOFTWARE\ASIO`에 드라이버를 등록합니다.
3. `obs-asio-splitter.dll`을 검색된 OBS 64비트 플러그인 폴더에 복사합니다.
4. `AsioSplitterTray.exe`를 설치합니다. 시작프로그램에는 등록하지 않습니다.

Proxy ASIO 호스트 연결이 성공하면 트레이 선택기가 자동으로 실행됩니다.
트레이 아이콘을 우클릭하면 등록된 ASIO 드라이버 목록이 표시됩니다. 선택한
드라이버는 다음 ASIO 호스트 초기화부터 사용됩니다. 호스트 연결이 끊기면
트레이 아이콘과 트레이 프로세스가 함께 종료됩니다.

OBS를 기본 경로가 아닌 곳에 설치했다면 다음처럼 경로를 지정합니다.

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\install.ps1 -ObsPath "D:\Apps\obs-studio"
```

`ObsPath`는 `obs-plugins\64bit` 폴더를 포함하는 OBS 설치 경로입니다.

## 제거

`uninstall.cmd`를 관리자 권한으로 실행합니다. 스크립트는
`DllUnregisterServer`를 호출해 이 드라이버의 COM 클래스와 ASIO 등록을
제거한 뒤 OBS 플러그인 DLL과 트레이 앱을 삭제합니다.

OBS 설치 경로가 기본 경로가 아닌 경우:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\uninstall.ps1 -ObsPath "D:\Apps\obs-studio"
```

이 스크립트는 다른 ASIO 드라이버는 삭제하지 않습니다.
