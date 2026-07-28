#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <commdlg.h>
#include <vector>

using namespace std;

// ========== ГЛОБАЛЬНЫЕ КОНТРОЛЫ ==========
HWND g_hComboProcess = NULL;
HWND g_hEditDllPath = NULL;
HWND g_hStatus = NULL;
HWND g_hBtnRefresh = NULL;

// Структура для хранения информации о процессе
struct ProcessInfo {
    wstring name;
    DWORD pid;
};

vector<ProcessInfo> g_processes;

// ========== ПОЛУЧИТЬ СПИСОК ПРОЦЕССОВ ==========
vector<ProcessInfo> GetProcessList() {
    vector<ProcessInfo> result;
    PROCESSENTRY32 entry = { sizeof(PROCESSENTRY32) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snap == INVALID_HANDLE_VALUE) return result;

    if (Process32First(snap, &entry)) {
        do {
            // Пропускаем системные процессы с PID 0 и 4
            if (entry.th32ProcessID != 0 && entry.th32ProcessID != 4) {
                ProcessInfo info;
                info.name = entry.szExeFile;
                info.pid = entry.th32ProcessID;
                result.push_back(info);
            }
        } while (Process32Next(snap, &entry));
    }

    CloseHandle(snap);
    return result;
}

// ========== ОБНОВИТЬ СПИСОК В КОМБОБОКСЕ ==========
void RefreshProcessList() {
    // Очищаем комбобокс
    SendMessageW(g_hComboProcess, CB_RESETCONTENT, 0, 0);

    // Получаем свежий список
    g_processes = GetProcessList();

    // Добавляем процессы в комбобокс
    for (const auto& proc : g_processes) {
        wchar_t item[512];
        wsprintfW(item, L"%s (PID: %d)", proc.name.c_str(), proc.pid);
        SendMessageW(g_hComboProcess, CB_ADDSTRING, 0, (LPARAM)item);
    }

    // Выбираем первый процесс, если есть
    if (!g_processes.empty()) {
        SendMessageW(g_hComboProcess, CB_SETCURSEL, 0, 0);
    }

    SetWindowTextW(g_hStatus, L"The list has been updated");
}

// ========== ПОЛУЧИТЬ PID ВЫБРАННОГО ПРОЦЕССА ==========
DWORD GetSelectedProcessId() {
    int index = (int)SendMessageW(g_hComboProcess, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR || index >= (int)g_processes.size()) {
        return 0;
    }
    return g_processes[index].pid;
}

// ========== ИНЖЕКТ ==========
bool InjectDLL(DWORD pid, const char* dllPath) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        SetWindowTextW(g_hStatus, L"Error (need an admin)");
        return false;
    }

    size_t len = strlen(dllPath) + 1;
    void* remoteMem = VirtualAllocEx(hProcess, NULL, len, MEM_COMMIT, PAGE_READWRITE);
    if (!remoteMem) {
        CloseHandle(hProcess);
        SetWindowTextW(g_hStatus, L"Error VirtualAllocEx");
        return false;
    }

    if (!WriteProcessMemory(hProcess, remoteMem, dllPath, len, NULL)) {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        SetWindowTextW(g_hStatus, L"Error WriteProcessMemory");
        return false;
    }

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC loadLib = GetProcAddress(k32, "LoadLibraryA");

    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
        (LPTHREAD_START_ROUTINE)loadLib, remoteMem, 0, NULL);

    if (!hThread) {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        SetWindowTextW(g_hStatus, L"Error CreateRemoteThread");
        return false;
    }

    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return true;
}

// ========== ДИАЛОГ ВЫБОРА ФАЙЛА ==========
void BrowseDLL() {
    OPENFILENAMEW ofn = { 0 };
    wchar_t fileName[MAX_PATH] = { 0 };

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = L"DLL Files\0*.dll\0All Files\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileNameW(&ofn)) {
        SetWindowTextW(g_hEditDllPath, fileName);
    }
}

