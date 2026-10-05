/*
 * Jass 插件静态文件更新工具
 * 纯 Win32 C 程序，静态链接，无需额外运行库
 *
 * Release 版本下载源: https://raw.githubusercontent.com/naichabaobao/jass/master/static/
 * Debug   版本下载源: https://raw.githubusercontent.com/jzy-chitong56/jass/master/static/
 *
 * Debug 版本额外特性：弹框中显示可复制的运行日志
 */

#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "kernel32.lib")

static const wchar_t *g_files[] = {
    L"AIScripts.ai", L"Cheats.j", L"DzAPI.j", L"InitCheats.j",
    L"blizzard.j", L"common.ai", L"common.j", L"numbers.jass",
    L"presets.jass", L"strings.jass", L"war3map.j",
};
static const int g_fileCount = (int)(sizeof(g_files) / sizeof(g_files[0]));

#ifdef JASS_DEBUG
#define GITHUB_USER L"jzy-chitong56"
#else
#define GITHUB_USER L"naichabaobao"
#endif
#define GITHUB_REPO  L"jass"
#define GITHUB_BRANCH L"master"
#define GITHUB_SUBDIR L"static"

typedef enum {
    ERR_NONE = 0,
    ERR_PLUGIN_NOT_FOUND,
    ERR_DOWNLOAD_FAILED,
    ERR_WRITE_FAILED,
    ERR_REPLACE_FAILED,
} UpdateError;

/* ---------- 运行日志（仅 Debug 版本） ---------- */
#ifdef JASS_DEBUG
static wchar_t g_logBuf[32768];
static int g_logLen = 0;

static void Log(const wchar_t *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    wchar_t line[1024];
    int n = vswprintf(line, 1024, fmt, args);
    va_end(args);
    if (n <= 0) return;
    if (g_logLen + n + 2 < (int)sizeof(g_logBuf) / sizeof(wchar_t)) {
        memcpy(g_logBuf + g_logLen, line, n * sizeof(wchar_t));
        g_logLen += n;
        g_logBuf[g_logLen++] = L'\r';
        g_logBuf[g_logLen++] = L'\n';
        g_logBuf[g_logLen] = 0;
    }
}
static void ClearLog(void) { g_logLen = 0; g_logBuf[0] = 0; }
#else
#define Log(fmt, ...)   ((void)0)
#define ClearLog()      ((void)0)
#endif

/* ---------- 自定义弹窗 ---------- */
static HWND g_msgBoxHwnd = NULL;
static volatile BOOL g_msgBoxClosed = FALSE;

static LRESULT CALLBACK MsgBoxWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == 1001 && HIWORD(wParam) == BN_CLICKED) {
            g_msgBoxClosed = TRUE;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
    case WM_DESTROY:
        g_msgBoxClosed = TRUE;
        if (msg == WM_CLOSE) DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ShowCustomMsgBox(const wchar_t *title, const wchar_t *message, const wchar_t *btnText)
{
    HWND hwndOwner = GetDesktopWindow();
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwndOwner, GWLP_HINSTANCE);

    static BOOL classRegistered = FALSE;
    if (!classRegistered) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = MsgBoxWndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = L"JassMsgBox";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        classRegistered = TRUE;
    }

    g_msgBoxClosed = FALSE;

#ifdef JASS_DEBUG
    int dlgW = 520, dlgH = 420;
#else
    int dlgW = 360, dlgH = 180;
#endif

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"JassMsgBox", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, dlgW, dlgH,
        hwndOwner, NULL, hInst, NULL);
    if (!dlg) {
        MessageBoxW(hwndOwner, message, title, MB_OK | MB_ICONINFORMATION);
        return;
    }
    g_msgBoxHwnd = dlg;

    CreateWindowExW(0, L"STATIC", message,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, 15, dlgW - 40, 30, dlg, NULL, hInst, NULL);

