#include <windows.h>
#include <shellapi.h>

#include "asio_driver_config.h"
#include "asio_ipc_protocol.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT MENU_DRIVER_FIRST = 1000;
constexpr UINT MENU_REFRESH = 2000;
constexpr UINT MENU_EXIT = 2001;
constexpr UINT MENU_CONTROL_PANEL = 2002;
constexpr wchar_t WINDOW_CLASS_NAME[] = L"AsioSplitterTrayWindow";

HWND g_window = nullptr;
NOTIFYICONDATAW g_trayIcon{};
std::vector<AsioSplitter::Drivers::DriverInfo> g_drivers;
bool g_trayIconAdded = false;

bool equalClsid(REFCLSID left, REFCLSID right)
{
    return IsEqualCLSID(left, right) != FALSE;
}

void refreshDrivers()
{
    g_drivers = AsioSplitter::Drivers::enumerate();
}

void showDriverMenu()
{
    refreshDrivers();

    HMENU menu = CreatePopupMenu();
    if (!menu)
    {
        return;
    }

    CLSID selected{};
    const bool hasSelection = AsioSplitter::Drivers::readSelected(selected);
    for (std::size_t index = 0; index < g_drivers.size(); ++index)
    {
        MENUITEMINFOW item{sizeof(item)};
        item.fMask = MIIM_ID | MIIM_STATE | MIIM_STRING;
        item.wID = MENU_DRIVER_FIRST + static_cast<UINT>(index);
        item.fState = hasSelection && equalClsid(selected, g_drivers[index].clsid)
            ? MFS_CHECKED
            : MFS_UNCHECKED;
        item.dwTypeData = g_drivers[index].name.data();
        InsertMenuItemW(menu, static_cast<UINT>(index), TRUE, &item);
    }

    if (g_drivers.empty())
    {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, MENU_DRIVER_FIRST, L"등록된 ASIO 드라이버 없음");
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, MENU_CONTROL_PANEL, L"선택한 드라이버 설정 열기");
    AppendMenuW(menu, MF_STRING, MENU_REFRESH, L"새로 고침");
    AppendMenuW(menu, MF_STRING, MENU_EXIT, L"종료");

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(g_window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, g_window, nullptr);
    DestroyMenu(menu);
}

void addTrayIcon()
{
    if (g_trayIconAdded)
    {
        return;
    }

    g_trayIcon = {};
    g_trayIcon.cbSize = sizeof(g_trayIcon);
    g_trayIcon.hWnd = g_window;
    g_trayIcon.uID = 1;
    g_trayIcon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_trayIcon.uCallbackMessage = WM_TRAYICON;
    g_trayIcon.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_trayIcon.szTip, L"ASIO OBS Splitter");
    g_trayIconAdded = Shell_NotifyIconW(NIM_ADD, &g_trayIcon) != FALSE;
}

void removeTrayIcon()
{
    if (g_trayIconAdded)
    {
        Shell_NotifyIconW(NIM_DELETE, &g_trayIcon);
        g_trayIconAdded = false;
    }
}

bool isHostConnected()
{
    HANDLE event = OpenEventA(SYNCHRONIZE, FALSE, AsioSplitter::Ipc::EVENT_HOST_CONNECTED_NAME);
    if (!event)
    {
        return false;
    }

    const bool connected = WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
    CloseHandle(event);
    return connected;
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_TIMER:
        if (isHostConnected())
        {
            addTrayIcon();
        }
        else
        {
            DestroyWindow(window);
        }
        return 0;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP)
        {
            showDriverMenu();
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == MENU_EXIT)
        {
            DestroyWindow(window);
            return 0;
        }
        if (LOWORD(wParam) == MENU_REFRESH)
        {
            return 0;
        }
        if (LOWORD(wParam) == MENU_CONTROL_PANEL)
        {
            HANDLE controlPanelEvent = OpenEventA(
                EVENT_MODIFY_STATE,
                FALSE,
                AsioSplitter::Ipc::EVENT_CONTROL_PANEL_REQUEST_NAME
            );
            if (controlPanelEvent)
            {
                SetEvent(controlPanelEvent);
                CloseHandle(controlPanelEvent);
            }
            return 0;
        }
        if (LOWORD(wParam) >= MENU_DRIVER_FIRST &&
            LOWORD(wParam) < MENU_DRIVER_FIRST + g_drivers.size())
        {
            AsioSplitter::Drivers::writeSelected(
                g_drivers[LOWORD(wParam) - MENU_DRIVER_FIRST].clsid);
            HANDLE selectionEvent = OpenEventA(
                EVENT_MODIFY_STATE,
                FALSE,
                AsioSplitter::Ipc::EVENT_DRIVER_SELECTION_CHANGED_NAME
            );
            if (selectionEvent)
            {
                SetEvent(selectionEvent);
                CloseHandle(selectionEvent);
            }
            return 0;
        }
        break;

    case WM_DESTROY:
        KillTimer(window, 1);
        removeTrayIcon();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"Global\\AsioObsSplitterTray");
    if (!instanceMutex || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (instanceMutex)
        {
            CloseHandle(instanceMutex);
        }
        CoUninitialize();
        return 0;
    }

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = WINDOW_CLASS_NAME;
    RegisterClassW(&windowClass);

    g_window = CreateWindowExW(
        0,
        WINDOW_CLASS_NAME,
        L"ASIO OBS Splitter",
        WS_OVERLAPPED,
        0, 0, 0, 0,
        nullptr,
        nullptr,
        instance,
        nullptr
    );
    if (!g_window)
    {
        CloseHandle(instanceMutex);
        CoUninitialize();
        return 1;
    }

    SetTimer(g_window, 1, 500, nullptr);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    CloseHandle(instanceMutex);
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
