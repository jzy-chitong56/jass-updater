/*
 * Jass 插件静态文件更新工具
 * 纯 Win32 C 程序，静态链接，无需额外运行库
 *
 * Release 版本下载源: https://raw.githubusercontent.com/naichabaobao/jass/master/static/
 * Debug   版本下载源: https://raw.githubusercontent.com/jzy-chitong56/jass/master/static/
 */

#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "kernel32.lib")

/* ---------- 需要更新的文件列表 ---------- */
static const wchar_t *g_files[] = {
    L"AIScripts.ai",
    L"Cheats.j",
    L"DzAPI.j",
    L"InitCheats.j",
    L"blizzard.j",
    L"common.ai",
    L"common.j",
    L"numbers.jass",
    L"presets.jass",
    L"strings.jass",
    L"war3map.j",
};
static const int g_fileCount = (int)(sizeof(g_files) / sizeof(g_files[0]));

/* ---------- 下载源配置 ---------- */
#ifdef JASS_DEBUG
#define GITHUB_USER L"jzy-chitong56"
#else
#define GITHUB_USER L"naichabaobao"
#endif

#define GITHUB_REPO  L"jass"
#define GITHUB_BRANCH L"master"
#define GITHUB_SUBDIR L"static"

/* ---------- 错误原因 ---------- */
typedef enum {
    ERR_NONE = 0,
    ERR_PLUGIN_NOT_FOUND,
    ERR_DOWNLOAD_FAILED,
    ERR_WRITE_FAILED,
    ERR_REPLACE_FAILED,
} UpdateError;

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
        g_msgBoxClosed = TRUE;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_msgBoxClosed = TRUE;
        break;
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

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"JassMsgBox", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 360, 180,
        hwndOwner, NULL, hInst, NULL);
    if (!dlg) {
        MessageBoxW(hwndOwner, message, title, MB_OK | MB_ICONINFORMATION);
        return;
    }
    g_msgBoxHwnd = dlg;

    CreateWindowExW(0, L"STATIC", message,
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        20, 20, 310, 60, dlg, NULL, hInst, NULL);

    HWND hBtn = CreateWindowExW(0, L"BUTTON", btnText,
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        120, 95, 120, 32, dlg, (HMENU)1001, hInst, NULL);
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SendMessageW(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

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
    DWORD totalSize = 0;
    DWORD cap = 65536;
    BYTE *buf = (BYTE *)malloc(cap);
    if (!buf) return NULL;

    wchar_t urlPath[512];
    swprintf_s(urlPath, 512, L"/%s/%s/%s/%s",
        GITHUB_REPO, GITHUB_BRANCH, GITHUB_SUBDIR, fileName);

    hSession = WinHttpOpen(L"JassUpdater/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) goto done;

    hConnect = WinHttpConnect(hSession, L"raw.githubusercontent.com",
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) goto done;

    hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath,
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) goto done;

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) goto done;

    if (!WinHttpReceiveResponse(hRequest, NULL)) goto done;

    DWORD statusCode = 0, sz = sizeof(statusCode);
    if (!WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        NULL, &statusCode, &sz, NULL) || statusCode != 200) {
        goto done;
    }

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;
        if (totalSize + avail > cap) {
            while (totalSize + avail > cap) cap *= 2;
            BYTE *nb = (BYTE *)realloc(buf, cap);
            if (!nb) goto done;
            buf = nb;
        }
        DWORD read = 0;
        if (!WinHttpReadData(hRequest, buf + totalSize, avail, &read) || read == 0) break;
        totalSize += read;
    }

    if (totalSize > 0) {
        result = buf;
        *outSize = totalSize;
        buf = NULL;
    }

done:
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    free(buf);
    return result;
}

/* ---------- 不区分大小写的宽字符串前缀匹配 ---------- */
static BOOL wcsStartsWithCI(const wchar_t *str, const wchar_t *prefix)
{
    while (*prefix) {
        wchar_t a = *str, b = *prefix;
        if (a >= L'A' && a <= L'Z') a += 32;
        if (b >= L'A' && b <= L'Z') b += 32;
        if (a != b) return FALSE;
        str++;
        prefix++;
    }
    return TRUE;
}