#ifdef JASS_DEBUG
    CreateWindowExW(0, L"STATIC", L"\u8fd0\u884c\u65e5\u5fd7\uff08\u53ef\u9009\u4e2d\u590d\u5236\uff09\uff1a",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, 50, dlgW - 40, 20, dlg, NULL, hInst, NULL);

    HWND hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_logBuf,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        20, 70, dlgW - 40, dlgH - 140, dlg, NULL, hInst, NULL);
    SendMessageW(hLog, WM_SETFONT, (WPARAM)GetStockObject(ANSI_FIXED_FONT), TRUE);
    int btnY = dlgH - 70;
#else
    int btnY = 95;
#endif

    HWND hBtn = CreateWindowExW(0, L"BUTTON", btnText,
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        (dlgW - 120) / 2, btnY, 120, 32, dlg, (HMENU)1001, hInst, NULL);
    SendMessageW(hBtn, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    RECT rcOwner, rcDlg;
    GetWindowRect(hwndOwner, &rcOwner);
    GetWindowRect(dlg, &rcDlg);
    int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - (rcDlg.right - rcDlg.left)) / 2;
    int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - (rcDlg.bottom - rcDlg.top)) / 2;
    SetWindowPos(dlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    EnableWindow(hwndOwner, FALSE);
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);

    MSG msg;
    while (!g_msgBoxClosed && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsWindow(dlg) || !IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (g_msgBoxClosed) break;
    }

    EnableWindow(hwndOwner, TRUE);
    SetForegroundWindow(hwndOwner);
    g_msgBoxHwnd = NULL;
}

/* ---------- 网络下载 ---------- */
static BYTE *DownloadFile(const wchar_t *fileName, DWORD *outSize)
{
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    BYTE *result = NULL;
    DWORD totalSize = 0, cap = 65536;
    BYTE *buf = (BYTE *)malloc(cap);
    if (!buf) { Log(L"[\u4e0b\u8f7d] \u5185\u5b58\u5206\u914d\u5931\u8d25: %s", fileName); return NULL; }

    wchar_t urlPath[512];
    swprintf_s(urlPath, 512, L"/%s/%s/%s/%s", GITHUB_REPO, GITHUB_BRANCH, GITHUB_SUBDIR, fileName);
    Log(L"[\u4e0b\u8f7d] https://raw.githubusercontent.com%s", urlPath);

    hSession = WinHttpOpen(L"JassUpdater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { Log(L"[\u4e0b\u8f7d] WinHttpOpen \u5931\u8d25"); goto done; }
    hConnect = WinHttpConnect(hSession, L"raw.githubusercontent.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { Log(L"[\u4e0b\u8f7d] WinHttpConnect \u5931\u8d25"); goto done; }
    hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { Log(L"[\u4e0b\u8f7d] WinHttpOpenRequest \u5931\u8d25"); goto done; }
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        Log(L"[\u4e0b\u8f7d] SendRequest \u5931\u8d25 err=%lu", GetLastError()); goto done;
    }
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        Log(L"[\u4e0b\u8f7d] ReceiveResponse \u5931\u8d25 err=%lu", GetLastError()); goto done;
    }
    DWORD statusCode = 0, sz = sizeof(statusCode);
    if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &statusCode, &sz, NULL)) {
        Log(L"[\u4e0b\u8f7d] \u83b7\u53d6\u72b6\u6001\u7801\u5931\u8d25 err=%lu", GetLastError()); goto done;
    }
    Log(L"[\u4e0b\u8f7d] HTTP %lu", statusCode);
    if (statusCode != 200) goto done;

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;
        if (totalSize + avail > cap) {
            while (totalSize + avail > cap) cap *= 2;
            BYTE *nb = (BYTE *)realloc(buf, cap);
            if (!nb) { Log(L"[\u4e0b\u8f7d] \u5185\u5b58\u6269\u5bb9\u5931\u8d25"); goto done; }
            buf = nb;
        }
        DWORD read = 0;
        if (!WinHttpReadData(hRequest, buf + totalSize, avail, &read) || read == 0) break;
        totalSize += read;
    }
    Log(L"[\u4e0b\u8f7d] \u5b8c\u6210 %s %luB", fileName, totalSize);
    if (totalSize > 0) { result = buf; *outSize = totalSize; buf = NULL; }