// ========== ОБРАБОТЧИК КНОПКИ INJECT ==========
void OnInject() {
    wchar_t dllPathW[1024] = { 0 };
    char dllPathA[1024] = { 0 };

    GetWindowTextW(g_hEditDllPath, dllPathW, 1024);

    DWORD pid = GetSelectedProcessId();
    if (pid == 0) {
        SetWindowTextW(g_hStatus, L"choose a process!");
        return;
    }

    if (dllPathW[0] == L'\0') {
        SetWindowTextW(g_hStatus, L"Specify the DLL path!");
        return;
    }

    WideCharToMultiByte(CP_ACP, 0, dllPathW, -1, dllPathA, 1024, NULL, NULL);

    wchar_t status[512];
    wsprintfW(status, L"We will inject intoPID: %d ...", pid);
    SetWindowTextW(g_hStatus, status);

    if (InjectDLL(pid, dllPathA)) {
        SetWindowTextW(g_hStatus, L"[+] Successfully injected!");
    }
}

// ========== ОКОННАЯ ФУНКЦИЯ ==========
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // Шрифт
        HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        // ===== РЯД 1: Процесс (ComboBox + кнопка Refresh) =====
        CreateWindowW(L"STATIC", L"Process:",
            WS_CHILD | WS_VISIBLE,
            20, 20, 70, 25, hWnd, NULL, NULL, NULL);

        // Выпадающий список процессов
        g_hComboProcess = CreateWindowW(L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
            100, 20, 200, 200, hWnd, (HMENU)1, NULL, NULL);

        // Кнопка "Обновить" (ID = 2)
        g_hBtnRefresh = CreateWindowW(L"BUTTON", L"Update",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            310, 19, 80, 27, hWnd, (HMENU)2, NULL, NULL);

        // ===== РЯД 2: Путь к DLL + кнопка Обзор =====
        CreateWindowW(L"STATIC", L"DLL path:",
            WS_CHILD | WS_VISIBLE,
            20, 60, 70, 25, hWnd, NULL, NULL, NULL);

        g_hEditDllPath = CreateWindowW(L"EDIT", L"C:\\my.dll",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            100, 60, 190, 25, hWnd, NULL, NULL, NULL);

        // Кнопка "Обзор..." (ID = 3)
        CreateWindowW(L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            300, 59, 80, 27, hWnd, (HMENU)3, NULL, NULL);

        // ===== РЯД 3: Кнопка INJECT =====
        CreateWindowW(L"BUTTON", L"INJECT",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            100, 105, 120, 40, hWnd, (HMENU)4, NULL, NULL);

        // ===== РЯД 4: Статус =====
        g_hStatus = CreateWindowW(L"STATIC", L"Ready",
            WS_CHILD | WS_VISIBLE | SS_SUNKEN,
            20, 165, 390, 30, hWnd, NULL, NULL, NULL);

        // Применяем шрифт
        EnumChildWindows(hWnd, [](HWND child, LPARAM font) {
            SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE);
            return TRUE;
            }, (LPARAM)hFont);

        // Загружаем список процессов при запуске
        RefreshProcessList();
        break;
    }

    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case 1:  // ComboBox (ничего не делаем)
            break;
        case 2:  // Кнопка "Обновить"
            RefreshProcessList();
            break;
        case 3:  // Кнопка "Обзор..."
            BrowseDLL();
            break;
        case 4:  // Кнопка "INJECT"
            OnInject();
            break;
        }
        break;
    }

    case WM_DESTROY: {
        PostQuitMessage(0);
        break;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ========== ТОЧКА ВХОДА ==========
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"InjectorWindow";

    if (!RegisterClassW(&wc)) {
        MessageBoxW(NULL, L"Window registration error", L"Error", MB_OK);
        return 1;
    }

    HWND hWnd = CreateWindowW(L"InjectorWindow", L"DLL Injector v2.0",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 460, 250,
        NULL, NULL, hInst, NULL);

    if (!hWnd) {
        MessageBoxW(NULL, L"Window registration error", L"Error", MB_OK);
        return 1;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return msg.wParam;
}