/* ---------- 在指定用户目录下查找 jass 插件 ---------- */
static void ScanUserProfile(const wchar_t *profileDir, wchar_t outList[][MAX_PATH],
                            int maxCount, int *count)
{
    const wchar_t *vscodeDirs[] = { L".vscode", L".vscode-insiders" };

    for (int v = 0; v < 2; v++) {
        wchar_t extPath[MAX_PATH];
        swprintf_s(extPath, MAX_PATH, L"%s\\%s\\extensions\\*", profileDir, vscodeDirs[v]);

        WIN32_FIND_DATAW fdExt;
        HANDLE hExt = FindFirstFileW(extPath, &fdExt);
        if (hExt == INVALID_HANDLE_VALUE) continue;

        do {
            if (!(fdExt.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;

            wchar_t staticPath[MAX_PATH];
            swprintf_s(staticPath, MAX_PATH, L"%s\\%s\\extensions\\%s\\static",
                profileDir, vscodeDirs[v], fdExt.cFileName);

            DWORD attr = GetFileAttributesW(staticPath);
            if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) continue;

            BOOL isJass = wcsStartsWithCI(fdExt.cFileName, L"jass.");
            if (!isJass) {
                wchar_t testFile[MAX_PATH];
                swprintf_s(testFile, MAX_PATH, L"%s\\common.j", staticPath);
                if (GetFileAttributesW(testFile) != INVALID_FILE_ATTRIBUTES) {
                    isJass = TRUE;
                } else {
                    swprintf_s(testFile, MAX_PATH, L"%s\\war3map.j", staticPath);
                    if (GetFileAttributesW(testFile) != INVALID_FILE_ATTRIBUTES) {
                        isJass = TRUE;
                    }
                }
            }

            if (isJass && *count < maxCount) {
                wcscpy_s(outList[*count], MAX_PATH, staticPath);
                (*count)++;
            }
        } while (FindNextFileW(hExt, &fdExt));
        FindClose(hExt);
    }
}

/* ---------- 收集所有用户目录 ---------- */
static int CollectUserDirs(wchar_t dirs[][MAX_PATH], int maxCount)
{
    int count = 0;
    wchar_t seen[64][MAX_PATH];
    int seenCount = 0;

    #define ADD_DIR(p) do { \
        int _i; \
        for (_i = 0; _i < seenCount; _i++) { \
            if (_wcsicmp(seen[_i], (p)) == 0) break; \
        } \
        if (_i == seenCount && seenCount < 64) { \
            wcscpy_s(seen[seenCount++], MAX_PATH, (p)); \
        } \
    } while (0)

    /* 1. USERPROFILE 环境变量 */
    wchar_t userProfile[MAX_PATH];
    if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH) > 0) {
        ADD_DIR(userProfile);
    }

    /* 2. SystemDrive + \Users */
    wchar_t sysDrive[MAX_PATH];
    if (GetEnvironmentVariableW(L"SystemDrive", sysDrive, MAX_PATH) > 0) {
        wchar_t usersPath[MAX_PATH];
        swprintf_s(usersPath, MAX_PATH, L"%s\\Users", sysDrive);
        WIN32_FIND_DATAW fd;
        wchar_t searchPath[MAX_PATH];
        swprintf_s(searchPath, MAX_PATH, L"%s\\*", usersPath);
        HANDLE hFind = FindFirstFileW(searchPath, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                wchar_t full[MAX_PATH];
                swprintf_s(full, MAX_PATH, L"%s\\%s", usersPath, fd.cFileName);
                ADD_DIR(full);
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    }

    /* 3. GetWindowsDirectoryW 推导系统盘 */
    wchar_t winDir[MAX_PATH];
    if (GetWindowsDirectoryW(winDir, MAX_PATH)) {
        wchar_t drive[4] = { winDir[0], L':', L'\\', 0 };
        wchar_t usersPath[MAX_PATH];
        swprintf_s(usersPath, MAX_PATH, L"%sUsers", drive);
        WIN32_FIND_DATAW fd;
        wchar_t searchPath[MAX_PATH];
        swprintf_s(searchPath, MAX_PATH, L"%s\\*", usersPath);
        HANDLE hFind = FindFirstFileW(searchPath, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                wchar_t full[MAX_PATH];
                swprintf_s(full, MAX_PATH, L"%s\\%s", usersPath, fd.cFileName);
                ADD_DIR(full);
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    }

    /* 4. 兜底 C:\Users */
    {
        const wchar_t *cUsers = L"C:\\Users";
        WIN32_FIND_DATAW fd;
        wchar_t searchPath[MAX_PATH];
        swprintf_s(searchPath, MAX_PATH, L"%s\\*", cUsers);
        HANDLE hFind = FindFirstFileW(searchPath, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                wchar_t full[MAX_PATH];
                swprintf_s(full, MAX_PATH, L"%s\\%s", cUsers, fd.cFileName);
                ADD_DIR(full);
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    }

    #undef ADD_DIR

    for (int i = 0; i < seenCount && count < maxCount; i++) {
        wcscpy_s(dirs[count++], MAX_PATH, seen[i]);
    }
    return count;
}

/* ---------- 查找插件 static 目录 ---------- */
static int FindPluginStaticDirs(wchar_t outList[][MAX_PATH], int maxCount)
{
    int count = 0;
    wchar_t userDirs[64][MAX_PATH];
    int userCount = CollectUserDirs(userDirs, 64);

    for (int i = 0; i < userCount; i++) {
        ScanUserProfile(userDirs[i], outList, maxCount, &count);
    }

    return count;
}

/* ---------- 执行更新 ---------- */
static UpdateError DoUpdate(void)
{
    wchar_t dirs[64][MAX_PATH];
    int dirCount = FindPluginStaticDirs(dirs, 64);
    if (dirCount <= 0) return ERR_PLUGIN_NOT_FOUND;

    for (int d = 0; d < dirCount; d++) {
        for (int f = 0; f < g_fileCount; f++) {
            DWORD dataSize = 0;
            BYTE *data = DownloadFile(g_files[f], &dataSize);
            if (!data || dataSize == 0) {
                free(data);
                return ERR_DOWNLOAD_FAILED;
            }

            wchar_t fullPath[MAX_PATH];
            swprintf_s(fullPath, MAX_PATH, L"%s\\%s", dirs[d], g_files[f]);

            HANDLE hFile = CreateFileW(fullPath, GENERIC_WRITE, 0, NULL,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile == INVALID_HANDLE_VALUE) {
                free(data);
                DWORD err = GetLastError();
                if (err == ERROR_ACCESS_DENIED || err == ERROR_SHARING_VIOLATION ||
                    err == ERROR_LOCK_VIOLATION) {
                    return ERR_REPLACE_FAILED;
                }
                return ERR_WRITE_FAILED;
            }

            DWORD written = 0;
            BOOL ok = WriteFile(hFile, data, dataSize, &written, NULL);
            CloseHandle(hFile);
            free(data);

            if (!ok || written != dataSize) {
                return ERR_WRITE_FAILED;
            }
        }
    }

    return ERR_NONE;
}

/* ---------- 主窗口 ---------- */
static HWND g_hBtn = NULL;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        g_hBtn = CreateWindowExW(0, L"BUTTON", L"开始更新",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            60, 30, 160, 40, hwnd, (HMENU)100, NULL, NULL);
        HFONT hFont = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");
        SendMessageW(g_hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 100 && HIWORD(wParam) == BN_CLICKED) {
            EnableWindow(g_hBtn, FALSE);
            UpdateError err = DoUpdate();
            EnableWindow(g_hBtn, TRUE);

            if (err == ERR_NONE) {
                ShowCustomMsgBox(L"更新成功", L"所有插件静态文件已更新完毕！", L"关闭小程序");
            } else {
                const wchar_t *reason = L"未知错误";
                switch (err) {
                case ERR_PLUGIN_NOT_FOUND:
                    reason = L"找不到插件，可能未安装";
                    break;
                case ERR_DOWNLOAD_FAILED:
                    reason = L"下载文件失败，请检查网络后重试";
                    break;
                case ERR_WRITE_FAILED:
                    reason = L"文件写入失败，请检查插件地址是否受保护，请关闭保护后重试";
                    break;
                case ERR_REPLACE_FAILED:
                    reason = L"替换文件失败";
                    break;
                default:
                    break;
                }
                ShowCustomMsgBox(L"更新失败", reason, L"确定");
            }
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    (void)hPrev; (void)lpCmd;

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"JassUpdaterWnd";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, L"JassUpdaterWnd", L"Jass 插件更新工具",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 130,
        NULL, NULL, hInst, NULL);

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