done:
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    free(buf);
    return result;
}

static BOOL wcsStartsWithCI(const wchar_t *str, const wchar_t *prefix)
{
    while (*prefix) {
        wchar_t a = *str, b = *prefix;
        if (a >= L'A' && a <= L'Z') a += 32;
        if (b >= L'A' && b <= L'Z') b += 32;
        if (a != b) return FALSE;
        str++; prefix++;
    }
    return TRUE;
}

static void ScanUserProfile(const wchar_t *profileDir, wchar_t outList[][MAX_PATH], int maxCount, int *count)
{
    const wchar_t *vscodeDirs[] = { L".vscode", L".vscode-insiders" };
    for (int v = 0; v < 2; v++) {
        wchar_t extPath[MAX_PATH];
        swprintf_s(extPath, MAX_PATH, L"%s\\%s\\extensions\\*", profileDir, vscodeDirs[v]);
        Log(L"[\u626b\u63cf] %s", extPath);

        WIN32_FIND_DATAW fdExt;
        HANDLE hExt = FindFirstFileW(extPath, &fdExt);
        if (hExt == INVALID_HANDLE_VALUE) { Log(L"[\u626b\u63cf] \u672a\u627e\u5230: %s", extPath); continue; }

        do {
            if (!(fdExt.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            wchar_t staticPath[MAX_PATH];
            swprintf_s(staticPath, MAX_PATH, L"%s\\%s\\extensions\\%s\\static", profileDir, vscodeDirs[v], fdExt.cFileName);
            DWORD attr = GetFileAttributesW(staticPath);
            if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) continue;

            Log(L"[\u626b\u63cf] \u53d1\u73b0: %s", fdExt.cFileName);
            BOOL isJass = wcsStartsWithCI(fdExt.cFileName, L"jass.");
            if (!isJass) {
                wchar_t tf[MAX_PATH];
                swprintf_s(tf, MAX_PATH, L"%s\\common.j", staticPath);
                if (GetFileAttributesW(tf) != INVALID_FILE_ATTRIBUTES) { isJass = TRUE; Log(L"[\u626b\u63cf] common.j\u5339\u914d"); }
                else { swprintf_s(tf, MAX_PATH, L"%s\\war3map.j", staticPath); if (GetFileAttributesW(tf) != INVALID_FILE_ATTRIBUTES) { isJass = TRUE; Log(L"[\u626b\u63cf] war3map.j\u5339\u914d"); } }
            }
            if (isJass && *count < maxCount) {
                wcscpy_s(outList[*count], MAX_PATH, staticPath);
                (*count)++;
                Log(L"[\u626b\u63cf] \u547d\u4e2d: %s", staticPath);
            }
        } while (FindNextFileW(hExt, &fdExt));
        FindClose(hExt);
    }
}

static int CollectUserDirs(wchar_t dirs[][MAX_PATH], int maxCount)
{
    int count = 0;
    wchar_t seen[64][MAX_PATH];
    int seenCount = 0;
    #define ADD_DIR(p) do { \
        int _i; \
        for (_i = 0; _i < seenCount; _i++) if (_wcsicmp(seen[_i], (p)) == 0) break; \
        if (_i == seenCount && seenCount < 64) wcscpy_s(seen[seenCount++], MAX_PATH, (p)); \
    } while (0)

    wchar_t userProfile[MAX_PATH];
    if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH) > 0) {
        Log(L"[\u7528\u6237\u76ee\u5f55] USERPROFILE=%s", userProfile);
        ADD_DIR(userProfile);
    } else { Log(L"[\u7528\u6237\u76ee\u5f55] USERPROFILE \u672a\u627e\u5230"); }

    wchar_t sysDrive[MAX_PATH];
    if (GetEnvironmentVariableW(L"SystemDrive", sysDrive, MAX_PATH) > 0) {
        Log(L"[\u7528\u6237\u76ee\u5f55] SystemDrive=%s", sysDrive);
        wchar_t up[MAX_PATH]; swprintf_s(up, MAX_PATH, L"%s\\Users", sysDrive);
        wchar_t sp[MAX_PATH]; swprintf_s(sp, MAX_PATH, L"%s\\*", up);
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(sp, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
                wchar_t f[MAX_PATH]; swprintf_s(f, MAX_PATH, L"%s\\%s", up, fd.cFileName);
                Log(L"[\u7528\u6237\u76ee\u5f55] %s", f); ADD_DIR(f);
            } while (FindNextFileW(h, &fd)); FindClose(h);
        } else { Log(L"[\u7528\u6237\u76ee\u5f55] \u65e0\u6cd5\u679a\u4e3e: %s", sp); }
    } else { Log(L"[\u7528\u6237\u76ee\u5f55] SystemDrive \u672a\u627e\u5230"); }

    wchar_t winDir[MAX_PATH];
    if (GetWindowsDirectoryW(winDir, MAX_PATH)) {
        Log(L"[\u7528\u6237\u76ee\u5f55] WindowsDir=%s", winDir);
        wchar_t drv[4] = { winDir[0], L':', L'\\', 0 };
        wchar_t up[MAX_PATH]; swprintf_s(up, MAX_PATH, L"%sUsers", drv);
        wchar_t sp[MAX_PATH]; swprintf_s(sp, MAX_PATH, L"%s\\*", up);
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(sp, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
                wchar_t f[MAX_PATH]; swprintf_s(f, MAX_PATH, L"%s\\%s", up, fd.cFileName); ADD_DIR(f);
            } while (FindNextFileW(h, &fd)); FindClose(h);
        }
    }

    { const wchar_t *cu = L"C:\\Users"; wchar_t sp[MAX_PATH]; swprintf_s(sp, MAX_PATH, L"%s\\*", cu);
      WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(sp, &fd);
      if (h != INVALID_HANDLE_VALUE) {
        do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            wchar_t f[MAX_PATH]; swprintf_s(f, MAX_PATH, L"%s\\%s", cu, fd.cFileName); ADD_DIR(f);
        } while (FindNextFileW(h, &fd)); FindClose(h); } }

    #undef ADD_DIR
    Log(L"[\u7528\u6237\u76ee\u5f55] \u5171 %d \u4e2a", seenCount);
    for (int i = 0; i < seenCount && count < maxCount; i++) wcscpy_s(dirs[count++], MAX_PATH, seen[i]);
    return count;
}

static int FindPluginStaticDirs(wchar_t outList[][MAX_PATH], int maxCount)
{
    int count = 0;
    wchar_t userDirs[64][MAX_PATH];
    int userCount = CollectUserDirs(userDirs, 64);
    for (int i = 0; i < userCount; i++) ScanUserProfile(userDirs[i], outList, maxCount, &count);
    Log(L"[\u7ed3\u679c] \u627e\u5230 %d \u4e2a\u63d2\u4ef6\u76ee\u5f55", count);
    return count;
}

static UpdateError DoUpdate(void)
{
    ClearLog();
    Log(L"==== Jass \u66f4\u65b0\u5de5\u5175 \u8c03\u8bd5\u65e5\u5fd7 ====");

    wchar_t dirs[64][MAX_PATH];
    int dirCount = FindPluginStaticDirs(dirs, 64);
    if (dirCount <= 0) { Log(L"[\u9519\u8bef] \u672a\u627e\u5230\u63d2\u4ef6"); return ERR_PLUGIN_NOT_FOUND; }

    for (int d = 0; d < dirCount; d++) {
        Log(L"[\u66f4\u65b0] %s", dirs[d]);
        for (int f = 0; f < g_fileCount; f++) {
            DWORD dataSize = 0;
            BYTE *data = DownloadFile(g_files[f], &dataSize);
            if (!data || dataSize == 0) { free(data); Log(L"[\u9519\u8bef] \u4e0b\u8f7d\u5931\u8d25: %s", g_files[f]); return ERR_DOWNLOAD_FAILED; }
            wchar_t fullPath[MAX_PATH];
            swprintf_s(fullPath, MAX_PATH, L"%s\\%s", dirs[d], g_files[f]);
            HANDLE hFile = CreateFileW(fullPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile == INVALID_HANDLE_VALUE) {
                free(data); DWORD err = GetLastError();
                Log(L"[\u9519\u8bef] \u521b\u5efa\u6587\u4ef6\u5931\u8d25 %s err=%lu", fullPath, err);
                if (err == ERROR_ACCESS_DENIED || err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION) return ERR_REPLACE_FAILED;
                return ERR_WRITE_FAILED;
            }
            DWORD written = 0;
            BOOL ok = WriteFile(hFile, data, dataSize, &written, NULL);
            CloseHandle(hFile); free(data);
            if (!ok || written != dataSize) { Log(L"[\u9519\u8bef] \u5199\u5165\u5931\u8d25 %s", fullPath); return ERR_WRITE_FAILED; }
            Log(L"[\u66f4\u65b0] %s %luB", g_files[f], dataSize);
        }
    }
    Log(L"[\u5b8c\u6210] \u5168\u90e8\u6210\u529f");
    return ERR_NONE;
}

static HWND g_hBtn = NULL;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        g_hBtn = CreateWindowExW(0, L"BUTTON", L"\u5f00\u59cb\u66f4\u65b0", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 60, 30, 160, 40, hwnd, (HMENU)100, NULL, NULL);
        { HFONT hf = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei"); SendMessageW(g_hBtn, WM_SETFONT, (WPARAM)hf, TRUE); }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == 100 && HIWORD(wParam) == BN_CLICKED) {
            EnableWindow(g_hBtn, FALSE);
            UpdateError err = DoUpdate();
            EnableWindow(g_hBtn, TRUE);
            if (err == ERR_NONE) {
                ShowCustomMsgBox(L"\u66f4\u65b0\u6210\u529f", L"\u6240\u6709\u63d2\u4ef6\u9759\u6001\u6587\u4ef6\u5df2\u66f4\u65b0\u5b8c\u6bd5\uff01", L"\u5173\u95ed\u5c0f\u7a0b\u5e8f");
            } else {
                const wchar_t *reason = L"\u672a\u77e5\u9519\u8bef";
                switch (err) {
                    case ERR_PLUGIN_NOT_FOUND: reason = L"\u627e\u4e0d\u5230\u63d2\u4ef6\uff0c\u53ef\u80fd\u672a\u5b89\u88c5"; break;
                    case ERR_DOWNLOAD_FAILED: reason = L"\u4e0b\u8f7d\u6587\u4ef6\u5931\u8d25\uff0c\u8bf7\u68c0\u67e5\u7f51\u7edc\u540e\u91cd\u8bd5"; break;
                    case ERR_WRITE_FAILED: reason = L"\u6587\u4ef6\u5199\u5165\u5931\u8d25\uff0c\u8bf7\u68c0\u67e5\u63d2\u4ef6\u5730\u5740\u662f\u5426\u53d7\u4fdd\u62a4\uff0c\u8bf7\u5173\u95ed\u4fdd\u62a4\u540e\u91cd\u8bd5"; break;
                    case ERR_REPLACE_FAILED: reason = L"\u66ff\u6362\u6587\u4ef6\u5931\u8d25"; break;
                }
                ShowCustomMsgBox(L"\u66f4\u65b0\u5931\u8d25", reason, L"\u786e\u5b9a");
            }
        }
        break;
    case WM_DESTROY: PostQuitMessage(0); break;
    default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    (void)hPrev; (void)lpCmd;
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc; wc.hInstance = hInst; wc.lpszClassName = L"JassUpdaterWnd";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"JassUpdaterWnd", L"Jass \u63d2\u4ef6\u66f4\u65b0\u5de5\u5177",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 300, 130, NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, nShow); UpdateWindow(hwnd);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return (int)msg.wParam;
}
